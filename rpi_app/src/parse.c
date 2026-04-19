#include "parse.h"

bool protocol_parse(RingBuf_t *pbuf, SentinelFrame_t *pframe)
{
    while (RingBuf_getreadable(pbuf) >= MIN_PACKAGE_SIZE)
    {
        uint8_t temp[256];

        RingBuf_peek(pbuf, &temp[0], 0);
        RingBuf_peek(pbuf, &temp[1], 1);
        RingBuf_peek(pbuf, &temp[2], 2);

        if (temp[0] != 0xAA || temp[1] != 0x55)
        {
            RingBuf_skip(pbuf, 1);
            continue;
        }
        uint8_t len_field = temp[2];
        uint32_t total_frame_len = len_field + 4;

        if (RingBuf_getreadable(pbuf) < total_frame_len)
        {
            return false;
        }

        for (int i = 0; i < total_frame_len; ++i)
        {
            RingBuf_peek(pbuf, &temp[i], i);
        }
        uint16_t calc_crc = crc_calculate(temp, total_frame_len - 2);
        uint16_t packet_crc = temp[total_frame_len - 2] | (temp[total_frame_len - 1] << 8);

        if (calc_crc != packet_crc)
        {
            RingBuf_skip(pbuf, 1);
            return false;
        }
        RingBuf_skip(pbuf, total_frame_len);
        pframe->type = temp[3];
        pframe->len = len_field - 2; // 这里的 len 可以定义为纯 payload 长度
        memcpy(pframe->data.raw_payload, &temp[4], pframe->len);
        return true;
    }
    return false;
}

void handle_frame(SentinelFrame_t *pframe)
{
    switch (pframe->type)
    {
    case 0x01:
    {
        edge_log(LOG_INFO, "Receive a heartbeat\n");
        break;
    }
    case 0x02:
    {
        sensor_data_t record = {0};
        record.accel_peak = pframe->data.imu.accel_peak;
        record.accel_rms = pframe->data.imu.accel_rms;
        record.gyro_mean_x = pframe->data.imu.gyro_mean_x;
        record.gyro_mean_y = pframe->data.imu.gyro_mean_y;
        record.gyro_mean_z = pframe->data.imu.gyro_mean_z;
        record.timestamp = (uint32_t)time(NULL); // 从协议中获取 MCU 时间
        if (db_save_sensor_record(&record) != 0)
        {
            edge_log(LOG_INFO, "Failed to save sensor record!");
        }
        break;
    }
    case 0x04:
    {
        char alarm_desc[128] = {0};
        uint8_t type = pframe->data.alarm.alarm_type;

        if (type & 0x01)
            strcat(alarm_desc, "BUTTON ");
        if (type & 0x02)
            strcat(alarm_desc, "PEAK ");
        if (type & 0x04)
            strcat(alarm_desc, "RMS_X ");
        if (type & 0x08)
            strcat(alarm_desc, "RMS_Y ");
        // 如果有更多位，继续添加判断...

        if (alarm_desc[0] == '\0')
        {
            strcpy(alarm_desc, "NONE/UNKNOWN");
        }

        edge_log(LOG_WARN, "Alarm received! Flags: [ %s], MCU_Tick: %u",
                 alarm_desc, pframe->data.alarm.timestamp);
        break;
    }
    default:
    {
        edge_log(LOG_INFO, "invalid type received! type:%d", pframe->type);
        break;
    }
    }
}