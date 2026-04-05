/*------------------------Encoding: UTF-8-----------------------------*/
#ifndef __VIBRATION_MONITOR_H
#define __VIBRATION_MONITOR_H

#include "main.h"
#include <stdint.h>

/* 采样参数 */
#define SAMPLE_RATE_HZ      100     // 采样率 100Hz
#define SAMPLE_INTERVAL_MS  10      // 采样间隔 10ms
#define SAMPLES_PER_SECOND  100     // 每秒采样数

/* 阈值定义 */
#define ACCEL_PEAK_THRESHOLD   1.5f   // 加速度峰值阈值 (g)
#define ACCEL_RMS_THRESHOLD    0.3f   // 加速度RMS阈值 (g)
#define GYRO_MEAN_THRESHOLD    30.0f  // 角速度均值阈值 (deg/s)

/* 数据缓冲区结构 */
typedef struct {
    float accel_x[SAMPLES_PER_SECOND];
    float accel_y[SAMPLES_PER_SECOND];
    float accel_z[SAMPLES_PER_SECOND];
    float gyro_x[SAMPLES_PER_SECOND];
    float gyro_y[SAMPLES_PER_SECOND];
    float gyro_z[SAMPLES_PER_SECOND];
    uint16_t index;  // 当前索引
} VibMonitor_Buffer_t;

/* 统计结果结构 */
typedef struct {
    float accel_peak;      // 加速度峰值 (g)
    float accel_rms;       // 加速度RMS (g)
    float gyro_mean_x;     // 角速度X轴均值 (deg/s)
    float gyro_mean_y;     // 角速度Y轴均值 (deg/s)
    float gyro_mean_z;     // 角速度Z轴均值 (deg/s)
    uint8_t alarm_flag;    // 报警标志位
} VibMonitor_Statistics_t;

/* 函数声明 */
void VibMonitor_Init(void);
void VibMonitor_AddSample(float ax, float ay, float az, float gx, float gy, float gz);
void VibMonitor_Calculate(VibMonitor_Statistics_t *stats);
void VibMonitor_Reset(void);

#endif
