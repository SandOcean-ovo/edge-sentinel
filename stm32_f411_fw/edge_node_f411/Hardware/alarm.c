/*------------------------Encoding: UTF-8-----------------------------*/
#include "alarm.h"

/**
 * @brief 初始化报警模块
 */
void Alarm_Init(void)
{
    // 确保中断引脚初始为低电平
    HAL_GPIO_WritePin(MCU_ALARM_OUT_GPIO_Port, MCU_ALARM_OUT_Pin, GPIO_PIN_RESET);
}

/**
 * @brief 检测按键是否被按下
 * @retval 1=按下, 0=未按下
 */
uint8_t Alarm_CheckButton(void)
{
    // 按键低电平有效
    return (HAL_GPIO_ReadPin(USER_BTN_GPIO_Port, USER_BTN_Pin) == GPIO_PIN_RESET) ? 1 : 0;
}

/**
 * @brief 设置中断引脚状态
 * @param state 1=拉高, 0=拉低
 */
void Alarm_SetIRQ(uint8_t state)
{
    HAL_GPIO_WritePin(MCU_ALARM_OUT_GPIO_Port, MCU_ALARM_OUT_Pin,
                      state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief 触发中断信号（仅拉高引脚，需要调用者自行延时后拉低）
 */
void Alarm_TriggerIRQ(void)
{
    Alarm_SetIRQ(1);  // 拉高
}
