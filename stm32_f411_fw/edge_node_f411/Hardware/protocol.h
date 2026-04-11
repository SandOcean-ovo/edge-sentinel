/*------------------------Encoding: UTF-8-----------------------------*/
#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include "main.h"
#include <stdint.h>

/* 协议常量定义 */
#define PROTOCOL_SOF_0          0xAA
#define PROTOCOL_SOF_1          0x55
#define PROTOCOL_HEADER_SIZE    4     // SOF(2) + Len(1) + Type(1)
#define PROTOCOL_CRC_SIZE       2
#define PROTOCOL_MAX_PAYLOAD    128   // 最大载荷长度

/* 消息类型定义 */
#define MSG_TYPE_HEARTBEAT      0x01  // 心跳
#define MSG_TYPE_IMU_FEATURE    0x02  // IMU特征
#define MSG_TYPE_ENVIRONMENT    0x03  // 环境
#define MSG_TYPE_ALARM          0x04  // 告警
#define MSG_TYPE_CONFIG_DOWN    0x10  // 配置下发
#define MSG_TYPE_CONFIG_ACK     0x11  // 配置ACK
#define MSG_TYPE_STATUS_QUERY   0x12  // 状态查询

/* 协议帧结构 */
typedef struct {
    uint8_t sof[2];                           // 帧头 0xAA 0x55
    uint8_t len;                              // 长度 (Payload + CRC)
    uint8_t type;                             // 类型
    uint8_t payload[PROTOCOL_MAX_PAYLOAD];    // 载荷
    uint16_t crc16;                           // CRC16校验
} __attribute__((packed)) Protocol_Frame_t;

/* IMU特征数据结构 */
typedef struct {
    float accel_peak;      // 加速度峰值 (g)
    float accel_rms;       // 加速度RMS (g)
    float gyro_mean_x;     // 角速度X均值 (deg/s)
    float gyro_mean_y;     // 角速度Y均值 (deg/s)
    float gyro_mean_z;     // 角速度Z均值 (deg/s)
} __attribute__((packed)) IMU_Feature_Payload_t;

/* 告警数据结构 */
typedef struct {
    uint8_t alarm_type;    // 报警类型: 0x01=按键, 0x02=峰值, 0x04=RMS, 0x08=角速度
    uint32_t timestamp;    // 时间戳 (ms)
} __attribute__((packed)) Alarm_Payload_t;

/* 函数声明 */
void Protocol_Init(void);
uint16_t Protocol_CalcCRC16(uint8_t *data, uint16_t len);
uint16_t Protocol_PackFrame(uint8_t type, uint8_t *payload, uint8_t payload_len, uint8_t *out_buf);
void Protocol_SendHeartbeat(void);
void Protocol_SendIMUFeature(float peak, float rms, float gx, float gy, float gz);
void Protocol_SendAlarm(uint8_t alarm_type);
int8_t Protocol_ParseFrame(uint8_t *data, uint16_t len, Protocol_Frame_t *frame);
void Protocol_UART_IdleCallback(void);

#endif
