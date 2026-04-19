/*------------------------Encoding: UTF-8-----------------------------*/
#include "protocol.h"
#include "usart.h"
#include "cmsis_os.h"
#include <string.h>

/* 外部变量 */
extern UART_HandleTypeDef huart1;
extern osMessageQueueId_t RxDataQueueHandle;

/* DMA接收缓冲区 */
#define UART_RX_DMA_SIZE  256
static uint8_t uart_rx_dma_buf[UART_RX_DMA_SIZE];

/* 接收数据队列消息结构（需与freertos.c中定义一致） */
typedef struct {
    uint8_t data[256];
    uint16_t len;
} RxDataMsg_t;

/**
 * @brief 初始化协议模块
 */
void Protocol_Init(void)
{
    // 启用串口空闲中断
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);

    // 启动DMA接收
    HAL_UART_Receive_DMA(&huart1, uart_rx_dma_buf, UART_RX_DMA_SIZE);
}

/**
 * @brief 计算CRC16校验值 (CRC-16/XMODEM)
 * @param data 数据指针
 * @param len 数据长度
 * @retval CRC16值
 */
uint16_t Protocol_CalcCRC16(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0x0000;

    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
        }
    }

    return crc;
}

/**
 * @brief 打包协议帧
 * @param type 消息类型
 * @param payload 载荷数据指针
 * @param payload_len 载荷长度
 * @param out_buf 输出缓冲区
 * @retval 打包后的总长度
 */
uint16_t Protocol_PackFrame(uint8_t type, uint8_t *payload, uint8_t payload_len, uint8_t *out_buf)
{
    uint16_t index = 0;

    // 帧头
    out_buf[index++] = PROTOCOL_SOF_0;
    out_buf[index++] = PROTOCOL_SOF_1;

    // 长度 = Payload + CRC(2)
    out_buf[index++] = payload_len + PROTOCOL_CRC_SIZE;

    // 类型
    out_buf[index++] = type;

    // 载荷
    if (payload_len > 0 && payload != NULL)
    {
        memcpy(&out_buf[index], payload, payload_len);
        index += payload_len;
    }

    // 计算CRC16 (对 SOF + Len + Type + Payload)
    uint16_t crc = Protocol_CalcCRC16(out_buf, index);

    // CRC16 (小端序)
    out_buf[index++] = crc & 0xFF;
    out_buf[index++] = (crc >> 8) & 0xFF;

    return index;
}

/**
 * @brief 等待UART DMA发送完成后再发送
 * @param pData 数据指针
 * @param Size 数据长度
 */
static void Protocol_TransmitDMA(uint8_t *pData, uint16_t Size)
{
    // 等待上一次DMA发送完成
    while (huart1.gState != HAL_UART_STATE_READY) {}
    HAL_UART_Transmit_DMA(&huart1, pData, Size);
}

/**
 * @brief 发送心跳包
 */
void Protocol_SendHeartbeat(void)
{
    static uint8_t tx_buf[PROTOCOL_HEADER_SIZE + PROTOCOL_CRC_SIZE];
    uint16_t len = Protocol_PackFrame(MSG_TYPE_HEARTBEAT, NULL, 0, tx_buf);
    Protocol_TransmitDMA(tx_buf, len);
}

/**
 * @brief 发送IMU特征数据
 * @param peak 加速度峰值
 * @param rms 加速度RMS
 * @param gx 角速度X均值
 * @param gy 角速度Y均值
 * @param gz 角速度Z均值
 */
void Protocol_SendIMUFeature(float peak, float rms, float gx, float gy, float gz)
{
    static uint8_t tx_buf[PROTOCOL_HEADER_SIZE + sizeof(IMU_Feature_Payload_t) + PROTOCOL_CRC_SIZE];
    IMU_Feature_Payload_t payload;
    payload.accel_peak = peak;
    payload.accel_rms = rms;
    payload.gyro_mean_x = gx;
    payload.gyro_mean_y = gy;
    payload.gyro_mean_z = gz;

    uint16_t len = Protocol_PackFrame(MSG_TYPE_IMU_FEATURE, (uint8_t*)&payload, sizeof(payload), tx_buf);
    Protocol_TransmitDMA(tx_buf, len);
}

/**
 * @brief 发送告警消息
 * @param alarm_type 报警类型
 */
void Protocol_SendAlarm(uint8_t alarm_type)
{
    static uint8_t tx_buf[PROTOCOL_HEADER_SIZE + sizeof(Alarm_Payload_t) + PROTOCOL_CRC_SIZE];
    Alarm_Payload_t payload;
    payload.alarm_type = alarm_type;
    payload.timestamp = HAL_GetTick();

    uint16_t len = Protocol_PackFrame(MSG_TYPE_ALARM, (uint8_t*)&payload, sizeof(payload), tx_buf);
    Protocol_TransmitDMA(tx_buf, len);
}

/**
 * @brief 解析协议帧
 * @param data 接收到的数据
 * @param len 数据长度
 * @param frame 解析后的帧结构
 * @retval 0=成功, -1=帧头错误, -2=长度错误, -3=CRC错误
 */
int8_t Protocol_ParseFrame(uint8_t *data, uint16_t len, Protocol_Frame_t *frame)
{
    // 检查最小长度
    if (len < PROTOCOL_HEADER_SIZE + PROTOCOL_CRC_SIZE)
        return -2;

    // 检查帧头
    if (data[0] != PROTOCOL_SOF_0 || data[1] != PROTOCOL_SOF_1)
        return -1;

    // 解析长度和类型
    uint8_t payload_len = data[2] - PROTOCOL_CRC_SIZE;
    frame->len = data[2];
    frame->type = data[3];

    // 检查长度合法性
    if (payload_len > PROTOCOL_MAX_PAYLOAD || len < (PROTOCOL_HEADER_SIZE + payload_len + PROTOCOL_CRC_SIZE))
        return -2;

    // 提取载荷
    if (payload_len > 0)
    {
        memcpy(frame->payload, &data[4], payload_len);
    }

    // 提取CRC
    uint16_t recv_crc = (data[4 + payload_len] << 8) | data[5 + payload_len];

    // 计算CRC
    uint16_t calc_crc = Protocol_CalcCRC16(data, 4 + payload_len);

    // 校验CRC
    if (recv_crc != calc_crc)
        return -3;

    frame->crc16 = recv_crc;
    return 0;
}

/**
 * @brief UART空闲中断回调函数（在stm32f4xx_it.c中调用）
 */
void Protocol_UART_IdleCallback(void)
{
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE))
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart1);

        // 停止DMA接收
        HAL_UART_DMAStop(&huart1);

        // 计算接收到的数据长度
        uint16_t recv_len = UART_RX_DMA_SIZE - __HAL_DMA_GET_COUNTER(huart1.hdmarx);

        if (recv_len > 0)
        {
            // 发送到队列（非阻塞）
            RxDataMsg_t msg;
            msg.len = recv_len;
            memcpy(msg.data, uart_rx_dma_buf, recv_len);
            osMessageQueuePut(RxDataQueueHandle, &msg, 0, 0);
        }

        // 重新启动DMA接收
        HAL_UART_Receive_DMA(&huart1, uart_rx_dma_buf, UART_RX_DMA_SIZE);
    }
}
