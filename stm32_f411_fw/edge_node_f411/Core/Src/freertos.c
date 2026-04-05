/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
#include <stdio.h>
#include "icm42688.h"
#include "vibration_monitor.h"
#include "alarm.h"
#include "protocol.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
// 告警事件结构
typedef struct {
    uint8_t alarm_type;    // 报警类型
    float peak;            // 加速度峰值
    float rms;             // 加速度RMS
    float gyro_x;          // 角速度X
    float gyro_y;          // 角速度Y
    float gyro_z;          // 角速度Z
} AlarmEvent_t;

// 接收数据队列消息结构
typedef struct {
    uint8_t data[256];
    uint16_t len;
} RxDataMsg_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern float gyro_bias_x;
extern float gyro_bias_y;
extern float gyro_bias_z;
/* USER CODE END Variables */
/* Definitions for Task_Heartbeat */
osThreadId_t Task_HeartbeatHandle;
const osThreadAttr_t Task_Heartbeat_attributes = {
  .name = "Task_Heartbeat",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_IMU */
osThreadId_t Task_IMUHandle;
const osThreadAttr_t Task_IMU_attributes = {
  .name = "Task_IMU",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for Task_Protocol */
osThreadId_t Task_ProtocolHandle;
const osThreadAttr_t Task_Protocol_attributes = {
  .name = "Task_Protocol",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for Task_Alarm */
osThreadId_t Task_AlarmHandle;
const osThreadAttr_t Task_Alarm_attributes = {
  .name = "Task_Alarm",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};

/* Definitions for AlarmQueue */
osMessageQueueId_t AlarmQueueHandle;
const osMessageQueueAttr_t AlarmQueue_attributes = {
  .name = "AlarmQueue"
};

/* Definitions for RxDataQueue */
osMessageQueueId_t RxDataQueueHandle;
const osMessageQueueAttr_t RxDataQueue_attributes = {
  .name = "RxDataQueue"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void My_print(const char* format, ...);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTaskIMU(void *argument);
void StartTaskProtocol(void *argument);
void StartTaskAlarm(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  AlarmQueueHandle = osMessageQueueNew(5, sizeof(AlarmEvent_t), &AlarmQueue_attributes);
  RxDataQueueHandle = osMessageQueueNew(5, sizeof(RxDataMsg_t), &RxDataQueue_attributes);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of Task_Heartbeat */
  Task_HeartbeatHandle = osThreadNew(StartDefaultTask, NULL, &Task_Heartbeat_attributes);

  /* creation of Task_IMU */
  Task_IMUHandle = osThreadNew(StartTaskIMU, NULL, &Task_IMU_attributes);

  /* creation of Task_Protocol */
  Task_ProtocolHandle = osThreadNew(StartTaskProtocol, NULL, &Task_Protocol_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* creation of Task_Alarm */
  Task_AlarmHandle = osThreadNew(StartTaskAlarm, NULL, &Task_Alarm_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the Task_Heartbeat thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    HAL_GPIO_TogglePin(MCU_LED_GPIO_Port, MCU_LED_Pin);
    osDelay(1000);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartTaskIMU */
/**
* @brief Function implementing the Task_IMU thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskIMU */
void StartTaskIMU(void *argument)
{
  /* USER CODE BEGIN StartTaskIMU */
  icm42688_data_t imu_data;
  VibMonitor_Statistics_t stats;

  // 初始化振动监测模块
  VibMonitor_Init();

  uint16_t sample_count = 0;

  /* Infinite loop */
  for(;;)
  {
    // 读取 IMU 数据
    ICM_ReadTempData(&imu_data);

    // 转换为物理单位
    float ax = imu_data.accel_x / 2048.0f;  // ±16g 量程
    float ay = imu_data.accel_y / 2048.0f;
    float az = imu_data.accel_z / 2048.0f;
    float gx = (imu_data.gyro_x / 16.4f) - (gyro_bias_x / 0.0174533f);  // ±2000dps 量程，去零漂
    float gy = (imu_data.gyro_y / 16.4f) - (gyro_bias_y / 0.0174533f);
    float gz = (imu_data.gyro_z / 16.4f) - (gyro_bias_z / 0.0174533f);

    // 添加样本
    VibMonitor_AddSample(ax, ay, az, gx, gy, gz);
    sample_count++;

    // 每秒输出一次统计结果
    if (sample_count >= SAMPLES_PER_SECOND)
    {
      VibMonitor_Calculate(&stats);

      // 发送IMU特征数据到树莓派
      Protocol_SendIMUFeature(stats.accel_peak, stats.accel_rms,
                              stats.gyro_mean_x, stats.gyro_mean_y, stats.gyro_mean_z,
                              stats.alarm_flag);

      // 如果有阈值超限，发送到告警队列
      if (stats.alarm_flag)
      {
        AlarmEvent_t alarm_event;
        alarm_event.alarm_type = stats.alarm_flag << 1;  // 0x02=峰值, 0x04=RMS, 0x08=角速度
        alarm_event.peak = stats.accel_peak;
        alarm_event.rms = stats.accel_rms;
        alarm_event.gyro_x = stats.gyro_mean_x;
        alarm_event.gyro_y = stats.gyro_mean_y;
        alarm_event.gyro_z = stats.gyro_mean_z;
        osMessageQueuePut(AlarmQueueHandle, &alarm_event, 0, 0);
      }

      // 重置缓冲区
      VibMonitor_Reset();
      sample_count = 0;
    }

    // 10ms 采样间隔
    osDelay(SAMPLE_INTERVAL_MS);
  }
  /* USER CODE END StartTaskIMU */
}

/* USER CODE BEGIN Header_StartTaskProtocol */
/**
* @brief Function implementing the Task_Protocol thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskProtocol */
void StartTaskProtocol(void *argument)
{
  /* USER CODE BEGIN StartTaskProtocol */
  RxDataMsg_t rx_msg;
  Protocol_Frame_t frame;

  // 初始化协议模块
  Protocol_Init();

  /* Infinite loop */
  for(;;)
  {
    // 等待接收队列消息（阻塞）
    if (osMessageQueueGet(RxDataQueueHandle, &rx_msg, NULL, osWaitForever) == osOK)
    {
      // 解析协议帧
      int8_t ret = Protocol_ParseFrame(rx_msg.data, rx_msg.len, &frame);

      if (ret == 0)
      {
        // 解析成功，根据消息类型处理
        switch (frame.type)
        {
          case MSG_TYPE_CONFIG_DOWN:
            // TODO: 处理配置下发
            // 发送ACK应答
            // Protocol_SendConfigAck();
            break;

          case MSG_TYPE_STATUS_QUERY:
            // TODO: 处理状态查询
            // 发送状态响应
            break;

          default:
            // 未知消息类型，忽略
            break;
        }
      }
      else
      {
        // 解析失败（CRC错误、帧头错误等），忽略
      }
    }
  }
  /* USER CODE END StartTaskProtocol */
}

/* USER CODE BEGIN Header_StartTaskAlarm */
/**
* @brief Function implementing the Task_Alarm thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskAlarm */
void StartTaskAlarm(void *argument)
{
  /* USER CODE BEGIN StartTaskAlarm */
  AlarmEvent_t alarm_event;
  uint8_t button_triggered = 0;
  uint32_t irq_end_tick = 0;  // 中断引脚拉低时刻

  // 初始化报警模块
  Alarm_Init();

  /* Infinite loop */
  for(;;)
  {
    // 检测按键（每20ms检测一次，消抖）
    if (Alarm_CheckButton() && !button_triggered)
    {
      button_triggered = 1;

      // 构造按键报警事件
      alarm_event.alarm_type = ALARM_TYPE_BUTTON;
      alarm_event.peak = 0;
      alarm_event.rms = 0;
      alarm_event.gyro_x = 0;
      alarm_event.gyro_y = 0;
      alarm_event.gyro_z = 0;

      // 拉高中断引脚通知树莓派
      Alarm_TriggerIRQ();
      irq_end_tick = osKernelGetTickCount() + ALARM_IRQ_DURATION_MS;

      // 发送告警消息
      Protocol_SendAlarm(ALARM_TYPE_BUTTON);
    }
    else if (!Alarm_CheckButton())
    {
      button_triggered = 0;
    }

    // 检查告警队列（来自IMU任务的阈值超限）
    if (osMessageQueueGet(AlarmQueueHandle, &alarm_event, NULL, 0) == osOK)
    {
      // 拉高中断引脚通知树莓派
      Alarm_TriggerIRQ();
      irq_end_tick = osKernelGetTickCount() + ALARM_IRQ_DURATION_MS;

      // 发送告警消息
      Protocol_SendAlarm(alarm_event.alarm_type);
    }

    // 检查是否需要拉低中断引脚
    if (irq_end_tick > 0 && osKernelGetTickCount() >= irq_end_tick)
    {
      Alarm_SetIRQ(0);  // 拉低
      irq_end_tick = 0;
    }

    // 20ms 周期
    osDelay(20);
  }
  /* USER CODE END StartTaskAlarm */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

