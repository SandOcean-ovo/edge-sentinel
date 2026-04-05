/*------------------------Encoding: UTF-8-----------------------------*/
#include "vibration_monitor.h"
#include <math.h>
#include <string.h>

/* 私有变量 */
static VibMonitor_Buffer_t buffer;

/**
 * @brief 初始化振动监测模块
 */
void VibMonitor_Init(void)
{
    memset(&buffer, 0, sizeof(VibMonitor_Buffer_t));
    buffer.index = 0;
}

/**
 * @brief 添加一个采样数据
 * @param ax 加速度X轴 (g)
 * @param ay 加速度Y轴 (g)
 * @param az 加速度Z轴 (g)
 * @param gx 角速度X轴 (deg/s)
 * @param gy 角速度Y轴 (deg/s)
 * @param gz 角速度Z轴 (deg/s)
 */
void VibMonitor_AddSample(float ax, float ay, float az, float gx, float gy, float gz)
{
    if (buffer.index < SAMPLES_PER_SECOND)
    {
        buffer.accel_x[buffer.index] = ax;
        buffer.accel_y[buffer.index] = ay;
        buffer.accel_z[buffer.index] = az;
        buffer.gyro_x[buffer.index] = gx;
        buffer.gyro_y[buffer.index] = gy;
        buffer.gyro_z[buffer.index] = gz;
        buffer.index++;
    }
}

/**
 * @brief 计算统计数据
 * @param stats 统计结果结构体指针
 */
void VibMonitor_Calculate(VibMonitor_Statistics_t *stats)
{
    if (stats == NULL || buffer.index == 0) return;

    float sum_ax = 0.0f, sum_ay = 0.0f, sum_az = 0.0f;
    float sum_gx = 0.0f, sum_gy = 0.0f, sum_gz = 0.0f;

    // 第一遍：计算均值（用于去除直流分量，包括重力）
    for (uint16_t i = 0; i < buffer.index; i++)
    {
        sum_ax += buffer.accel_x[i];
        sum_ay += buffer.accel_y[i];
        sum_az += buffer.accel_z[i];
        sum_gx += buffer.gyro_x[i];
        sum_gy += buffer.gyro_y[i];
        sum_gz += buffer.gyro_z[i];
    }

    float mean_ax = sum_ax / buffer.index;
    float mean_ay = sum_ay / buffer.index;
    float mean_az = sum_az / buffer.index;

    // 第二遍：计算去除直流分量后的峰值和RMS
    float sum_square = 0.0f;
    float peak = 0.0f;

    for (uint16_t i = 0; i < buffer.index; i++)
    {
        // 去除直流分量（包括重力）
        float ac_x = buffer.accel_x[i] - mean_ax;
        float ac_y = buffer.accel_y[i] - mean_ay;
        float ac_z = buffer.accel_z[i] - mean_az;

        // 计算振动幅度（AC分量的合成）
        float vibration_magnitude = sqrtf(ac_x * ac_x + ac_y * ac_y + ac_z * ac_z);

        // 更新峰值
        if (vibration_magnitude > peak)
        {
            peak = vibration_magnitude;
        }

        // 累加平方和（用于RMS计算）
        sum_square += vibration_magnitude * vibration_magnitude;
    }

    // 计算结果
    stats->accel_peak = peak;
    stats->accel_rms = sqrtf(sum_square / buffer.index);
    stats->gyro_mean_x = sum_gx / buffer.index;
    stats->gyro_mean_y = sum_gy / buffer.index;
    stats->gyro_mean_z = sum_gz / buffer.index;

    // 判断是否超阈值
    stats->alarm_flag = 0;
    if (stats->accel_peak > ACCEL_PEAK_THRESHOLD)
    {
        stats->alarm_flag |= 0x01;  // bit0: 峰值超限
    }
    if (stats->accel_rms > ACCEL_RMS_THRESHOLD)
    {
        stats->alarm_flag |= 0x02;  // bit1: RMS超限
    }
    if (fabsf(stats->gyro_mean_x) > GYRO_MEAN_THRESHOLD ||
        fabsf(stats->gyro_mean_y) > GYRO_MEAN_THRESHOLD ||
        fabsf(stats->gyro_mean_z) > GYRO_MEAN_THRESHOLD)
    {
        stats->alarm_flag |= 0x04;  // bit2: 角速度超限
    }
}

/**
 * @brief 重置缓冲区
 */
void VibMonitor_Reset(void)
{
    buffer.index = 0;
}
