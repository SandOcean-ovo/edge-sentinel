#include <unistd.h>
#include "conf.h"
#include "log.h"
#include "crc16.h"
#include "ringbuf.h"

#define DEFAULT_CONF_PATH "/etc/edge_gateway/gateway.conf"

int main(int argc, char **argv)
{
    int opt;
    // 默认指向绝对路径
    char *conf_path = DEFAULT_CONF_PATH; 

    // 解析命令行参数，"c:" 表示 -c 后面必须跟一个参数
    while ((opt = getopt(argc, argv, "c:h")) != -1) {
        switch (opt) {
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

    printf("Starting Edge Gateway...\n");
    printf("Using config file: %s\n", conf_path);

    
    GatewayConfig_t config = {0};
    if (load_config(conf_path, &config) != 0) {
        // 如果读取失败，程序必须退出，不能带着错误的配置跑
        fprintf(stderr, "Fatal: Failed to load config from %s\n", conf_path);
        return -1;
    }

    if(log_init(config.log_file) == 1) return 1;

    edge_log(LOG_INFO, "Gateway started! Baudrate: %d", config.baudrate);

    log_deinit();


    return 0;
}