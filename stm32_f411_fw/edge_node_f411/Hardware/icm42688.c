/*------------------------Encoding: UTF-8-----------------------------*/
#include "icm42688.h"
#include "spi.h"
#include "stdlib.h"
#include "filter.h"
#include "stdio.h"
#include "stdint.h"
#include "stm32f4xx_hal.h"
#include <string.h>  // 添加string.h头文件，提供memcpy和memset函数

void My_print(const char* format, ...);

icm42688_data_t sensor_data;
icm42688_Handle_Data Handle_Data;

// 陀螺仪零偏变量，将在启动时通过自动校准进行赋值
volatile float gyro_bias_x = 0.0f;
volatile float gyro_bias_y = 0.0f;
volatile float gyro_bias_z = 0.0f;

volatile uint8_t gyro_calibrated = 0; // 标记校准是否完成

/**
 * @brief 自动校准陀螺仪零偏
 * @details 该函数通过采集一段时间的静态数据，计算陀螺仪的零点偏移，
 *          并直接更新模块内的`gyro_bias_*`变量。
 */
void Gyro_Calibration(void)
{
    float sum_x = 0.0f, sum_y = 0.0f, sum_z = 0.0f;
    const int calibration_samples = 1000;  // 校准采样次数
    
    My_print("Gyroscope calibration started...\r\n");
    HAL_Delay(50);

    for (volatile int i = 0; i < calibration_samples; i++)
    {
        ICM_ReadTempData(&sensor_data);
        
        // 累加原始陀螺仪数据
        sum_x += sensor_data.gyro_x / 16.4f * deg_to_rad;
        sum_y += sensor_data.gyro_y / 16.4f * deg_to_rad;
        sum_z += sensor_data.gyro_z / 16.4f * deg_to_rad;
        
        HAL_Delay(5);
    }
    
    // 计算并更新零点偏移
    gyro_bias_x = sum_x / calibration_samples;
    gyro_bias_y = sum_y / calibration_samples;
    gyro_bias_z = sum_z / calibration_samples;
    
    gyro_calibrated = 1; // 标记校准完成

}

/**
 * @brief SPI读写函数，用于与ICM-42688通信
 * @details 该函数实现了对ICM-42688的SPI读写操作：
 *          - 读操作：地址最高位置1，发送地址后接收数据
 *          - 写操作：地址最高位清0，发送地址后发送数据
 *          函数会自动处理片选信号和数据缓冲区
 * @param addr     寄存器地址
 * @param pTxData  发送数据（写操作时有效，单字节）
 * @param pRxData  接收缓冲区指针（读操作时有效）
 * @param dataSize 数据大小（字节数）
 * @retval         操作状态：0=成功，1=失败
 */
uint8_t SPI1_ReadWriteBytes(uint8_t addr, uint8_t pTxData, uint8_t *pRxData, uint16_t dataSize)
{
	uint16_t totalSize = dataSize + 1;
	uint8_t txBuffer[totalSize];
	uint8_t rxBuffer[totalSize];
	
	// 根据发送/接收缓冲区设置地址字节
	if (pRxData) 
	{
			txBuffer[0] = addr | 0x80;  // 读操作：地址最高位置1
			memset(&txBuffer[1], 0xFF, dataSize); // 读操作
	} 
	else 
	{
			txBuffer[0] = addr & 0x7F;  // 写操作：地址最高位清0
			txBuffer[1] = pTxData; // 写数据
	}
	// 执行SPI传输
	ICM_CS(0);
	if (HAL_SPI_TransmitReceive(&hspi1, txBuffer, rxBuffer, totalSize, 0xFF) != HAL_OK)
	{
			return 1;
	}
	ICM_CS(1);
	// 读取操作时，提取有效数据（跳过地址响应字节）
	if (pRxData) {
			memcpy(pRxData, &rxBuffer[1], dataSize);
	}
	return 0;
}
/**
 * @brief 初始化ICM-42688六轴传感器
 * @retval 状态码：0=成功，1=SPI通信失败, 2=id不匹配
 */
uint8_t ICM_Init(void)
{
	uint8_t id=0;
	id  = ICM_ReadID();
	if(id != ICM_ID)
	{
		printf("icm_id: %x\r\n", id);
		return 2;
	}
	uint8_t status;
	// 步骤1：发送软件复位命令（向设备配置寄存器1写入复位值）
  // ICM_SOFT_RESET(0x01)
	status = SPI1_ReadWriteBytes(ICM_DEVICE_CONFIG_1, ICM_SOFT_RESET, NULL, 1);
	HAL_Delay(4);
	// 步骤2：配置电源管理和工作模式（启用加速度计和陀螺仪）
  // ACCEL_GYRO_MODE = 0x0F（低噪声模式，所有轴启用）
  // [3:2]=11（陀螺仪 LN MODE），[1:0]=11（加速度计 LN MODE）
	status = SPI1_ReadWriteBytes(ICM_PWR_MGMT0, ACCEL_GYRO_MODE, NULL, 1);
	HAL_Delay(100);
	// 配置加速度计: ±16g, 100Hz ODR (FS=011, ODR=1000)
	status = SPI1_ReadWriteBytes(ICM_ACCEL_CONFIG0, 0x68, NULL, 1);
	HAL_Delay(1);
	// 配置陀螺仪: ±2000dps, 100Hz ODR (FS=000, ODR=1000)
	status = SPI1_ReadWriteBytes(ICM_GYRO_CONFIG0, 0x08, NULL, 1);
	HAL_Delay(1);
	if(status != 0) return 1;
	return 0;
}


