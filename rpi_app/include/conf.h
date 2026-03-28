#ifndef EDGE_CONF_H
#define EDGE_CONF_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define STR_MAX_LEN 64

typedef struct 
{
    char device_id[STR_MAX_LEN]; /* 设备id */
    char uart_dev[STR_MAX_LEN];  /* 串口挂载 */
    int baudrate;                /* 串口波特率 */
    float temp_threshold;        /* 温度阈值 */
    char log_file[STR_MAX_LEN];  /* 日志路径 */
} GatewayConfig_t;    

/**
 * @brief 读取配置文件的内容，并赋值给结构体 
 */
int load_config(const char* file_path, GatewayConfig_t* config);



#endif /* EDGE_CONF_H */