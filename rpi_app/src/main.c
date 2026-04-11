#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include "conf.h"
#include "log.h"
#include "crc16.h"
#include "ringbuf.h"
#include "uart.h"
#include "parse.h"


#define MAX_EVENTS 10

#define DEFAULT_CONF_PATH "/etc/edge_gateway/gateway.conf"

uint8_t buf[512];
RingBuf_t ringbuf;
SentinelFrame_t frame = {0};

int main(int argc, char **argv)
{
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
            printf("Usage: %s [-c config_file_path]\n", argv[0]);
            return 0;
        default:
            fprintf(stderr, "Usage: %s[-c config_file_path]\n", argv[0]);
            return -1;
        }
    }

    /* ----- 各模块初始化 ----- */

    GatewayConfig_t config = {0};
    if (load_config(conf_path, &config) != 0)
    {
        // 如果读取失败，程序必须退出，不能带着错误的配置跑
        fprintf(stderr, "Fatal: Failed to load config from %s\n", conf_path);
        return -1;
    }

    if (log_init(config.log_file) == 1)
        return 1;

    edge_log(LOG_INFO, "Gateway started! Baudrate: %d", config.baudrate);

    RingBuf_init(&ringbuf, buf, sizeof(buf));
    int uart_fd = uart_init(UART_DEVICE, 115200);

    /* ----- epoll初始化 ----- */

    int epfd = epoll_create1(0);
    if (epfd == -1)
    {
        // error handling
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
    struct epoll_event ev;
    ev.events = EPOLLIN;  // 监听可读事件
    ev.data.fd = uart_fd; // 绑定是我们关心的 fd
    epoll_ctl(epfd, EPOLL_CTL_ADD, uart_fd, &ev);
    
    // 存放内核返回的就绪事件列表
    struct epoll_event events[MAX_EVENTS];

    struct epoll_event ev_timer;
    ev_timer.events = EPOLLIN;
    ev_timer.data.fd = timer_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev_timer);

    uint8_t rx_temp[128];
    while (1)
    {

        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
        {
            // 被信号中断或其他错误
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
                    edge_log(LOG_INFO, "Timer triggered! Expirations: %lu", expirations);

                    // TODO: check_aht20()...
                }
            }
        }
    }

    log_deinit();

    return 0;
}