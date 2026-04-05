/*------------------------Encoding: UTF-8-----------------------------*/
#ifndef __ICM42688_H
#define __ICM42688_H
#include "main.h"

#define ICM_DEVICE_CONFIG_1				0x11  // 设备配置1
#define ICM_DEVICE_CONFIG_2				0x13  // 设备配置2
#define ICM_FIFO_CONFIG              	0x16  // 配置FIFO
#define ICM_TEMP_DATA1            		0x1D  // 温度数据

#define ICM_ACCEL_DATA_X1         		0x1F  // 加速度计X轴数据
#define ICM_FIFO_DATA             		0x30  // FIFO数据
#define ICM_INTF_CONFIG0          		0x4C  // 接口配置0
#define ICM_INTF_CONFIG1          		0x4D  // 接口配置1

#define ICM_PWR_MGMT0					0x4E  // 电源管理0
#define ICM_GYRO_CONFIG0				0x4F  // 陀螺仪配置0
#define ICM_ACCEL_CONFIG0				0x50  // 加速度计配置0
#define ICM_GYRO_CONFIG1				0x51  // 陀螺仪配置1
#define ICM_GYRO_ACCEL_CONFIG0			0x52  // 陀螺仪和加速度计配置0
#define ICM_ACCEL_CONFIG1	        	0x53  // 加速度计配置1
#define ICM_FIFO_CONFIG1          		0x5F  // FIFO配置1

#define ICM_ID                    		0x47  // 设备ID

#define ICM_SOFT_RESET					0x01  // 写1后，需等待1ms软复位生效
#define ACCEL_GYRO_MODE					0x0F  // 陀螺仪和加速度计工作在低噪声(LN)模式 
//这里只定义了两个量程（均为满量程） 因为陀螺仪在FIFO模式下，强制设置为满量程 所以不用管
#define GYRO_FS_SEL_ODR					0x06  // 陀螺仪满量程: ±2000dps, 输出速率: 1kHz
#define ACCEL_FS_SEL_ODR				0x06  // 加速度计满量程: ±16g, 输出速率: 1kHz
#define GYRO_CONFIG1_Param        		0x16  // 带宽：DLPF带宽=4000Hz；DLPF延迟=0.125ms
                                        	  // 陀螺仪和加速度计配置0：UI滤波器：二阶DEC2_M2滤波器，三阶滤波
#define GYRO_ACCEL_CONFIG0_Param  		0x11  // 加速度计带宽：BW=max(400Hz, ODR)/4；陀螺仪带宽：BW=max(400Hz, ODR)/4
#define ACCEL_CONFIG1_Param       		0x0D  // UI滤波器：二阶DEC2_M2滤波器，三阶滤波

#define FIFO_EN							0x07  // 使能温度、陀螺仪和加速度计FIFO
#define Stream_FIFO               		0x40  // FIFO流模式

#define ICM_DEVICE_ID 					0x75  // 设备ID寄存器地址
#define ICM_BUFFER_LEN					255   // 缓冲区长度

#define ICM_CS(state) do { \
    HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, (state) ? GPIO_PIN_SET : GPIO_PIN_RESET); \
} while(0)

#define g			      				9.80665f		      	     // m/s^2	
#define deg_to_rad  					0.0174533f				     // 角度转弧度

typedef struct 
{
	int16_t temp;
    int16_t gyro_x, gyro_y, gyro_z;
    int16_t accel_x, accel_y, accel_z;
} icm42688_data_t;

typedef struct
{
	uint8_t fifo_buffer[256];
	int8_t fifo_header;
	int16_t accel_x, accel_y, accel_z;
	int16_t gyro_x, gyro_y, gyro_z;
	uint16_t timeStamp;
	int8_t temp;
} icm42688_FIFO_data_t;

typedef struct
{
	float Temp;
	float Acc_x;
	float Acc_y;
	float Acc_z;
	float Gyr_x;
	float Gyr_y;
	float Gyr_z;
} icm42688_Handle_Data;

/* 函数声明 */
uint8_t ICM_ReadID(void);
uint32_t icm42xxx_spi_master_read_register(uint8_t reg_addr, uint8_t* pData, uint8_t len);
uint8_t ICM_Init(void);
void ICM_ReadData(icm42688_data_t *data);
void ICM_ReadTempData(icm42688_data_t *data);
void ICM_ReadFIFOData(icm42688_FIFO_data_t *data);
void IMU_Data(float *filtered_rol, float *filtered_pit, float *filtered_yaw);
void Gyro_Calibration(void); // 统一校准函数声明



#endif
