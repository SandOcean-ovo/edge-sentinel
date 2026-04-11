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
        edge_log(LOG_INFO, "Receive a heartbeat");
        break;
    case 0x02:
        edge_log(LOG_INFO, "Receive IMU data: accel_peak = %.2f, "
                                             "accel_rms = %.2f, "
                                             "gyro_mean_x = %.2f, "
                                             "gyro_mean_y = %.2f, "
                                             "gyro_mean_z = %.2f", 
                                             pframe->data.imu.accel_peak, 
                                             pframe->data.imu.accel_rms, 
                                             pframe->data.imu.gyro_mean_x, 
                                             pframe->data.imu.gyro_mean_y, 
                                             pframe->data.imu.gyro_mean_z
                                            );
        break;

    case 0x04:
        char* alarm_type;
        if(pframe->data.alarm.alarm_type == 0x01)
        {
            alarm_type = "ALARM_TYPE_BUTTON";
        }
        else if (pframe->data.alarm.alarm_type == 0x02)
        {
            alarm_type = "ALARM_TYPE_PEAK";
        }
        else if (pframe->data.alarm.alarm_type == 0x04)
        {
            alarm_type = "ALARM_TYPE_RMS";
        }
        else if (pframe->data.alarm.alarm_type == 0x08)
        {
            alarm_type = "ALARM_TYPE_RMS";
        }
        else
        {
            alarm_type = "INVALID";
        }
        
        edge_log(LOG_WARN, "Alarm received! alarm_type = %s, timestamp on MCU = %d", alarm_type, pframe->data.alarm.timestamp);
        break;

    
    default:
        printf("invalid type received! type:%d", pframe->type);
        break;
    }
}