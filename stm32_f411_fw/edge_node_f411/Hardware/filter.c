/*------------------------Encoding: UTF-8-----------------------------*/
/**
 * @file filter.c
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

#include "filter.h"
#include "stdlib.h"
#include "stddef.h" 
#include "math.h"

// 全局滤波器数据结构实例化
IMU_filter_Data I_filtdata;
KalmanFilter    Kf;
LowPassFilter   Lp;

// 定义全局姿态数据变量
Attitude_t Attitude_Data;

// --- Madgwick算法相关全局变量 ---
#define sampleFreq  100.0f  // IMU更新频率 (Hz)
#define beta        0.03f   // Madgwick算法的梯度下降增益, 较小的值可减少由加速度计引起的漂移

// 全局静态四元数，表示设备的姿态
// q0: 实部, q1-q3: 虚部 (i, j, k)
static float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f; 


/**
 * @brief 比较函数，用于qsort标准库函数.
 * @details 比较两个浮点数的大小。qsort通过此函数指针来确定元素的排序顺序。
 * @param a 指向第一个待比较元素的void指针.
 * @param b 指向第二个待比较元素的void指针.
 * @return int 返回值遵循qsort规范:
 * - 小于0: *a 小于 *b
 * - 等于0: *a 等于 *b
 * - 大于0: *a 大于 *b
 */
