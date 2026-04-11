#include <unistd.h>
#include "conf.h"
#include "log.h"
#include "crc16.h"
#include "ringbuf.h"
#include "uart.h"
#include "parse.h"

#define DEFAULT_CONF_PATH "/etc/edge_gateway/gateway.conf"

uint8_t buf[256];
RingBuf_t ringbuf;
SentinelFrame_t frame = {0};

int main(int argc, char **argv)
{
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
    int uart_dev = uart_init(UART_DEVICE, 115200);

    uint8_t rx_temp[128];
    while (1)
    {
        ssize_t n = read(uart_dev, rx_temp, sizeof(rx_temp));
        if (n > 0)
        {
            RingBuf_writeblocks(&ringbuf, rx_temp, n);
        }

        if (protocol_parse(&ringbuf, &frame) == true)
        {
            handle_frame(&frame);
        }
    }

    log_deinit();

    return 0;
}