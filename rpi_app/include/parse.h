#ifndef EDGE_PARSE_H
#define EDGE_PARSE_H

#include <stdbool.h>
#include "ringbuf.h"
#include "crc16.h"
#include "log.h"
#include "db.h"
#include "protocol_utils.h"

#define MIN_PACKAGE_SIZE 6

// 1. IMU 数据结构
typedef struct __attribute__((packed)) IMUData_t
{
    float accel_peak;  // 加速度峰值 (g)
    float accel_rms;   // 加速度RMS (g)
    float gyro_mean_x; // 角速度X均值 (deg/s)
    float gyro_mean_y; // 角速度Y均值 (deg/s)
    float gyro_mean_z; // 角速度Z均值 (deg/s)
} IMUData_t;

// 2. 告警数据结构
typedef struct __attribute__((packed)) AlarmData_t
{
    uint8_t alarm_type;
    uint32_t timestamp;
} AlarmData_t;

// 3. 统一的协议对象
typedef struct SentinelFrame_t
{
    uint8_t type;
    uint8_t len; // 纯载荷长度
    union
    {
        IMUData_t imu;
        AlarmData_t alarm;
        uint8_t raw_payload[256]; // 原始数据备份
    } data;
} SentinelFrame_t;

bool protocol_parse(RingBuf_t *pbuf, SentinelFrame_t *pframe);

void handle_frame(SentinelFrame_t *pframe);

#endif