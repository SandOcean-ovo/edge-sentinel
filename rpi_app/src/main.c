#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>

#include "conf.h"
#include "log.h"
#include "crc16.h"
#include "ringbuf.h"
#include "uart.h"
#include "parse.h"
#include "db.h"
#include "protocol_utils.h"
#include "net_client.h"
#include "gateway_pose.h"
#include "gateway_monitor_ioctl.h"

#define MAX_EVENTS 16
#define DEFAULT_CONF_PATH "/etc/edge_gateway/gateway.conf"
#define BATCH_SIZE 10
#define JSON_PACK_SIZE 4096
#define DEFAULT_CLEANUP_INTERVAL 7
#define TIMER_INTERVAL_SEC 5
#define CLEANUP_THRESHOLD (3600 / TIMER_INTERVAL_SEC)
#define EDGE_ALARM_DEV "/dev/edge_alarm"

/* ---------- 全局状态 ---------- */
static uint8_t g_buf[512];
static RingBuf_t g_ringbuf;
static SentinelFrame_t g_frame = {0};

static volatile sig_atomic_t keep_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    keep_running = 0;
}

static void trace_marker(const char *msg)
{
    int fd = open("/sys/kernel/debug/tracing/trace_marker", O_WRONLY | O_CLOEXEC);
    if (fd >= 0)
    {
        write(fd, msg, strlen(msg));
        close(fd);
    }
}

/* ---------- 小工具 ---------- */

