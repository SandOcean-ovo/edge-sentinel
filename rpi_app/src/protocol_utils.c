#include "protocol_utils.h"

#include <math.h>

static double round_to_3(float val)
{
    return floor(val * 1000.0 + 0.5) / 1000.0;
}

int protocol_to_json(const sensor_data_t *data, char *out_buf, size_t buf_len)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return -1;

    // 1. 组装 JSON 对象，字段名需与后端/协议约定一致
    cJSON_AddNumberToObject(root, "accel_peak", round_to_3(data->accel_peak));
    cJSON_AddNumberToObject(root, "accel_rms",  round_to_3(data->accel_rms));
    cJSON_AddNumberToObject(root, "gyro_x",      round_to_3(data->gyro_mean_x));
    cJSON_AddNumberToObject(root, "gyro_y",      round_to_3(data->gyro_mean_y));
    cJSON_AddNumberToObject(root, "gyro_z",      round_to_3(data->gyro_mean_z));
    cJSON_AddNumberToObject(root, "timestamp",   (double)data->timestamp);

    // 2. 转换到预分配的 buffer (0 代表压缩格式，1 代表格式化)
    int ok = cJSON_PrintPreallocated(root, out_buf, (int)buf_len, 0);

    // 3. 释放 cJSON 内部节点
    cJSON_Delete(root);

    return ok ? 0 : -1;
}

int protocol_encode_batch(const sensor_data_t *data_array, int count, char *out_buf, size_t buf_len)
{
    if (count <= 0) return -1;

    cJSON *array = cJSON_CreateArray();
    if (!array)
        return -1;

    for (int i = 0; i < count; ++i)
    {
        cJSON *item = cJSON_CreateObject();
        if (!item)
        {
            cJSON_Delete(array);
            return -1;
        }
        
        // 映射结构体中的特征数据
        cJSON_AddNumberToObject(item, "accel_peak", round_to_3(data_array[i].accel_peak));
        cJSON_AddNumberToObject(item, "accel_rms",  round_to_3(data_array[i].accel_rms));
        cJSON_AddNumberToObject(item, "gyro_x",      round_to_3(data_array[i].gyro_mean_x));
        cJSON_AddNumberToObject(item, "gyro_y",      round_to_3(data_array[i].gyro_mean_y));
        cJSON_AddNumberToObject(item, "gyro_z",      round_to_3(data_array[i].gyro_mean_z));
        cJSON_AddNumberToObject(item, "timestamp",   data_array[i].timestamp);

        cJSON_AddItemToArray(array, item);
    }

    // 转换到 buffer
    int ok = cJSON_PrintPreallocated(array, out_buf, (int)buf_len, 0);

    cJSON_Delete(array);

    return ok ? 0 : -1;
}

int protocol_encode_alarm_pose(const char *device_id, int alarm_active,
                               const gateway_pose_t *pose,
                               char *out_buf, size_t buf_len)
{
    if (!device_id || !pose || !out_buf || buf_len == 0)
        return -1;

    cJSON *root = cJSON_CreateObject();
    cJSON *gateway_pose = cJSON_CreateObject();
    cJSON *accel_raw = cJSON_CreateObject();
    cJSON *accel_g = cJSON_CreateObject();
    if (!root || !gateway_pose || !accel_raw || !accel_g)
    {
        cJSON_Delete(root);
        cJSON_Delete(gateway_pose);
        cJSON_Delete(accel_raw);
        cJSON_Delete(accel_g);
        return -1;
    }

    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "alarm", alarm_active);
    cJSON_AddNumberToObject(root, "timestamp", pose->timestamp);

    cJSON_AddNumberToObject(gateway_pose, "valid", pose->valid);

    cJSON_AddNumberToObject(accel_raw, "x", pose->accel_x_raw);
    cJSON_AddNumberToObject(accel_raw, "y", pose->accel_y_raw);
    cJSON_AddNumberToObject(accel_raw, "z", pose->accel_z_raw);
    cJSON_AddItemToObject(gateway_pose, "accel_raw", accel_raw);

    cJSON_AddNumberToObject(gateway_pose, "accel_scale_mps2", pose->accel_scale);

    cJSON_AddNumberToObject(accel_g, "x", pose->accel_x_g);
    cJSON_AddNumberToObject(accel_g, "y", pose->accel_y_g);
    cJSON_AddNumberToObject(accel_g, "z", pose->accel_z_g);
    cJSON_AddItemToObject(gateway_pose, "accel_g", accel_g);

    cJSON_AddNumberToObject(gateway_pose, "roll_deg", pose->roll_deg);
    cJSON_AddNumberToObject(gateway_pose, "pitch_deg", pose->pitch_deg);
    cJSON_AddNumberToObject(gateway_pose, "tilt_deg", pose->tilt_deg);
    cJSON_AddItemToObject(root, "gateway_pose", gateway_pose);

    int ok = cJSON_PrintPreallocated(root, out_buf, (int)buf_len, 0);
    cJSON_Delete(root);
    return ok ? 0 : -1;
}
