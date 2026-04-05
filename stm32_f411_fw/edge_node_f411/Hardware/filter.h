/*------------------------Encoding: UTF-8-----------------------------*/
#ifndef __FILTER_H__
#define __FILTER_H__

/**
 * @file filter.h
 * @brief IMU数据滤波和姿态估计算法库实现
 * @details 实现了多种滤波算法：
 *          - 移动平均滤波器
 *          - 中值滤波器
 *          - 卡尔曼滤波器
 *          - 一阶低通滤波器
 *          以及Madgwick姿态解算算法
 * @author SandOcean
 * @date 2025-07-07
 */

// --- 宏定义 ---
#define BUFFER_SIZE 10       // 定义滤波器（如移动平均、中位值）所用窗口的大小

// --- 数据结构定义 ---

/**
 * @brief 通用三轴滤波器数据结构体
 */
typedef struct
{
    float filter_x; // X轴滤波后的数据
    float filter_y; // Y轴滤波后的数据
    float filter_z; // Z轴滤波后的数据
} filter_Data;

/**
 * @brief 存储所有IMU传感器滤波后数据的结构体
 */
typedef struct
{
    // 加速度计滤波后数据
    float Acc_filter_x;
    float Acc_filter_y;
    float Acc_filter_z;
    // 陀螺仪滤波后数据
    float Gyr_filter_x;
    float Gyr_filter_y;
    float Gyr_filter_z;
    // 磁力计 (高斯计) 滤波后数据
    float Guass_filter_x;
    float Guass_filter_y;
    float Guass_filter_z;
} IMU_filter_Data;

/**
 * @brief 卡尔曼滤波器状态结构体 (适用于三轴独立滤波)
 */
typedef struct
{
    float Q[3];         // 过程噪声协方差 (x, y, z)
    float R[3];         // 测量噪声协方差 (x, y, z)
    float X[3];         // 状态的最优估计值 (x, y, z)
    float P[3];         // 估计误差协方差 (x, y, z)
    float K[3];         // 卡尔曼增益 (x, y, z)
    int initialized;    // 初始化标志位, 0:未初始化, 1:已初始化
} KalmanFilter;

/**
 * @brief 一阶低通滤波器状态结构体 (适用于三轴独立滤波)
 */
typedef struct
{
    float alpha;        // 平滑系数 (0 < alpha < 1)
    float X[3];         // 上一次的滤波输出值 (x, y, z)
    int initialized;    // 初始化标志位, 0:未初始化, 1:已初始化
} LowPassFilter;

/**
 * @brief 姿态数据结构体 (欧拉角)
 */
typedef struct 
{
    float rol; // 横滚角 (Roll), 单位: 度
    float pit; // 俯仰角 (Pitch), 单位: 度
    float yaw; // 偏航角 (Yaw), 单位: 度
} Attitude_t;

// --- 全局变量声明 ---
// 使用 extern 关键字声明全局变量，使其可以在其他文件中被访问
extern IMU_filter_Data I_filtdata; // 存储所有IMU滤波数据的全局实例
extern KalmanFilter    Kf;         // 卡尔曼滤波器全局实例
extern LowPassFilter   Lp;         // 低通滤波器全局实例
extern Attitude_t      Attitude_Data; // 存储最终姿态解算结果的全局实例

// --- 函数原型声明 ---

/**
 * @brief 对三轴数据应用简化的卡尔曼滤波器
 */
void Kalman_Filter(KalmanFilter *kf, float Q[3], float R[3], float initial_value, float x, float y, float z);

/**
 * @brief 对三轴数据应用中位值滤波器
 */
void Median_Filter(float new_samples[3], filter_Data *filtdata);

/**
 * @brief 对三轴数据应用移动平均滤波器
 */
void Moving_Average_Filter(float new_samples[3], filter_Data *filtdata);

/**
 * @brief 对三轴数据应用一阶低通滤波器
 */
void LowPass_Filter(LowPassFilter *lp, float alpha, float x, float y, float z);

/**
 * @brief 初始化Madgwick算法
 */
void Madgwick_Init(void);

/**
 * @brief 使用陀螺仪和加速度计数据更新姿态 (Madgwick算法核心)
 */
void Madgwick_Update(float gx, float gy, float gz, float ax, float ay, float az);

#endif // __FILTER_H__