static int epoll_add(int epfd, int fd, uint32_t events)
{
    struct epoll_event ev = {.events = events, .data.fd = fd};
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

static int epoll_mod(int epfd, int fd, uint32_t events)
{
    struct epoll_event ev = {.events = events, .data.fd = fd};
    return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

static void epoll_del(int epfd, int fd)
{
    if (fd >= 0)
    {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
    }
}

static void safe_close(int *fd)
{
    if (*fd >= 0)
    {
        close(*fd);
        *fd = -1;
    }
}

/* 关掉 TCP，同时从 epoll 摘除 */
static void drop_tcp(int epfd, int *sock_fd)
{
    if (*sock_fd < 0)
        return;
    epoll_del(epfd, *sock_fd);
    net_client_deinit();
    *sock_fd = -1;
}

/* 尝试重连并注册到 epoll */
static void try_reconnect_tcp(int epfd, int *sock_fd, const GatewayConfig_t *cfg)
{
    edge_log(LOG_INFO, "Timer: Attempting to reconnect tcp...");
    if (net_client_init(cfg->ip, cfg->port) != 0)
        return;

    *sock_fd = net_client_get_fd();
    if (*sock_fd < 0)
        return;

    // 连接建立前监听可写事件，连上后再改为只监听读和断开
    epoll_add(epfd, *sock_fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP);
}

static int open_edge_alarm_device(void)
{
    int fd = open(EDGE_ALARM_DEV, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        edge_log(LOG_WARN, "Failed to open %s: %s", EDGE_ALARM_DEV, strerror(errno));
    }
    return fd;
}

static void push_thresholds_to_driver(const GatewayConfig_t *cfg)
{
    int fd = open(GATEWAY_MONITOR_DEVICE, O_RDWR);
    if (fd < 0)
    {
        edge_log(LOG_WARN, "Cannot open %s for threshold push: %s",
                 GATEWAY_MONITOR_DEVICE, strerror(errno));
        return;
    }

    int32_t peak_mg = (int32_t)(cfg->accel_peak_threshold * 1000.0f);
    int32_t rms_mg  = (int32_t)(cfg->accel_rms_threshold * 1000.0f);
    int32_t gyro_md = (int32_t)(cfg->gyro_threshold * 1000.0f);

    if (ioctl(fd, GATEWAY_MONITOR_IOC_SET_PEAK_THR, &peak_mg) != 0)
        edge_log(LOG_WARN, "Failed to set peak threshold via ioctl");
    else
        edge_log(LOG_INFO, "Driver peak threshold set: %.3f g", cfg->accel_peak_threshold);

    if (ioctl(fd, GATEWAY_MONITOR_IOC_SET_RMS_THR, &rms_mg) != 0)
        edge_log(LOG_WARN, "Failed to set rms threshold via ioctl");
    else
        edge_log(LOG_INFO, "Driver rms threshold set: %.3f g", cfg->accel_rms_threshold);

    if (ioctl(fd, GATEWAY_MONITOR_IOC_SET_GYRO_THR, &gyro_md) != 0)
        edge_log(LOG_WARN, "Failed to set gyro threshold via ioctl");
    else
        edge_log(LOG_INFO, "Driver gyro threshold set: %.1f deg/s", cfg->gyro_threshold);

    close(fd);
}

static int refresh_edge_alarm_fd(int epfd, int *alarm_fd)
{
    if (*alarm_fd >= 0)
        return 0;

    int new_fd = open_edge_alarm_device();
    if (new_fd < 0)
        return -1;

    if (epoll_add(epfd, new_fd, EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP | EPOLLRDHUP) != 0)
    {
        edge_log(LOG_ERROR, "Failed to add %s to epoll: %s", EDGE_ALARM_DEV, strerror(errno));
        close(new_fd);
        return -1;
    }

    *alarm_fd = new_fd;
    edge_log(LOG_INFO, "%s detected and registered to epoll (fd=%d)", EDGE_ALARM_DEV, new_fd);
    return 0;
}

static void maybe_send_alarm_snapshot(int epfd, int *sock_fd, const GatewayConfig_t *cfg)
{
    gateway_pose_t pose = {0};
    char json_payload[JSON_PACK_SIZE];

    if (gateway_pose_read_snapshot(&pose) != 0)
    {
        edge_log(LOG_WARN, "Alarm triggered but no valid IIO pose snapshot available");
        pose.timestamp = (uint32_t)time(NULL);
    }

    if (protocol_encode_alarm_pose(cfg->device_id, 1, &pose,
                                   json_payload, sizeof(json_payload)) != 0)
        return;

    edge_log(LOG_INFO, "Alarm payload ready: %s", json_payload);

    if (net_client_get_fd() < 0)
    {
        drop_tcp(epfd, sock_fd);
        try_reconnect_tcp(epfd, sock_fd, cfg);
        return;
    }

    int total_send = (int)strlen(json_payload);
    int sent_bytes = net_client_send(json_payload, total_send);
    if (sent_bytes < 0)
    {
        edge_log(LOG_ERROR, "Failed to send alarm snapshot");
        drop_tcp(epfd, sock_fd);
        return;
    }

    if (sent_bytes != total_send)
    {
        edge_log(LOG_WARN, "Partial alarm send (%d/%d)", sent_bytes, total_send);
        return;
    }

    edge_log(LOG_INFO, "Alarm snapshot sent successfully");
}

/* ---------- 定时器里：上报一批未发数据 ---------- */

static void report_unsent_batch(int epfd, int *sock_fd,
                                sensor_data_t *batch_list,
                                char *json_payload, size_t json_size)
{
    int count = db_get_unsent_records(batch_list, BATCH_SIZE);
    if (count <= 0)
        return;

    edge_log(LOG_INFO, "Timer: Found %d unsent records, reporting...", count);

    if (protocol_encode_batch(batch_list, count, json_payload, json_size) != 0)
        return;

    int total_send = (int)strlen(json_payload);
    int sent_bytes = net_client_send(json_payload, total_send);

    if (sent_bytes < 0)
    {
        edge_log(LOG_ERROR, "Failed to send data to cloud, marking connection as broken");
        drop_tcp(epfd, sock_fd);
        return;
    }

    if (sent_bytes != total_send)
    {
        edge_log(LOG_WARN, "Partial send (%d/%d), retry next time", sent_bytes, total_send);
        return;
    }

    edge_log(LOG_INFO, "Fully sent batch data! Marking...");
    for (int j = 0; j < count; ++j)
    {
        db_mark_as_sent(batch_list[j].db_id);
    }
}

/* ---------- 三类事件处理 ---------- */

static void handle_uart_event(int uart_fd, uint32_t events)
{
    if (!(events & EPOLLIN))
        return;

    uint8_t rx_temp[128];
    ssize_t n = read(uart_fd, rx_temp, sizeof(rx_temp));
    if (n > 0)
    {
        RingBuf_writeblocks(&g_ringbuf, rx_temp, n);
    }

    while (protocol_parse(&g_ringbuf, &g_frame) == true)
    {
        handle_frame(&g_frame);
    }
}

static void handle_alarm_event(int epfd, int *alarm_fd, int *sock_fd, const GatewayConfig_t *cfg, uint32_t events)
{
    if (*alarm_fd < 0)
        return;

    if (events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))
    {
        edge_log(LOG_WARN, "edge_alarm device disappeared");
        epoll_del(epfd, *alarm_fd);
        safe_close(alarm_fd);
        return;
    }

    char discard[16];
    while (read(*alarm_fd, discard, sizeof(discard)) > 0)
    {
        /* 清空边沿触发数据 */
    }

    edge_log(LOG_INFO, "edge_alarm triggered, reading IIO snapshot...");

    /* 用户态兜底：读取姿态后先用本地阈值判断 */
    gateway_pose_t pose_check = {0};
    if (gateway_pose_read_snapshot(&pose_check) == 0 && pose_check.valid)
    {
        const char *reason = NULL;
        if (gateway_pose_check_threshold(&pose_check,
                                         cfg->accel_peak_threshold,
                                         cfg->accel_rms_threshold,
                                         cfg->gyro_threshold,
                                         &reason))
        {
            edge_log(LOG_WARN, "Userspace fallback: gateway pose exceeds %s!", reason);
        }
    }

    maybe_send_alarm_snapshot(epfd, sock_fd, cfg);
}

static void handle_timer_event(int epfd, int timer_fd, int *sock_fd,
                               int *alarm_fd, const GatewayConfig_t *cfg,
                               sensor_data_t *batch_list,
                               char *json_payload, size_t json_size,
                               int *cleanup_counter)
{
    uint64_t expirations;
    ssize_t s = read(timer_fd, &expirations, sizeof(expirations));
    if (s != sizeof(expirations))
        return;

    if (refresh_edge_alarm_fd(epfd, alarm_fd) == 0 && *alarm_fd >= 0)
    {
        /* Do Nothing */
    }

    // 连接掉了：尝试重连，然后这一轮就不做上报
    if (net_client_get_fd() < 0)
    {
        drop_tcp(epfd, sock_fd);
        try_reconnect_tcp(epfd, sock_fd, cfg);
        return;
    }

    (*cleanup_counter)++;
    if (*cleanup_counter >= CLEANUP_THRESHOLD)
    {
        edge_log(LOG_INFO, "Timer: Performing periodic cleanup of old data...");
        db_cleanup_old_data(DEFAULT_CLEANUP_INTERVAL);
        *cleanup_counter = 0;
    }
    // 连接正常：上报一批未发数据
    report_unsent_batch(epfd, sock_fd, batch_list, json_payload, json_size);
}

static void handle_tcp_event(int epfd, int *sock_fd, uint32_t events)
{
    if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
    {
        edge_log(LOG_WARN, "TCP Connection error or closed by peer");
        drop_tcp(epfd, sock_fd);
        return;
    }

    if (!(events & EPOLLOUT))
        return;

    if (net_client_check_connect_status() != 0)
    {
        edge_log(LOG_ERROR, "TCP Connection failed");
        drop_tcp(epfd, sock_fd);
        return;
    }

    edge_log(LOG_INFO, "TCP Connected successfully!");
    // 连上后立刻取消 EPOLLOUT，否则会死循环触发
    epoll_mod(epfd, *sock_fd, EPOLLIN | EPOLLRDHUP);
}

/* ---------- 命令行解析 ---------- */

static int parse_args(int argc, char **argv, const char **conf_path)
{
    int opt;
    while ((opt = getopt(argc, argv, "c:h")) != -1)
    {
        switch (opt)
        {
        case 'c':
            *conf_path = optarg;
            break;
        case 'h':
            printf("Usage: %s [-c config_file_path]\n", argv[0]);
            return 1; // 正常退出
        default:
            fprintf(stderr, "Usage: %s [-c config_file_path]\n", argv[0]);
            return -1; // 参数错误
        }
    }
    return 0;
}

/* ---------- timerfd 初始化 ---------- */

static int setup_timerfd(int sec)
{
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (fd < 0)
        return -1;

    struct itimerspec timeout = {
        .it_value = {.tv_sec = sec, .tv_nsec = 0},
        .it_interval = {.tv_sec = sec, .tv_nsec = 0},
    };
    timerfd_settime(fd, 0, &timeout, NULL);
    return fd;
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    const char *conf_path = DEFAULT_CONF_PATH;
    int rc = parse_args(argc, argv, &conf_path);
    if (rc != 0)
        return (rc > 0) ? 0 : -1;

    int cleanup_counter = 0;
    int alarm_fd = -1;

    /* ----- 各模块初始化 ----- */
    GatewayConfig_t config = {0};
    if (load_config(conf_path, &config) != 0)
    {
        fprintf(stderr, "Fatal: Failed to load config from %s\n", conf_path);
        return -1;
    }

    if (log_init(config.log_file) != 0)
    {
        fprintf(stderr, "Fatal: Failed to initialize log at %s\n", config.log_file);
        return -1;
    }
    edge_log(LOG_INFO, "Gateway started! Baudrate: %d", config.baudrate);

    push_thresholds_to_driver(&config);

    RingBuf_init(&g_ringbuf, g_buf, sizeof(g_buf));

    int uart_fd = uart_init(config.uart_dev, config.baudrate);
    if (uart_fd < 0)
    {
        fprintf(stderr, "Fatal: Failed to initialize uart at %s\n", config.uart_dev);
        return -1;
    }
    edge_log(LOG_INFO, "Start uart connection with device: %s", config.uart_dev);

    if (db_init(config.db_path) != 0)
    {
        fprintf(stderr, "Fatal: Failed to initialize database at %s\n", config.db_path);
        return -1;
    }

    if (net_client_init(config.ip, config.port) != 0)
    {
        // 连不上不致命，定时器会重连
        edge_log(LOG_ERROR, "Failed to initialize net client at %s:%d",
                 config.ip, config.port);
    }
    int sock_fd = net_client_get_fd();

    alarm_fd = open_edge_alarm_device();

    int epfd = epoll_create1(0);
    if (epfd < 0)
    {
        fprintf(stderr, "Fatal: Failed to create epoll instance\n");
        safe_close(&alarm_fd);
        return -1;
    }

    int timer_fd = setup_timerfd(TIMER_INTERVAL_SEC);
    if (timer_fd < 0)
    {
        fprintf(stderr, "Fatal: Failed to create timerfd\n");
        safe_close(&alarm_fd);
        safe_close(&uart_fd);
        safe_close(&epfd);
        return -1;
    }

    epoll_add(epfd, uart_fd, EPOLLIN);
    epoll_add(epfd, timer_fd, EPOLLIN);
    if (alarm_fd >= 0)
    {
        epoll_add(epfd, alarm_fd, EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP | EPOLLRDHUP);
    }
    if (sock_fd >= 0)
    {
        epoll_add(epfd, sock_fd, EPOLLIN);
    }

    sensor_data_t batch_list[BATCH_SIZE];
    char json_payload[JSON_PACK_SIZE];
    struct epoll_event events[MAX_EVENTS];

    while (keep_running)
    {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        trace_marker("USER_EPOLL_RETURN\n");
        if (nfds < 0)
        {
            if (errno == EINTR)
                continue;
            edge_log(LOG_ERROR, "epoll_wait failed: %s", strerror(errno));
            continue;
        }

        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == uart_fd)
            {
                handle_uart_event(uart_fd, ev);
            }
            else if (fd == timer_fd)
            {
                handle_timer_event(epfd, timer_fd, &sock_fd, &alarm_fd, &config,
                                   batch_list, json_payload, sizeof(json_payload), &cleanup_counter);
            }
            else if (fd == alarm_fd)
            {
                handle_alarm_event(epfd, &alarm_fd, &sock_fd, &config, ev);
            }
            else if (fd == sock_fd)
            {
                handle_tcp_event(epfd, &sock_fd, ev);
            }
        }
    }

    edge_log(LOG_INFO, "Gateway shutting down.");

    safe_close(&uart_fd);
    safe_close(&timer_fd);
    safe_close(&alarm_fd);
    safe_close(&epfd);
    net_client_deinit();
    db_deinit();
    log_deinit();

    return 0;
}
