#define _GNU_SOURCE
/*
 * Build:
 *   gcc -Wall -Wextra -O2 -g -pthread -Irpi_app/include \
 *       scripts/ioctl_stress.c -o build/ioctl_stress
 *
 * Run on Raspberry Pi after loading gateway_monitor.ko:
 *   sudo taskset -c 0-3 ./build/ioctl_stress -t 10 -n 100000
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "gateway_monitor_ioctl.h"

#define DEFAULT_THREADS 10
#define DEFAULT_LOOPS 100000
#define DEFAULT_READ_EVERY 16
#define IIO_ROOT "/sys/bus/iio/devices"

static volatile sig_atomic_t stop_requested;

struct stress_config {
    const char *device;
    const char *iio_dir;
    int threads;
    int loops;
    int read_every;
    int pin_threads;
};

struct worker_stats {
    uint64_t set_ops;
    uint64_t get_ops;
    uint64_t iio_reads;
    uint64_t ioctl_errors;
    uint64_t verify_errors;
    uint64_t iio_errors;
};

struct start_gate {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int start;
};

struct worker_ctx {
    int id;
    const struct stress_config *cfg;
    struct start_gate *gate;
    struct worker_stats stats;
};

struct progress_ctx {
    const struct stress_config *cfg;
    const struct worker_ctx *workers;
    struct timespec start;
    int done;
};

static void handle_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
}

static void stat_inc(uint64_t *value)
{
    __atomic_add_fetch(value, 1, __ATOMIC_RELAXED);
}

static uint64_t stat_load(const uint64_t *value)
{
    return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static void progress_set_done(struct progress_ctx *progress)
{
    __atomic_store_n(&progress->done, 1, __ATOMIC_RELEASE);
}

static int progress_is_done(const struct progress_ctx *progress)
{
    return __atomic_load_n(&progress->done, __ATOMIC_ACQUIRE);
}

static void usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("  -d <path>     gateway_monitor device (default: %s)\n", GATEWAY_MONITOR_DEVICE);
    printf("  -i <dir>      IIO device dir, or auto-detect if omitted\n");
    printf("  -t <count>    worker threads (default: %d)\n", DEFAULT_THREADS);
    printf("  -n <count>    loops per thread (default: %d)\n", DEFAULT_LOOPS);
    printf("  -R <count>    read IIO raw files every N loops (default: %d, 0 disables)\n", DEFAULT_READ_EVERY);
    printf("  -A            disable per-thread CPU affinity\n");
    printf("  -h            show this help\n");
}

static int parse_positive(const char *text, int *out)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0 || value > INT32_MAX)
        return -1;

    *out = (int)value;
    return 0;
}

static int parse_args(int argc, char **argv, struct stress_config *cfg)
{
    int opt;

    while ((opt = getopt(argc, argv, "d:i:t:n:R:Ah")) != -1) {
        switch (opt) {
        case 'd':
            cfg->device = optarg;
            break;
        case 'i':
            cfg->iio_dir = optarg;
            break;
        case 't':
            if (parse_positive(optarg, &cfg->threads) != 0 || cfg->threads <= 0)
                return -1;
            break;
        case 'n':
            if (parse_positive(optarg, &cfg->loops) != 0)
                return -1;
            break;
        case 'R':
            if (parse_positive(optarg, &cfg->read_every) != 0)
                return -1;
            break;
        case 'A':
            cfg->pin_threads = 0;
            break;
        case 'h':
            usage(argv[0]);
            exit(0);
        default:
            return -1;
        }
    }

    return 0;
}

static int read_text_file(const char *path, char *buf, size_t size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n;

    if (fd < 0)
        return -1;

    n = read(fd, buf, size - 1);
    close(fd);
    if (n <= 0)
        return -1;

    buf[n] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

static int find_iio_device(char *out, size_t out_size)
{
    DIR *dir = opendir(IIO_ROOT);
    struct dirent *ent;

    if (!dir)
        return -1;

    while ((ent = readdir(dir)) != NULL) {
        char name_path[512];
        char name[128];
        int n;

        if (strncmp(ent->d_name, "iio:device", 10) != 0)
            continue;

        n = snprintf(name_path, sizeof(name_path), "%s/%s/name", IIO_ROOT, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(name_path))
            continue;

        if (read_text_file(name_path, name, sizeof(name)) != 0)
            continue;

        if (strstr(name, "mpu") || strstr(name, "icm") || strstr(name, "accel")) {
            n = snprintf(out, out_size, "%s/%s", IIO_ROOT, ent->d_name);
            closedir(dir);
            return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
        }
    }

    closedir(dir);
    return -1;
}

static int read_iio_axis(const char *iio_dir, const char *axis)
{
    char path[512];
    char buf[64];
    int n = snprintf(path, sizeof(path), "%s/in_accel_%s_raw", iio_dir, axis);

    if (n < 0 || (size_t)n >= sizeof(path))
        return -1;

    return read_text_file(path, buf, sizeof(buf));
}

static int read_iio_snapshot(const char *iio_dir)
{
    if (!iio_dir)
        return -1;

    if (read_iio_axis(iio_dir, "x") != 0)
        return -1;
    if (read_iio_axis(iio_dir, "y") != 0)
        return -1;
    if (read_iio_axis(iio_dir, "z") != 0)
        return -1;

    return 0;
}

static int pin_current_thread(int worker_id)
{
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    cpu_set_t set;

    if (cpus <= 0)
        return -1;

    CPU_ZERO(&set);
    CPU_SET(worker_id % (int)cpus, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

static void wait_for_start(struct start_gate *gate)
{
    pthread_mutex_lock(&gate->lock);
    while (!gate->start)
        pthread_cond_wait(&gate->cond, &gate->lock);
    pthread_mutex_unlock(&gate->lock);
}

static void release_start_gate(struct start_gate *gate)
{
    pthread_mutex_lock(&gate->lock);
    gate->start = 1;
    pthread_cond_broadcast(&gate->cond);
    pthread_mutex_unlock(&gate->lock);
}

static gateway_monitor_threshold_t make_pattern(int worker_id, int loop, int channel)
{
    uint32_t seq = (uint32_t)((worker_id * 131 + loop * 7 + channel * 17) & 0x7fff);
    uint32_t inv = (~seq) & 0x7fff;

    return (gateway_monitor_threshold_t)((seq << 15) | inv);
}

static int pattern_is_valid(gateway_monitor_threshold_t value)
{
    uint32_t raw = (uint32_t)value;
    uint32_t seq = (raw >> 15) & 0x7fff;
    uint32_t inv = raw & 0x7fff;

    if (raw > 0x3fffffff)
        return 0;

    return inv == ((~seq) & 0x7fff);
}

static unsigned long set_cmd_for_channel(int channel)
{
    static const unsigned long cmds[] = {
        GATEWAY_MONITOR_IOC_SET_PEAK_THR,
        GATEWAY_MONITOR_IOC_SET_RMS_THR,
        GATEWAY_MONITOR_IOC_SET_GYRO_THR,
    };

    return cmds[channel % 3];
}

static unsigned long get_cmd_for_channel(int channel)
{
    static const unsigned long cmds[] = {
        GATEWAY_MONITOR_IOC_GET_PEAK_THR,
        GATEWAY_MONITOR_IOC_GET_RMS_THR,
        GATEWAY_MONITOR_IOC_GET_GYRO_THR,
    };

    return cmds[channel % 3];
}

static void *worker_main(void *arg)
{
    struct worker_ctx *ctx = arg;
    const struct stress_config *cfg = ctx->cfg;
    int fd;

    if (cfg->pin_threads && pin_current_thread(ctx->id) != 0) {
        fprintf(stderr, "worker %d: failed to set CPU affinity: %s\n", ctx->id, strerror(errno));
    }

    wait_for_start(ctx->gate);

    fd = open(cfg->device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "worker %d: open %s failed: %s\n", ctx->id, cfg->device, strerror(errno));
        stat_inc(&ctx->stats.ioctl_errors);
        return NULL;
    }

    for (int i = 0; i < cfg->loops && !stop_requested; ++i) {
        int channel = (ctx->id + i) % 3;
        gateway_monitor_threshold_t expected = make_pattern(ctx->id, i, channel);
        gateway_monitor_threshold_t actual = 0;

        if (ioctl(fd, set_cmd_for_channel(channel), &expected) != 0) {
            stat_inc(&ctx->stats.ioctl_errors);
            continue;
        }
        stat_inc(&ctx->stats.set_ops);

        if (ioctl(fd, get_cmd_for_channel(channel), &actual) != 0) {
            stat_inc(&ctx->stats.ioctl_errors);
            continue;
        }
        stat_inc(&ctx->stats.get_ops);

        if (!pattern_is_valid(actual))
            stat_inc(&ctx->stats.verify_errors);

        if (cfg->iio_dir && cfg->read_every > 0 && i % cfg->read_every == 0) {
            if (read_iio_snapshot(cfg->iio_dir) == 0)
                stat_inc(&ctx->stats.iio_reads);
            else
                stat_inc(&ctx->stats.iio_errors);
        }
    }

    close(fd);
    return NULL;
}

static int single_thread_sanity(const struct stress_config *cfg)
{
    int fd = open(cfg->device, O_RDWR | O_CLOEXEC);

    if (fd < 0) {
        fprintf(stderr, "sanity: open %s failed: %s\n", cfg->device, strerror(errno));
        return -1;
    }

    for (int channel = 0; channel < 3; ++channel) {
        gateway_monitor_threshold_t expected = make_pattern(0, channel + 1, channel);
        gateway_monitor_threshold_t actual = 0;

        if (ioctl(fd, set_cmd_for_channel(channel), &expected) != 0) {
            fprintf(stderr, "sanity: SET channel %d failed: %s\n", channel, strerror(errno));
            close(fd);
            return -1;
        }

        if (ioctl(fd, get_cmd_for_channel(channel), &actual) != 0) {
            fprintf(stderr, "sanity: GET channel %d failed: %s\n", channel, strerror(errno));
            close(fd);
            return -1;
        }

        if (actual != expected) {
            fprintf(stderr, "sanity: channel %d mismatch expected=%d actual=%d\n",
                    channel, expected, actual);
            close(fd);
            return -1;
        }
    }

    close(fd);
    return 0;
}

static double elapsed_sec(const struct timespec *start, const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static struct worker_stats collect_stats(const struct worker_ctx *workers, int count)
{
    struct worker_stats total = {0};

    for (int i = 0; i < count; ++i) {
        total.set_ops += stat_load(&workers[i].stats.set_ops);
        total.get_ops += stat_load(&workers[i].stats.get_ops);
        total.iio_reads += stat_load(&workers[i].stats.iio_reads);
        total.ioctl_errors += stat_load(&workers[i].stats.ioctl_errors);
        total.verify_errors += stat_load(&workers[i].stats.verify_errors);
        total.iio_errors += stat_load(&workers[i].stats.iio_errors);
    }

    return total;
}

static void print_progress_line(const struct progress_ctx *progress)
{
    struct timespec now;
    struct worker_stats total = collect_stats(progress->workers, progress->cfg->threads);
    uint64_t planned = (uint64_t)progress->cfg->threads * (uint64_t)progress->cfg->loops;
    uint64_t completed = total.set_ops;
    double percent = planned > 0 ? ((double)completed * 100.0) / (double)planned : 100.0;

    clock_gettime(CLOCK_MONOTONIC, &now);
    double sec = elapsed_sec(&progress->start, &now);
    double ioctl_ops = (double)(total.set_ops + total.get_ops);

    if (percent > 100.0)
        percent = 100.0;

    printf("[progress] %.1f%% set=%llu/%llu get=%llu iio=%llu errors=%llu verify=%llu rate=%.0f ops/s elapsed=%.1fs\n",
           percent,
           (unsigned long long)total.set_ops,
           (unsigned long long)planned,
           (unsigned long long)total.get_ops,
           (unsigned long long)total.iio_reads,
           (unsigned long long)(total.ioctl_errors + total.iio_errors),
           (unsigned long long)total.verify_errors,
           sec > 0.0 ? ioctl_ops / sec : 0.0,
           sec);
    fflush(stdout);
}

static void *progress_main(void *arg)
{
    const struct progress_ctx *progress = arg;

    while (!progress_is_done(progress)) {
        sleep(1);
        print_progress_line(progress);
    }

    return NULL;
}

int main(int argc, char **argv)
{
    struct stress_config cfg = {
        .device = GATEWAY_MONITOR_DEVICE,
        .iio_dir = NULL,
        .threads = DEFAULT_THREADS,
        .loops = DEFAULT_LOOPS,
        .read_every = DEFAULT_READ_EVERY,
        .pin_threads = 1,
    };
    char detected_iio[512];
    pthread_t *threads = NULL;
    struct worker_ctx *ctxs = NULL;
    struct start_gate gate = {
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .start = 0,
    };
    struct timespec start;
    struct timespec end;
    struct worker_stats total = {0};
    struct progress_ctx progress = {0};
    pthread_t progress_thread;
    int progress_started = 0;
    int started_threads = 0;
    int rc = 1;

    if (parse_args(argc, argv, &cfg) != 0) {
        usage(argv[0]);
        return 2;
    }

    if (!cfg.iio_dir && find_iio_device(detected_iio, sizeof(detected_iio)) == 0)
        cfg.iio_dir = detected_iio;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("gateway_monitor ioctl stress\n");
    printf("  device:      %s\n", cfg.device);
    printf("  iio:         %s\n", cfg.iio_dir ? cfg.iio_dir : "disabled/not found");
    printf("  threads:     %d\n", cfg.threads);
    printf("  loops/thread:%d\n", cfg.loops);
    printf("  read_every:  %d\n", cfg.read_every);
    printf("  affinity:    %s\n", cfg.pin_threads ? "enabled" : "disabled");
    fflush(stdout);

    if (single_thread_sanity(&cfg) != 0)
        return 1;

    threads = calloc((size_t)cfg.threads, sizeof(*threads));
    ctxs = calloc((size_t)cfg.threads, sizeof(*ctxs));
    if (!threads || !ctxs) {
        fprintf(stderr, "allocation failed\n");
        goto out;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    progress.cfg = &cfg;
    progress.workers = ctxs;
    progress.start = start;

    if (pthread_create(&progress_thread, NULL, progress_main, &progress) == 0) {
        progress_started = 1;
    } else {
        fprintf(stderr, "progress thread unavailable; continuing without live progress\n");
    }

    for (int i = 0; i < cfg.threads; ++i) {
        ctxs[i].id = i;
        ctxs[i].cfg = &cfg;
        ctxs[i].gate = &gate;
        if (pthread_create(&threads[i], NULL, worker_main, &ctxs[i]) != 0) {
            fprintf(stderr, "pthread_create %d failed\n", i);
            stop_requested = 1;
            break;
        }
        started_threads++;
    }

    release_start_gate(&gate);

    for (int i = 0; i < started_threads; ++i)
        pthread_join(threads[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &end);

    if (progress_started) {
        progress_set_done(&progress);
        pthread_join(progress_thread, NULL);
    }

    total = collect_stats(ctxs, cfg.threads);

    double sec = elapsed_sec(&start, &end);
    double ioctl_ops = (double)(total.set_ops + total.get_ops);

    printf("\nresult\n");
    printf("  set_ops:       %llu\n", (unsigned long long)total.set_ops);
    printf("  get_ops:       %llu\n", (unsigned long long)total.get_ops);
    printf("  iio_reads:     %llu\n", (unsigned long long)total.iio_reads);
    printf("  ioctl_errors:  %llu\n", (unsigned long long)total.ioctl_errors);
    printf("  verify_errors: %llu\n", (unsigned long long)total.verify_errors);
    printf("  iio_errors:    %llu\n", (unsigned long long)total.iio_errors);
    printf("  elapsed:       %.3f sec\n", sec);
    printf("  ioctl_rate:    %.0f ops/sec\n", sec > 0.0 ? ioctl_ops / sec : 0.0);

    rc = (total.ioctl_errors == 0 && total.verify_errors == 0) ? 0 : 1;

out:
    free(ctxs);
    free(threads);
    return rc;
}