int compare(const void* a, const void* b)
{
    // 将void*指针转换为const float*，然后解引用以获取实际值
    float val_a = *(const float*)a;
    float val_b = *(const float*)b;

    if (val_a < val_b) {
        return -1;
    } else if (val_a > val_b) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief 对三轴数据应用移动平均滤波器 (Moving Average Filter).
 * @details 此函数使用一个循环缓冲区来存储最近的N个样本值 (N由BUFFER_SIZE定义)，
 * 并计算它们的平均值作为滤波结果。
 * @param new_samples 包含新采样的三轴数据 (x, y, z) 的浮点数组.
 * @param filtdata 指向filter_Data结构体的指针，用于存储滤波后的结果.
 */
void Moving_Average_Filter(float new_samples[3], filter_Data *filtdata)
{
    static float sum[3] = {0.0f, 0.0f, 0.0f};           // 存储三个轴各自窗口内的样本总和
    static int index[3] = {0};                          // 三个轴独立的循环缓冲区索引
    static float buffer[3][BUFFER_SIZE] = {0};          // 三个轴独立的循环缓冲区
    static float filtered_values[3];                    // 存储滤波后的x, y, z值

    // 对X, Y, Z三个轴分别进行滤波
    for (int i = 0; i < 3; ++i) 
    {
        // 1. 从总和中减去最旧的数据
        sum[i] -= buffer[i][index[i]];
        
        // 2. 将新数据存入缓冲区，覆盖最旧的数据
        buffer[i][index[i]] = new_samples[i];
        
        // 3. 将新数据加入总和
        sum[i] += new_samples[i];
        
        // 4. 更新循环索引
        index[i] = (index[i] + 1) % BUFFER_SIZE;
        
        // 5. 计算平均值
        filtered_values[i] = sum[i] / BUFFER_SIZE;
    }
    
    // 将滤波结果存入输出结构体
    filtdata->filter_x = filtered_values[0];
    filtdata->filter_y = filtered_values[1];
    filtdata->filter_z = filtered_values[2];
}

/**
 * @brief 对三轴数据应用中位值滤波器 (Median Filter).
 * @details 此函数为每个轴维护一个数据窗口。每次调用时，它会将新样本存入窗口，
 * 然后对窗口内所有数据进行排序，并取中位值作为滤波输出。
 * 这种方法能有效去除偶然的脉冲噪声。
 * @param new_samples 包含新采样的三轴数据 (x, y, z) 的浮点数组.
 * @param filtdata 指向filter_Data结构体的指针，用于存储滤波后的结果.
 */
void Median_Filter(float new_samples[3], filter_Data *filtdata)
{
    static int index[3] = {0};                          // 三个轴独立的循环缓冲区索引
    static float buffer[3][BUFFER_SIZE] = {0};          // 三个轴独立的循环缓冲区
    static float sorted_buffer[BUFFER_SIZE];            // 临时缓冲区，用于排序
    static float filtered_values[3];                    // 存储滤波后的x, y, z值

    // 对X, Y, Z三个轴分别进行滤波
    for (int i = 0; i < 3; ++i)
    {
        // 1. 将新数据存入缓冲区
        buffer[i][index[i]] = new_samples[i];
        
        // 2. 更新循环索引
        index[i] = (index[i] + 1) % BUFFER_SIZE;

        // 3. 对当前轴的缓冲区进行排序
        //    为避免修改原始缓冲区顺序，可以复制到临时数组再排序，但这里为了效率直接排序
        qsort(buffer[i], BUFFER_SIZE, sizeof(float), compare);

        // 4. 计算中位值
        if (BUFFER_SIZE % 2 == 0) // 如果窗口大小是偶数
        {
            // 取中间两个值的平均值
            filtered_values[i] = (buffer[i][BUFFER_SIZE / 2 - 1] + buffer[i][BUFFER_SIZE / 2]) / 2.0f;
        }
        else // 如果窗口大小是奇数
        {
            // 直接取中间值
            filtered_values[i] = buffer[i][BUFFER_SIZE / 2];
        }
    }
    
    // 5. 将滤波结果存入输出结构体
    filtdata->filter_x = filtered_values[0];
    filtdata->filter_y = filtered_values[1];
    filtdata->filter_z = filtered_values[2];
}

/**
 * @brief 对三轴数据应用简化的卡尔曼滤波器 (Kalman Filter).
 * @details 此函数为每个轴独立实现一个一维卡尔曼滤波器。
 * 它通过预测和更新两个步骤，融合含噪声的测量值，得到对真实状态的最优估计。
 * 适用于处理高斯白噪声。
 * @param kf 指向KalmanFilter结构体的指针，该结构体存储了滤波器的状态.
 * @param Q  过程噪声协方差 (Process Noise Covariance)，表示模型预测的不确定性.
 * @param R  测量噪声协方差 (Measurement Noise Covariance)，表示传感器测量的不确定性.
 * @param initial_value 状态的初始估计值.
 * @param x, y, z 当前测量的三轴数据.
 */
void Kalman_Filter(KalmanFilter *kf, float Q[3], float R[3], float initial_value, float x, float y, float z)
{
    // 如果滤波器未初始化，则进行首次设置
    if (!kf->initialized) 
    {
        for (int i = 0; i < 3; i++)
        {
            kf->Q[i] = Q[i];        // 设置过程噪声协方差
            kf->R[i] = R[i];        // 设置测量噪声协方差
            kf->X[i] = initial_value; // 设置状态初始估计值
            kf->P[i] = 1.0f;        // 设置初始估计误差协方差
        }
        kf->initialized = 1; // 标记为已初始化
    }

    // 将当前测量值存入数组
    float measurements[3] = {x, y, z};

    // 对X, Y, Z三个轴分别进行滤波
    for (int i = 0; i < 3; i++)
    {
        // --- 预测步骤 (Predict) ---
        // 1. 预测估计误差协方差 P(k|k-1) = P(k-1|k-1) + Q
        kf->P[i] += kf->Q[i];

        // --- 更新步骤 (Update) ---
        // 2. 计算卡尔曼增益 K(k) = P(k|k-1) / (P(k|k-1) + R)
        kf->K[i] = kf->P[i] / (kf->P[i] + kf->R[i]);

        // 3. 更新状态估计值 X(k|k) = X(k|k-1) + K(k) * (Z(k) - X(k|k-1))
        kf->X[i] += kf->K[i] * (measurements[i] - kf->X[i]);

        // 4. 更新估计误差协方差 P(k|k) = (1 - K(k)) * P(k|k-1)
        kf->P[i] *= (1.0f - kf->K[i]);
    }
}

/**
 * @brief 对三轴数据应用一阶低通滤波器 (Low-pass Filter).
 * @details 这是一个指数移动平均滤波器(Exponential Moving Average)，
 * 通过平滑系数alpha来平衡新旧数据在结果中的权重，有效滤除高频噪声。
 * @param lp 指向LowPassFilter结构体的指针，该结构体存储了滤波器的状态.
 * @param alpha 平滑系数 (0 < alpha < 1)。值越小，平滑效果越强，但响应越慢.
 * @param x, y, z 当前测量的三轴数据.
 */
void LowPass_Filter(LowPassFilter *lp, float alpha, float x, float y, float z)
{
    // 如果滤波器未初始化，则进行首次设置
    if (!lp->initialized)
    {
        lp->alpha = alpha; // 设置平滑系数
        lp->X[0] = x;      // 使用第一个采样值作为初始状态
        lp->X[1] = y;
        lp->X[2] = z;
        lp->initialized = 1; // 标记为已初始化
    }
    
    // 应用低通滤波公式: Y(n) = alpha * X(n) + (1-alpha) * Y(n-1)
    lp->X[0] = lp->alpha * x + (1.0f - lp->alpha) * lp->X[0]; // 滤波x轴
    lp->X[1] = lp->alpha * y + (1.0f - lp->alpha) * lp->X[1]; // 滤波y轴
    lp->X[2] = lp->alpha * z + (1.0f - lp->alpha) * lp->X[2]; // 滤波z轴
}

/**
 * @brief 初始化Madgwick算法的四元数.
 * @details 将姿态重置为初始状态（水平，航向为0）。
 * q0=1, q1=q2=q3=0 代表没有旋转。
 */
void Madgwick_Init(void)
{
    q0 = 1.0f;
    q1 = 0.0f;
    q2 = 0.0f;
    q3 = 0.0f;
}

/**
 * @brief 使用陀螺仪和加速度计数据更新姿态。
 * @details 实现了Madgwick AHRS（姿态和航向参考系统）算法的核心部分。
 * 该算法融合了陀螺仪的角速度和加速度计的重力向量，以四元数形式计算姿态。
 * @param gx, gy, gz陀螺仪测量的三轴角速度 (单位: 弧度/秒).
 * @param ax, ay, az加速度计测量的三轴加速度 (单位: m/s^2).
 */
void Madgwick_Update(float gx, float gy, float gz, float ax, float ay, float az)
{
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2 ,_8q1, _8q2, q0q0, q1q1, q2q2, q3q3;

    // --- 1. 基于陀螺仪数据计算四元数变化率 ---
    // 这是姿态更新的“预测”部分
    qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
    qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
    qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
    qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

    // --- 2. 基于加速度计数据计算修正量 ---
    // 这是姿态更新的“修正”部分，仅在加速度计读数有效时执行
    if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

        // 2a. 将加速度计读数归一化，使其成为单位向量
        recipNorm = 1.0f / sqrtf(ax * ax + ay * ay + az * az);
        ax *= recipNorm;
        ay *= recipNorm;
        az *= recipNorm;

        // 2b. 预计算一些重复项以提高效率
        _2q0 = 2.0f * q0;
        _2q1 = 2.0f * q1;
        _2q2 = 2.0f * q2;
        _2q3 = 2.0f * q3;
        _4q0 = 4.0f * q0;
        _4q1 = 4.0f * q1;
        _4q2 = 4.0f * q2;
        _8q1 = 8.0f * q1;
        _8q2 = 8.0f * q2;
        q0q0 = q0 * q0;
        q1q1 = q1 * q1;
        q2q2 = q2 * q2;
        q3q3 = q3 * q3;

        // 2c. 梯度下降算法：计算当前姿态下重力向量的估计值与实际测量值之间的误差
        // 's'向量是目标函数(误差函数)的梯度
        s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
        s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
        s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
        s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
        
        // 2d. 归一化梯度
        recipNorm = 1.0f / sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        s0 *= recipNorm;
        s1 *= recipNorm;
        s2 *= recipNorm;
        s3 *= recipNorm;

        // 2e. 将修正量应用到四元数变化率上
        qDot1 -= beta * s0;
        qDot2 -= beta * s1;
        qDot3 -= beta * s2;
        qDot4 -= beta * s3;
    }

    // --- 3. 积分，更新四元数 ---
    // 将总的四元数变化率（陀螺仪预测 + 加速度计修正）乘以时间间隔，得到姿态变化量
    float dt = 1.0f / sampleFreq;
    q0 += qDot1 * dt;
    q1 += qDot2 * dt;
    q2 += qDot3 * dt;
    q3 += qDot4 * dt;

    // --- 4. 归一化四元数 ---
    // 保持四元数为单位长度，防止因计算误差累积导致的“范数”漂移
    recipNorm = 1.0f / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= recipNorm;
    q1 *= recipNorm;
    q2 *= recipNorm;
    q3 *= recipNorm;

    // --- 5. 将四元数转换为欧拉角 ---
    // 将姿态结果转换为更直观的翻滚角、俯仰角和偏航角，并转换为度
    const float rad_to_deg = 57.295779513082320876798154814105f; // 180/PI
    Attitude_Data.rol = atan2f(2.0f * (q0 * q1 + q2 * q3), 1.0f - 2.0f * (q1 * q1 + q2 * q2)) * rad_to_deg;
    Attitude_Data.pit = asinf(2.0f * (q0 * q2 - q3 * q1)) * rad_to_deg;
    Attitude_Data.yaw = atan2f(2.0f * (q0 * q3 + q1 * q2), 1.0f - 2.0f * (q2 * q2 + q3 * q3)) * rad_to_deg;
}


