/*------------------------Encoding: UTF-8-----------------------------*/
#ifndef __ALARM_H
#define __ALARM_H

#include "main.h"
#include <stdint.h>

/* 报警类型定义 */
#define ALARM_TYPE_BUTTON       0x01  // 按键报警
#define ALARM_TYPE_PEAK         0x02  // 加速度峰值超限
#define ALARM_TYPE_RMS          0x04  // 加速度RMS超限
#define ALARM_TYPE_GYRO         0x08  // 角速度超限

/* 中断引脚参数 */
#define ALARM_IRQ_DURATION_MS   100   // 中断信号持续时间 (ms)

/* 函数声明 */
void Alarm_Init(void);
uint8_t Alarm_CheckButton(void);
void Alarm_TriggerIRQ(void);
void Alarm_SetIRQ(uint8_t state);

#endif
