#ifndef EDGE_PROTOCOL_H
#define EDGE_PROTOCOL_H

#include <stddef.h>
#include "cJSON.h"
#include "db.h"
#include "gateway_pose.h"

int protocol_to_json(const sensor_data_t *data, char *out_buf, size_t buf_len);

int protocol_encode_batch(const sensor_data_t *data_array, int count, char *out_buf, size_t buf_len);

int protocol_encode_alarm_pose(const char *device_id, int alarm_active,
                               const gateway_pose_t *pose,
                               char *out_buf, size_t buf_len);

#endif
