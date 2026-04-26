#ifndef EDGE_GATEWAY_POSE_H
#define EDGE_GATEWAY_POSE_H

#include <stdint.h>

typedef struct gateway_pose_t
{
    int accel_x_raw;
    int accel_y_raw;
    int accel_z_raw;
    float accel_scale;
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float roll_deg;
    float pitch_deg;
    float tilt_deg;
    uint32_t timestamp;
    int valid;
} gateway_pose_t;

int gateway_pose_read_snapshot(gateway_pose_t *pose);
int gateway_pose_read_snapshot_from_root(const char *iio_root, gateway_pose_t *pose);

/* 用户态兜底：比较 pose 数据与阈值，返回非 0 表示触发告警 */
int gateway_pose_check_threshold(const gateway_pose_t *pose,
                                 float peak_g, float rms_g, float gyro_dps, const char **reason);

#endif
