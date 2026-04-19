#ifndef EDGE_DB_H
#define EDGE_DB_H

#include <stdint.h>
#include <stddef.h>
#include <sqlite3.h>
#include "log.h"

typedef struct sensor_data_t
{
    int db_id;
    float accel_peak;
    float accel_rms;
    float gyro_mean_x;
    float gyro_mean_y;
    float gyro_mean_z;
    uint32_t timestamp;
    int is_sent; // 0: 未上报, 1: 已上报
} sensor_data_t;

// 初始化：创建表结构（注意使用 IF NOT EXISTS）
int db_init(const char *db_path);

int db_deinit(void);

// 存入数据：将解析出的传感器特征入库
int db_save_sensor_record(const sensor_data_t *data);

// 提取待补传数据：获取 is_sent=0 的最早 N 条记录
int db_get_unsent_records(sensor_data_t *out_list, int max_count);

// 状态更新：数据成功发送到云端后，更新标记位
int db_mark_as_sent(int id);

#endif