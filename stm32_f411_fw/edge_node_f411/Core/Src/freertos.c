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

osSemaphoreId_t imuDataReadySemHandle;       // IMU数据就绪信号量
volatile uint8_t imu_data_available = 0;     // 1秒内是否收到过IMU数据

// Alarm任务相关（放在USER CODE区避免CubeMX覆盖）
osThreadId_t Task_AlarmHandle;
const osThreadAttr_t Task_Alarm_attributes = {
  .name = "Task_Alarm",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};

// 消息队列
osMessageQueueId_t AlarmQueueHandle;
const osMessageQueueAttr_t AlarmQueue_attributes = { .name = "AlarmQueue" };
osMessageQueueId_t RxDataQueueHandle;
const osMessageQueueAttr_t RxDataQueue_attributes = { .name = "RxDataQueue" };
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
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for Task_Protocol */
osThreadId_t Task_ProtocolHandle;
const osThreadAttr_t Task_Protocol_attributes = {
  .name = "Task_Protocol",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void My_print(const char* format, ...);
void StartTaskAlarm(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTaskIMU(void *argument);
void StartTaskProtocol(void *argument);

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
  imuDataReadySemHandle = osSemaphoreNew(1, 0, NULL);
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
    // 先清标志，再等1秒，看这1秒内IMU有没有发过特征包
    imu_data_available = 0;
    osDelay(1000);
    HAL_GPIO_TogglePin(MCU_LED_GPIO_Port, MCU_LED_Pin);

    if (!imu_data_available)
    {
      // 这1秒内没收到IMU数据，发空心跳包兜底
      Protocol_SendHeartbeat();
    }
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
    // 等待IMU数据就绪信号量（超时50ms）
    if (osSemaphoreAcquire(imuDataReadySemHandle, 50) == osOK)
    {
      // 读取 IMU 数据
      ICM_ReadTempData(&imu_data);

      // SPI通信成功，标记IMU链路正常
      imu_data_available = 1;

      // 转换为物理单位
      float ax = imu_data.accel_x / 2048.0f;  // ±16g 量程
      float ay = imu_data.accel_y / 2048.0f;
      float az = imu_data.accel_z / 2048.0f;
      float gx = (imu_data.gyro_x / 16.4f) - (gyro_bias_x / 0.0174533f);
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
                                stats.gyro_mean_x, stats.gyro_mean_y, stats.gyro_mean_z);

        // 如果有阈值超限，发送告警到队列（位掩码，上层解析）
        if (stats.alarm_flag)
        {
          AlarmEvent_t alarm_event;
          alarm_event.alarm_type = stats.alarm_flag << 1;
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
    }
    // 超时则不做处理，等待下一次信号量
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

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void StartTaskAlarm(void *argument)
{
  AlarmEvent_t alarm_event;
  uint32_t irq_off_tick = 0;  // 告警引脚自动拉低的时刻

  for(;;)
  {
    // 自动拉低告警引脚（非阻塞）
    if (irq_off_tick && HAL_GetTick() >= irq_off_tick)
    {
      Alarm_SetIRQ(0);
      irq_off_tick = 0;
    }

    // 检查按键报警
    if (Alarm_CheckButton())
    {
      osDelay(20);  // 消抖
      if (Alarm_CheckButton())
      {
        Alarm_SetIRQ(1);
        irq_off_tick = HAL_GetTick() + ALARM_IRQ_DURATION_MS;
        Protocol_SendAlarm(ALARM_TYPE_BUTTON);
        // 等待按键释放
        while (Alarm_CheckButton()) { osDelay(10); }
      }
    }

    // 检查IMU超限告警（非阻塞，超时10ms）
    if (osMessageQueueGet(AlarmQueueHandle, &alarm_event, NULL, 10) == osOK)
    {
      Alarm_SetIRQ(1);
      irq_off_tick = HAL_GetTick() + ALARM_IRQ_DURATION_MS;
      Protocol_SendAlarm(alarm_event.alarm_type);
    }
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == IMU_INT_Pin)
  {
    osSemaphoreRelease(imuDataReadySemHandle);
  }
}
/* USER CODE END Application */

