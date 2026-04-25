#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "conf.h"
#include "log.h"
#include "crc16.h"
#include "ringbuf.h"
#include "uart.h"
#include "parse.h"
#include "db.h"
#include "protocol_utils.h"
#include "net_client.h"

#define MAX_EVENTS 10
#define DEFAULT_CONF_PATH "/etc/edge_gateway/gateway.conf"
#define BATCH_SIZE 10
#define JSON_PACK_SIZE 4096
#define DEFAULT_CLEANUP_INTERVAL 7
#define TIMER_INTERVAL_SEC 5
#define CLEANUP_THRESHOLD (3600 / TIMER_INTERVAL_SEC)

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

static void handle_timer_event(int epfd, int timer_fd, int *sock_fd,
                               const GatewayConfig_t *cfg,
                               sensor_data_t *batch_list,
                               char *json_payload, size_t json_size,
                               int *cleanup_counter)
{
    uint64_t expirations;
    ssize_t s = read(timer_fd, &expirations, sizeof(expirations));
    if (s != sizeof(expirations))
        return;

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

    /* ----- epoll & timerfd ----- */
    int epfd = epoll_create1(0);
    if (epfd < 0)
    {
        fprintf(stderr, "Fatal: Failed to create epoll instance\n");
        return -1;
    }

    int timer_fd = setup_timerfd(TIMER_INTERVAL_SEC);
    if (timer_fd < 0)
    {
        fprintf(stderr, "Fatal: Failed to create timerfd\n");
        return -1;
    }

    epoll_add(epfd, uart_fd, EPOLLIN);
    epoll_add(epfd, timer_fd, EPOLLIN);
    if (sock_fd >= 0)
    {
        epoll_add(epfd, sock_fd, EPOLLIN);
    }

    /* ----- 主循环 ----- */
    sensor_data_t batch_list[BATCH_SIZE];
    char json_payload[JSON_PACK_SIZE];
    struct epoll_event events[MAX_EVENTS];

    while (keep_running)
    {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        if (nfds < 0)
        {
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
                handle_timer_event(epfd, timer_fd, &sock_fd, &config,
                                   batch_list, json_payload, sizeof(json_payload), &cleanup_counter);
            }
            else if (fd == sock_fd)
            {
                handle_tcp_event(epfd, &sock_fd, ev);
            }
        }
    }

    /* ----- 清理 ----- */
    edge_log(LOG_INFO, "Gateway shutting down.");

    if (uart_fd >= 0)
        close(uart_fd);
    if (timer_fd >= 0)
        close(timer_fd);
    if (epfd >= 0)
        close(epfd);
    net_client_deinit();
    db_deinit();
    log_deinit();

    return 0;
}