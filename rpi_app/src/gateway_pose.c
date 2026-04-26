#include "gateway_pose.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define IIO_SYSFS_ROOT "/sys/bus/iio/devices"
#define MPU6050_DEFAULT_ACCEL_SCALE (9.80665f / 16384.0f)
#define STANDARD_GRAVITY 9.80665f
#define RAD_TO_DEG 57.29577951308232f

static int find_iio_device_dir(const char *iio_root, char *out, size_t out_size)
{
    DIR *dir = opendir(iio_root);
    if (!dir)
        return -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (strncmp(ent->d_name, "iio:device", 10) != 0)
            continue;

        char name_path[512];
        char name_buf[128] = {0};
        int path_len = snprintf(name_path, sizeof(name_path), "%s/%s/name", iio_root, ent->d_name);
        if (path_len < 0 || (size_t)path_len >= sizeof(name_path))
            continue;

        FILE *fp = fopen(name_path, "r");
        if (!fp)
            continue;

        if (fgets(name_buf, sizeof(name_buf), fp) != NULL)
        {
            name_buf[strcspn(name_buf, "\r\n")] = '\0';
            if (strstr(name_buf, "mpu") != NULL || strstr(name_buf, "icm") != NULL || strstr(name_buf, "accel") != NULL)
            {
                path_len = snprintf(out, out_size, "%s/%s", iio_root, ent->d_name);
                fclose(fp);
                closedir(dir);
                if (path_len < 0 || (size_t)path_len >= out_size)
                    return -1;
                return 0;
            }
        }

        fclose(fp);
    }

    closedir(dir);
    return -1;
}

static int read_int_from_file(const char *path, int *out)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    char buf[64];
    if (!fgets(buf, sizeof(buf), fp))
    {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *out = atoi(buf);
    return 0;
}

static int read_float_from_file(const char *path, float *out)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    char buf[64];
    if (!fgets(buf, sizeof(buf), fp))
    {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *out = strtof(buf, NULL);
    return 0;
}

static float clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

int gateway_pose_read_snapshot_from_root(const char *iio_root, gateway_pose_t *pose)
{
    if (!iio_root || !pose)
        return -1;

    memset(pose, 0, sizeof(*pose));
    pose->timestamp = (uint32_t)time(NULL);
    pose->accel_scale = MPU6050_DEFAULT_ACCEL_SCALE;

    char dev_dir[512];
    if (find_iio_device_dir(iio_root, dev_dir, sizeof(dev_dir)) != 0)
        return -1;

    char path[1024];

    snprintf(path, sizeof(path), "%s/in_accel_x_raw", dev_dir);
    if (read_int_from_file(path, &pose->accel_x_raw) != 0)
        return -1;

    snprintf(path, sizeof(path), "%s/in_accel_y_raw", dev_dir);
    if (read_int_from_file(path, &pose->accel_y_raw) != 0)
        return -1;

    snprintf(path, sizeof(path), "%s/in_accel_z_raw", dev_dir);
    if (read_int_from_file(path, &pose->accel_z_raw) != 0)
        return -1;

    snprintf(path, sizeof(path), "%s/in_accel_scale", dev_dir);
    float scale = 0.0f;
    if (read_float_from_file(path, &scale) == 0 && scale > 0.0f)
        pose->accel_scale = scale;

    pose->accel_x_g = ((float)pose->accel_x_raw * pose->accel_scale) / STANDARD_GRAVITY;
    pose->accel_y_g = ((float)pose->accel_y_raw * pose->accel_scale) / STANDARD_GRAVITY;
    pose->accel_z_g = ((float)pose->accel_z_raw * pose->accel_scale) / STANDARD_GRAVITY;

    float ax = pose->accel_x_g;
    float ay = pose->accel_y_g;
    float az = pose->accel_z_g;
    float norm = sqrtf(ax * ax + ay * ay + az * az);
    if (norm < 0.000001f)
        return -1;

    pose->roll_deg = atan2f(ay, az) * RAD_TO_DEG;
    pose->pitch_deg = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
    pose->tilt_deg = acosf(clamp_float(az / norm, -1.0f, 1.0f)) * RAD_TO_DEG;
    pose->valid = 1;

    return 0;
}

int gateway_pose_read_snapshot(gateway_pose_t *pose)
{
    return gateway_pose_read_snapshot_from_root(IIO_SYSFS_ROOT, pose);
}

int gateway_pose_check_threshold(const gateway_pose_t *pose,
                                 float peak_g, float rms_g, float gyro_dps, const char **reason)
{
    if (!pose || !pose->valid)
        return 0;

    float ax = pose->accel_x_g < 0 ? -pose->accel_x_g : pose->accel_x_g;
    float ay = pose->accel_y_g < 0 ? -pose->accel_y_g : pose->accel_y_g;
    float az = pose->accel_z_g < 0 ? -pose->accel_z_g : pose->accel_z_g;

    if (peak_g > 0.0f)
    {
        if (ax > peak_g) { if (reason) *reason = "accel_x exceeds peak"; return 1; }
        if (ay > peak_g) { if (reason) *reason = "accel_y exceeds peak"; return 1; }
        if (az > peak_g) { if (reason) *reason = "accel_z exceeds peak"; return 1; }
    }

    if (rms_g > 0.0f)
    {
        float rms = sqrtf((ax * ax + ay * ay + az * az) / 3.0f);
        if (rms > rms_g)
        {
            if (reason) *reason = "rms exceeds threshold";
            return 1;
        }
    }

    if (gyro_dps > 0.0f)
    {
        float tilt_mag = pose->tilt_deg < 0 ? -pose->tilt_deg : pose->tilt_deg;
        if (tilt_mag > gyro_dps)
        {
            if (reason) *reason = "tilt exceeds gyro threshold";
            return 1;
        }
    }

    return 0;
}
