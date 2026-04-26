#ifndef EDGE_CONF_H
#define EDGE_CONF_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define STR_MAX_LEN 64

typedef struct GatewayConfig_t
{
    char device_id[STR_MAX_LEN]; /* 设备id */
    char uart_dev[STR_MAX_LEN];  /* 串口挂载 */
    int baudrate;                /* 串口波特率 */
    float temp_threshold;        /* 温度阈值 */
    float accel_peak_threshold;  /* 加速度峰值阈值 (g) */
    float accel_rms_threshold;   /* 加速度 RMS 阈值 (g) */
    float gyro_threshold;        /* 角速度阈值 (deg/s) */
    char log_file[STR_MAX_LEN];  /* 日志路径 */
    char db_path[STR_MAX_LEN];   /* 数据库路径 */
    char ip[STR_MAX_LEN];
    int port;
} GatewayConfig_t;    

/**
 * @brief 读取配置文件的内容，并赋值给结构体 
 */
int load_config(const char* file_path, GatewayConfig_t* config);



#endif /* EDGE_CONF_H */