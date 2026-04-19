#ifndef EDGE_PROTOCOL_H
#define EDGE_PROTOCOL_H

#include <math.h>
#include "cJSON.h"
#include "db.h"

int protocol_to_json(const sensor_data_t *data, char *out_buf, size_t buf_len);

int protocol_encode_batch(const sensor_data_t *data_array, int count, char *out_buf, size_t buf_len);


#endif