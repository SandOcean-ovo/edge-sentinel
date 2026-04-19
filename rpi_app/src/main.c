#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <signal.h>
#include <errno.h>
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

uint8_t buf[512];
RingBuf_t ringbuf;
SentinelFrame_t frame = {0};

static volatile sig_atomic_t keep_running = 1;

static void signal_handler(int sig)
{
    keep_running = 0;
}

int main(int argc, char **argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler); // 同时也处理 kill 命令发送的终止信号

    /* ----- 命令行解析 ----- */
    int opt;
    // 默认指向绝对路径
    char *conf_path = DEFAULT_CONF_PATH;

    // 解析命令行参数，"c:" 表示 -c 后面必须跟一个参数
    while ((opt = getopt(argc, argv, "c:h")) != -1)
    {
        switch (opt)
        {
        case 'c':
            conf_path = optarg; // 如果传了 -c，就用用户指定的路径 这是getopt运行后更新的外部变量，直接用即可
            break;
        case 'h':
            printf("Usage: %s [-c config_file_path]", argv[0]);
            return 0;
        default:
            fprintf(stderr, "Usage: %s[-c config_file_path]", argv[0]);
            return -1;
        }
    }

    /* ----- 各模块初始化 ----- */

    GatewayConfig_t config = {0};
    if (load_config(conf_path, &config) != 0)
    {
        // 如果读取失败，程序必须退出，不能带着错误的配置跑
        fprintf(stderr, "Fatal: Failed to load config from %s", conf_path);
        return -1;
    }

    if (log_init(config.log_file) != 0)
    {
        fprintf(stderr, "Fatal: Failed to initialize log at %s", config.log_file);
        return -1;
    }
    edge_log(LOG_INFO, "Gateway started! Baudrate: %d", config.baudrate);

    RingBuf_init(&ringbuf, buf, sizeof(buf));
    int uart_fd = uart_init(UART_DEVICE, config.baudrate);
    if (uart_fd < 0)
    {
        fprintf(stderr, "Fatal: Failed to initialize uart at %s", UART_DEVICE);

        return -1;
    }

    edge_log(LOG_INFO, "Start uart connetion with device: %s", UART_DEVICE);

    if (db_init(config.db_path) != 0)
    {
        fprintf(stderr, "Fatal: Failed to initialize database at %s", config.db_path);
        return -1;
    }

    sensor_data_t batch_list[BATCH_SIZE];
    char json_payload[JSON_PACK_SIZE];

    if (net_client_init(config.ip, config.port) != 0)
    {
        // 在日志报错，但不需要退出程序 在timer里面处理重新连接
        edge_log(LOG_ERROR, "Failed to initialize net client at %s:%d", config.ip, config.port);
    }
    int sock_fd = net_client_get_fd();

    /* ----- epoll初始化 ----- */

    int epfd = epoll_create1(0);
    if (epfd == -1)
    {
        fprintf(stderr, "Fatal: Failed to create epoll instance");
        return -1;
    }

    /* ----- timerfd初始化 ----- */

    // 1. 创建 timerfd (使用 CLOCK_MONOTONIC 比较稳定，不受系统改时间影响)
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    // 2. 设置定时器参数
    struct itimerspec timeout;
    // 首次触发时间 (比如 5 秒后第一次触发)
    timeout.it_value.tv_sec = 5;
    timeout.it_value.tv_nsec = 0;
    // 周期触发间隔 (之后每隔 5 秒触发一次)
    timeout.it_interval.tv_sec = 5;
    timeout.it_interval.tv_nsec = 0;

    // 启动定时器
    timerfd_settime(timer_fd, 0, &timeout, NULL);

    /* ----- 登记epoll ----- */

    // 配置要监听的事件 (登记 uart_fd)
    struct epoll_event ev_uart;
    ev_uart.events = EPOLLIN;  // 监听可读事件
    ev_uart.data.fd = uart_fd; // 绑定是我们关心的 fd
    epoll_ctl(epfd, EPOLL_CTL_ADD, uart_fd, &ev_uart);

    // 登记 timerfd
    struct epoll_event ev_timer;
    ev_timer.events = EPOLLIN;
    ev_timer.data.fd = timer_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev_timer);

    // 登记 TCP
    if (sock_fd >= 0)
    {
        struct epoll_event ev_tcp;
        ev_tcp.events = EPOLLIN;
        ev_tcp.data.fd = sock_fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, sock_fd, &ev_tcp);
    }
    uint8_t rx_temp[128];

    // 存放内核返回的就绪事件列表
    struct epoll_event events[MAX_EVENTS];
    while (keep_running)
    {

        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        if (nfds == -1)
        {
            if (errno == EINTR)
                continue; // 如果是被信号中断，进入下一次循环检查标志位
            continue;
        }

        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == uart_fd)
            {
                // 判断是不是可读事件
                if (events[i].events & EPOLLIN)
                {
                    ssize_t n = read(uart_fd, rx_temp, sizeof(rx_temp));
                    if (n > 0)
                    {
                        RingBuf_writeblocks(&ringbuf, rx_temp, n);
                    }

                    while (protocol_parse(&ringbuf, &frame) == true)
                    {
                        handle_frame(&frame);
                    }
                }
            }

            else if (events[i].data.fd == timer_fd)
            {
                // 读取 timerfd 的事件，必须的步骤，否则定时器不会再次触发
                uint64_t expirations;
                ssize_t s = read(timer_fd, &expirations, sizeof(expirations));
                if (s == sizeof(expirations))
                {
                    int current_sock = net_client_get_fd();
                    if (current_sock < 0)
                    {
                        if (sock_fd >= 0)
                        {
                            epoll_ctl(epfd, EPOLL_CTL_DEL, sock_fd, NULL);
                            sock_fd = -1;
                        }
                        edge_log(LOG_INFO, "Timer: Attempting to reconnect tcp...");
                        if (net_client_init(config.ip, config.port) == 0)
                        {
                            sock_fd = net_client_get_fd();
                            struct epoll_event ev_tcp;
                            ev_tcp.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP; // 连接建立前也监听可写事件，成功后会修改为只监听可读和断开
                            ev_tcp.data.fd = sock_fd;
                            epoll_ctl(epfd, EPOLL_CTL_ADD, sock_fd, &ev_tcp);
                        }
                    }
                    else
                    {
                        int count = db_get_unsent_records(batch_list, BATCH_SIZE);
                        if (count > 0)
                        {
                            edge_log(LOG_INFO, "Timer: Found %d unsent records, reporting...", count);

                            if (protocol_encode_batch(batch_list, count, json_payload, sizeof(json_payload)) == 0)
                            {
                                int total_send = strlen(json_payload);
                                int sent_bytes = net_client_send(json_payload, total_send);
                                if (sent_bytes == total_send)
                                {
                                    edge_log(LOG_INFO, "Fully sent batch data! Marking...");
                                    for (int j = 0; j < count; ++j)
                                    {
                                        db_mark_as_sent(batch_list[j].db_id);
                                    }
                                }
                                else if (sent_bytes >= 0)
                                {
                                    edge_log(LOG_WARN, "Partial send (%d/%d), retry next time", sent_bytes, total_send);
                                }
                                else
                                {
                                    edge_log(LOG_ERROR, "Failed to send data to cloud, marking connection as broken");
                                    epoll_ctl(epfd, EPOLL_CTL_DEL, current_sock, NULL);
                                    net_client_deinit();
                                    sock_fd = -1;
                                }
                            }
                        }
                    }
                }
            }

            else if (events[i].data.fd == sock_fd)
            {
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
                {

                    edge_log(LOG_WARN, "TCP Connection error or closed by peer");
                    epoll_ctl(epfd, EPOLL_CTL_DEL, sock_fd, NULL);
                    net_client_deinit();
                    sock_fd = -1;
                    continue;
                }

                if (events[i].events & EPOLLOUT)
                {
                    if (net_client_check_connect_status() == 0)
                    {
                        edge_log(LOG_INFO, "TCP Connected successfully!");
                        // 【关键】连上后立刻修改事件，不再监听 EPOLLOUT，否则会死循环触发
                        struct epoll_event ev;
                        ev.events = EPOLLIN | EPOLLRDHUP; // 只保留读和断开检测
                        ev.data.fd = sock_fd;
                        epoll_ctl(epfd, EPOLL_CTL_MOD, sock_fd, &ev);
                    }
                    else
                    {
                        edge_log(LOG_ERROR, "TCP Connection failed");
                        net_client_deinit();
                        sock_fd = -1;
                    }
                }
            }
        }
    }

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