/**
 * @brief 读取ICM-42688设备ID寄存器值
 * @retval 成功时返回设备ID(正常为0x47)，失败时返回0
 */
uint8_t ICM_ReadID(void)
{
	uint8_t Temp;	 
	// 调用SPI传输函数读取设备ID寄存器(地址为ICM_DEVICE_ID,0x75)
  // 参数: 寄存器地址, 写数据(0表示无写操作), 接收缓冲区, 数据长度(1字节)
  // 返回值: 0表示SPI通信成功, 非0表示失败
	if(SPI1_ReadWriteBytes(ICM_DEVICE_ID,0,&Temp,1))
	{
		return 1;
	}			    
	return Temp;
}

/**
 * @brief 读取ICM-42688的加速度计和陀螺仪原始数据
 * @param data 存储传感器数据的结构体指针
 */
void ICM_ReadData(icm42688_data_t *data)
{
	uint8_t buf[12];
	// 从加速度计X轴高位寄存器开始，连续读取12字节数据
  // 地址自动递增模式：ICM_ACCEL_DATA_X1 (0x1F) → ICM_GYRO_DATA_Z0 (0x2A)
	SPI1_ReadWriteBytes(ICM_ACCEL_DATA_X1, 0, buf, 12);
	// 解析加速度计数据（高字节在前，16位有符号整数）
  data->accel_x = (buf[0] << 8) | buf[1];
  data->accel_y = (buf[2] << 8) | buf[3];
  data->accel_z = (buf[4] << 8) | buf[5];

  // 解析陀螺仪数据（高字节在前，16位有符号整数）
  data->gyro_x = (buf[6] << 8) | buf[7];
  data->gyro_y = (buf[8] << 8) | buf[9];
  data->gyro_z = (buf[10] << 8) | buf[11];
}

/**
 * @brief 读取ICM-42688的加速度计和陀螺仪原始数据
 * @param data 存储传感器数据的结构体指针
 */
void ICM_ReadTempData(icm42688_data_t *data)
{
	uint8_t buf[14];
	// 从加速度计X轴高位寄存器开始，连续读取12字节数据
  // 地址自动递增模式：ICM_TEMP_DATA1 (0x1D) → ICM_GYRO_DATA_Z0 (0x2A)
	SPI1_ReadWriteBytes(ICM_TEMP_DATA1, 0, buf, 14);
	data->temp = (buf[0] << 8) | buf[1];
	// 解析加速度计数据（高字节在前，16位有符号整数）
  data->accel_x = (buf[2] << 8) | buf[3];
  data->accel_y = (buf[4] << 8) | buf[5];
  data->accel_z = (buf[6] << 8) | buf[7];

  // 解析陀螺仪数据（高字节在前，16位有符号整数）
  data->gyro_x = (buf[8] << 8) | buf[9];
  data->gyro_y = (buf[10] << 8) | buf[11];
  data->gyro_z = (buf[12] << 8) | buf[13];
}

/**
 * @brief 从ICM-42688传感器的FIFO缓冲区读取并解析数据
 * @param data 指向icm42688_FIFO_data_t结构体的指针，用于存储解析后的数据
 */
void ICM_ReadFIFOData(icm42688_FIFO_data_t *data)
{
	  uint16_t data_index = 0;
    // 检查输入参数
    if (data == NULL) return;
    // 读取FIFO数据
    if (SPI1_ReadWriteBytes(ICM_FIFO_DATA, 0, data->fifo_buffer, 128) != 0)
		{
        printf("SPI read failed!\n");
    }
    // 解析FIFO数据 - 根据ICM-42688的数据手册格式
    // 数据格式为: Header(1byte) + Accel(6bytes) + Gyro(6bytes) + Temp(1byte) + Timestamp(2bytes)
    while(data_index < 128)
		{
        // 解析头部
        data->fifo_header = data->fifo_buffer[data_index];
				data_index += 1;
        // 解析加速度数据 (16位有符号整数，高字节在前)
        data->accel_x = (data->fifo_buffer[data_index] << 8) | data->fifo_buffer[data_index + 1];
        data->accel_y = (data->fifo_buffer[data_index + 2] << 8) | data->fifo_buffer[data_index + 3];
        data->accel_z = (data->fifo_buffer[data_index + 4] << 8) | data->fifo_buffer[data_index + 5];
        data_index += 6;
        // 解析陀螺仪数据
        data->gyro_x = (data->fifo_buffer[data_index] << 8) | data->fifo_buffer[data_index+1];
        data->gyro_y = (data->fifo_buffer[data_index+2] << 8) | data->fifo_buffer[data_index+3];
        data->gyro_z = (data->fifo_buffer[data_index+4] << 8) | data->fifo_buffer[data_index+5];
			  data_index += 6;
        // 解析温度
        data->temp = data->fifo_buffer[data_index];
				data_index += 1;
			// 解析时间戳
        data->timeStamp = (data->fifo_buffer[data_index] << 8) | data->fifo_buffer[data_index+1];
				data_index += 2;
        // 打印解析结果
        printf("Header: 0x%02x", data->fifo_header);
        printf("Acc: X=%d, Y=%d, Z=%d", data->accel_x, data->accel_y, data->accel_z);
        printf("Gyr: X=%d, Y=%d, Z=%d", data->gyro_x, data->gyro_y, data->gyro_z);
        printf("Timestamp: %d", data->timeStamp);
        printf("Temp: %d\r\n", data->temp);
    }
		HAL_Delay(1);
}
	
/**
 * @brief 读取IMU数据并进行姿态解算和滤波
 * @details 该函数完成以下步骤：
 *          1. 读取ICM-42688的原始数据
 *          2. 将原始数据转换为物理单位（弧度/秒、g）
 *          3. 使用Madgwick算法进行姿态解算
 *          4. 对解算结果进行低通滤波以减少抖动
 * @param filtered_rol 指向存储滤波后横滚角的变量的指针
 * @param filtered_pit 指向存储滤波后俯仰角的变量的指针
 * @param filtered_yaw 指向存储滤波后偏航角的变量的指针
 */
void IMU_Data(float *filtered_rol, float *filtered_pit, float *filtered_yaw)
{
    // 读取IMU数据：陀螺仪和加速度计原始数据
    ICM_ReadTempData(&sensor_data);
    
    // ICM-42688数据转换（根据配置的量程进行转换）
    // 陀螺仪：±2000dps量程，灵敏度16.4 LSB/(°/s)，减去零点偏移
    // 直接使用本文件内的 gyro_bias_* 变量
    Handle_Data.Gyr_x = (sensor_data.gyro_x / 16.4f) * deg_to_rad - gyro_bias_x;
    Handle_Data.Gyr_y = (sensor_data.gyro_y / 16.4f) * deg_to_rad - gyro_bias_y;
    Handle_Data.Gyr_z = (sensor_data.gyro_z / 16.4f) * deg_to_rad - gyro_bias_z;
    
    // 加速度计：±16g量程，灵敏度2048 LSB/g
    Handle_Data.Acc_x = sensor_data.accel_x / 2048.0f;
    Handle_Data.Acc_y = sensor_data.accel_y / 2048.0f;
    Handle_Data.Acc_z = sensor_data.accel_z / 2048.0f;

    // 调用Madgwick算法进行姿态解算
    Madgwick_Update(Handle_Data.Gyr_x, Handle_Data.Gyr_y, Handle_Data.Gyr_z, 
                    Handle_Data.Acc_x, Handle_Data.Acc_y, Handle_Data.Acc_z);
    
    // 滤波会影响掉头时的数据，所以直接输出姿态解算结果
    *filtered_rol = Attitude_Data.rol;
    *filtered_pit = Attitude_Data.pit;
    *filtered_yaw = Attitude_Data.yaw;
    /* 一阶低通滤波示例，alpha取0.1~0.3之间合适 如果你的项目中没有掉头，可以取消注释以打开
    实测没有低通滤波时，数据也比较稳定
    static float last_rol = 0, last_pit = 0, last_yaw = 0;
    float alpha = 0.1f;
    *filtered_rol = alpha * Attitude_Data.rol + (1 - alpha) * last_rol;
    *filtered_pit = alpha * Attitude_Data.pit + (1 - alpha) * last_pit;
    *filtered_yaw = alpha * Attitude_Data.yaw + (1 - alpha) * last_yaw;
    last_rol = *filtered_rol;
    last_pit = *filtered_pit;
    last_yaw = *filtered_yaw;
    */
}

/**
 * @brief 使能ICM42688 INT1引脚的Data Ready中断
 * @details 配置INT1为推挽、高电平有效、脉冲模式，
 *          并使能Data Ready中断输出到INT1
 */
void ICM_EnableDataReadyInt(void)
{
    // INT1: 推挽输出, 高电平有效, 脉冲模式
    SPI1_ReadWriteBytes(ICM_INT_CONFIG, 0x02, NULL, 1);
    HAL_Delay(1);
    // 使能 Data Ready 中断到 INT1
    SPI1_ReadWriteBytes(ICM_INT_SOURCE0, 0x08, NULL, 1);
    HAL_Delay(1);
}

