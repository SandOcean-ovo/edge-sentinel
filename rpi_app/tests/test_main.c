#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <math.h>

#include "unity.h"

#include "ringbuf.h"
#include "crc16.h"
#include "gateway_pose.h"
#include "protocol_utils.h"
#include "conf.h"
#include "gateway_monitor_ioctl.h"
#include "parse.h"
#include "cJSON.h"

void setUp(void) {}
void tearDown(void) {}

/* ========== helpers ========== */

static void make_path(char *out, size_t out_size, const char *dir, const char *name)
{
    if (out == NULL || out_size == 0 || dir == NULL || name == NULL) {
        TEST_FAIL_MESSAGE("invalid path input");
        return;
    }

    int n = snprintf(out, out_size, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= out_size) {
        out[0] = '\0';
        TEST_FAIL_MESSAGE("path too long");
    }
}

static void write_text_file(const char *path, const char *content)
{
    if (path == NULL || content == NULL) {
        TEST_FAIL_MESSAGE("invalid file input");
        return;
    }

    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        TEST_FAIL_MESSAGE("fopen failed");
        return;
    }

    int write_rc = fputs(content, fp);
    int close_rc = fclose(fp);
    TEST_ASSERT_NOT_EQUAL(EOF, write_rc);
    TEST_ASSERT_EQUAL_INT(0, close_rc);
}

static const cJSON *require_object(const cJSON *parent, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (!cJSON_IsObject(item)) {
        TEST_FAIL_MESSAGE("missing JSON object");
        return NULL;
    }
    return item;
}

static double require_number(const cJSON *parent, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (!cJSON_IsNumber(item)) {
        TEST_FAIL_MESSAGE("missing JSON number");
        return 0.0;
    }
    return item->valuedouble;
}

static void assert_reason_contains(const char *reason, const char *needle)
{
    if (reason == NULL || needle == NULL) {
        TEST_FAIL_MESSAGE("missing reason text");
        return;
    }

    TEST_ASSERT_NOT_NULL(strstr(reason, needle));
}

/* ========== CRC16 ========== */

void test_crc16_known_value(void)
{
    const uint8_t sample_msg[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t result = crc_calculate(sample_msg, 6);
    TEST_ASSERT_EQUAL_HEX16(0xBB53, result);
}

/* ========== RingBuf ========== */

void test_ringbuf_basic_ops(void)
{
    uint8_t raw_buffer[10];
    RingBuf_t rb;
    RingBuf_init(&rb, raw_buffer, 10);

    TEST_ASSERT_EQUAL_UINT32(0, RingBuf_getreadable(&rb));
    uint8_t tmp;
    TEST_ASSERT_FALSE(RingBuf_read(&rb, &tmp));

    for (uint8_t i = 0; i < 9; i++) {
        TEST_ASSERT_TRUE(RingBuf_write(&rb, i));
    }
    TEST_ASSERT_FALSE(RingBuf_write(&rb, 99));
    TEST_ASSERT_EQUAL_UINT32(9, RingBuf_getreadable(&rb));

    uint8_t read_out[10];
    uint32_t read_len = RingBuf_readblocks(&rb, read_out, 5);
    TEST_ASSERT_EQUAL_UINT32(5, read_len);
    TEST_ASSERT_EQUAL_UINT8(0, read_out[0]);
    TEST_ASSERT_EQUAL_UINT8(4, read_out[4]);
    TEST_ASSERT_EQUAL_UINT32(4, RingBuf_getreadable(&rb));

    const uint8_t data_to_wrap[] = {10, 11, 12, 13};
    uint32_t write_len = RingBuf_writeblocks(&rb, data_to_wrap, 4);
    TEST_ASSERT_EQUAL_UINT32(4, write_len);
    TEST_ASSERT_EQUAL_UINT32(3, rb.head);

    uint8_t final_out[8];
    uint32_t final_read_len = RingBuf_readblocks(&rb, final_out, 8);
    TEST_ASSERT_EQUAL_UINT32(8, final_read_len);
    TEST_ASSERT_EQUAL_UINT8(8, final_out[3]);
    TEST_ASSERT_EQUAL_UINT8(10, final_out[4]);
    TEST_ASSERT_EQUAL_UINT8(13, final_out[7]);
}

/* ========== Config ========== */

void test_config_thresholds(void)
{
    char root_template[] = "/tmp/edge_conf_test_XXXXXX";
    char *root = mkdtemp(root_template);
    if (root == NULL) {
        TEST_FAIL_MESSAGE("mkdtemp failed");
        return;
    }

    char conf_path[512];
    make_path(conf_path, sizeof(conf_path), root, "test.conf");

    const char *conf_content =
        "baudrate = 115200\n"
        "temperture_threshold = 60.5\n"
        "accel_peak_threshold = 2.5\n"
        "accel_rms_threshold = 0.8\n"
        "gyro_threshold = 45.0\n"
        "device_id = test-unit\n"
        "uart_dev = /dev/ttyTEST0\n"
        "log_file = /tmp/gw.log\n"
        "database_file = /tmp/gw.db\n"
        "ip = 192.168.0.1\n"
        "port = 9090\n";
    write_text_file(conf_path, conf_content);

    GatewayConfig_t cfg = {0};
    TEST_ASSERT_EQUAL_INT(0, load_config(conf_path, &cfg));

    TEST_ASSERT_EQUAL_INT(115200, cfg.baudrate);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.5f, cfg.temp_threshold);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, cfg.accel_peak_threshold);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.8f, cfg.accel_rms_threshold);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, cfg.gyro_threshold);
    TEST_ASSERT_EQUAL_STRING("test-unit", cfg.device_id);
    TEST_ASSERT_EQUAL_STRING("/dev/ttyTEST0", cfg.uart_dev);
    TEST_ASSERT_EQUAL_STRING("/tmp/gw.log", cfg.log_file);
    TEST_ASSERT_EQUAL_STRING("/tmp/gw.db", cfg.db_path);
    TEST_ASSERT_EQUAL_STRING("192.168.0.1", cfg.ip);
    TEST_ASSERT_EQUAL_INT(9090, cfg.port);

    GatewayConfig_t empty = {0};
    TEST_ASSERT_EQUAL_FLOAT(0.0f, empty.accel_peak_threshold);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, empty.accel_rms_threshold);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, empty.gyro_threshold);

    unlink(conf_path);
    rmdir(root);
}

void test_config_file_missing(void)
{
    GatewayConfig_t cfg = {0};
    TEST_ASSERT_NOT_EQUAL(0, load_config("/tmp/edge_nonexistent_conf_99999.conf", &cfg));
}

/* ========== Gateway Pose ========== */

void test_gateway_pose_snapshot(void)
{
    char root_template[] = "/tmp/edge_pose_test_XXXXXX";
    char *root = mkdtemp(root_template);
    if (root == NULL) {
        TEST_FAIL_MESSAGE("mkdtemp failed");
        return;
    }

    char dev_dir[512];
    make_path(dev_dir, sizeof(dev_dir), root, "iio:device0");
    TEST_ASSERT_EQUAL_INT(0, mkdir(dev_dir, 0700));

    char path[1024];
    make_path(path, sizeof(path), dev_dir, "name");
    write_text_file(path, "mpu6050\n");
    make_path(path, sizeof(path), dev_dir, "in_accel_x_raw");
    write_text_file(path, "0\n");
    make_path(path, sizeof(path), dev_dir, "in_accel_y_raw");
    write_text_file(path, "0\n");
    make_path(path, sizeof(path), dev_dir, "in_accel_z_raw");
    write_text_file(path, "16384\n");

    gateway_pose_t pose = {0};
    TEST_ASSERT_EQUAL_INT(0, gateway_pose_read_snapshot_from_root(root, &pose));
    TEST_ASSERT_EQUAL_INT(1, pose.valid);
    TEST_ASSERT_EQUAL_INT(0, pose.accel_x_raw);
    TEST_ASSERT_EQUAL_INT(0, pose.accel_y_raw);
    TEST_ASSERT_EQUAL_INT(16384, pose.accel_z_raw);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pose.accel_x_g);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pose.accel_y_g);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, pose.accel_z_g);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pose.roll_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pose.pitch_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pose.tilt_deg);

    make_path(path, sizeof(path), dev_dir, "name");         unlink(path);
    make_path(path, sizeof(path), dev_dir, "in_accel_x_raw"); unlink(path);
    make_path(path, sizeof(path), dev_dir, "in_accel_y_raw"); unlink(path);
    make_path(path, sizeof(path), dev_dir, "in_accel_z_raw"); unlink(path);
    rmdir(dev_dir);
    rmdir(root);
}

void test_gateway_pose_check_threshold(void)
{
    const char *reason = NULL;
    const float peak = 1.5f, rms = 0.7f, gyro = 30.0f;

    /* 静置正常姿态 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 0.0f, .accel_y_g = 0.0f, .accel_z_g = 1.0f,
                               .tilt_deg = 0.0f};
        TEST_ASSERT_EQUAL_INT(0, gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason));
        TEST_ASSERT_NULL(reason);
    }
    /* X 轴峰值超标 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 2.0f, .accel_y_g = 0.0f, .accel_z_g = 1.0f,
                               .tilt_deg = 5.0f};
        reason = NULL;
        TEST_ASSERT_NOT_EQUAL(0, gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason));
        assert_reason_contains(reason, "peak");
    }
    /* Y 轴负向峰值超标 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 0.0f, .accel_y_g = -2.5f, .accel_z_g = 1.0f,
                               .tilt_deg = 5.0f};
        reason = NULL;
        TEST_ASSERT_NOT_EQUAL(0, gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason));
        assert_reason_contains(reason, "peak");
    }
    /* Z 轴峰值超标 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 0.0f, .accel_y_g = 0.0f, .accel_z_g = 3.0f,
                               .tilt_deg = 5.0f};
        reason = NULL;
        TEST_ASSERT_NOT_EQUAL(0, gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason));
        assert_reason_contains(reason, "peak");
    }
    /* RMS 超标 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 1.4f, .accel_y_g = 1.4f, .accel_z_g = 1.4f,
                               .tilt_deg = 5.0f};
        reason = NULL;
        TEST_ASSERT_NOT_EQUAL(0, gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason));
        assert_reason_contains(reason, "rms");
    }
    /* 倾斜超标 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 0.0f, .accel_y_g = 0.0f, .accel_z_g = 0.7f,
                               .tilt_deg = 45.0f};
        reason = NULL;
        TEST_ASSERT_NOT_EQUAL(0, gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason));
        assert_reason_contains(reason, "gyro");
    }
    /* 负向倾斜超标 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 0.0f, .accel_y_g = 0.0f, .accel_z_g = 0.7f,
                               .tilt_deg = -60.0f};
        reason = NULL;
        TEST_ASSERT_NOT_EQUAL(0, gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason));
        assert_reason_contains(reason, "gyro");
    }
    /* invalid pose */
    {
        gateway_pose_t pose = {.valid = 0,
                               .accel_x_g = 10.0f, .accel_y_g = 10.0f, .accel_z_g = 10.0f,
                               .tilt_deg = 90.0f};
        TEST_ASSERT_EQUAL_INT(0, gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason));
    }
    /* NULL pose */
    TEST_ASSERT_EQUAL_INT(0, gateway_pose_check_threshold(NULL, peak, rms, gyro, &reason));
    /* 阈值为 0 关闭检测 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 10.0f, .accel_y_g = 10.0f, .accel_z_g = 10.0f,
                               .tilt_deg = 90.0f};
        TEST_ASSERT_EQUAL_INT(0, gateway_pose_check_threshold(&pose, 0.0f, 0.0f, 0.0f, &reason));
    }
}

/* ========== IOCTL Commands ========== */

void test_ioctl_commands(void)
{
    TEST_ASSERT_NOT_EQUAL(GATEWAY_MONITOR_IOC_SET_PEAK_THR, GATEWAY_MONITOR_IOC_SET_RMS_THR);
    TEST_ASSERT_NOT_EQUAL(GATEWAY_MONITOR_IOC_SET_PEAK_THR, GATEWAY_MONITOR_IOC_SET_GYRO_THR);
    TEST_ASSERT_NOT_EQUAL(GATEWAY_MONITOR_IOC_SET_RMS_THR, GATEWAY_MONITOR_IOC_SET_GYRO_THR);
    TEST_ASSERT_EQUAL_STRING("/dev/gateway_monitor", GATEWAY_MONITOR_DEVICE);
    TEST_ASSERT_EQUAL_CHAR('G', GATEWAY_MONITOR_IOC_MAGIC);
    TEST_ASSERT_NOT_EQUAL(0, GATEWAY_MONITOR_IOC_SET_PEAK_THR);
    TEST_ASSERT_NOT_EQUAL(0, GATEWAY_MONITOR_IOC_SET_RMS_THR);
    TEST_ASSERT_NOT_EQUAL(0, GATEWAY_MONITOR_IOC_SET_GYRO_THR);
}

/* ========== Alarm Pose JSON ========== */

void test_alarm_pose_json(void)
{
    gateway_pose_t pose = {
        .accel_x_raw = 1, .accel_y_raw = -2, .accel_z_raw = 3,
        .accel_scale = 0.00059855f,
        .accel_x_g = 0.1f, .accel_y_g = -0.2f, .accel_z_g = 0.9f,
        .roll_deg = -12.5f, .pitch_deg = 6.25f, .tilt_deg = 14.0f,
        .timestamp = 123456, .valid = 1,
    };

    char json[1024];
    TEST_ASSERT_EQUAL_INT(0, protocol_encode_alarm_pose("edge-pi-01", 1, &pose, json, sizeof(json)));

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        TEST_FAIL_MESSAGE("cJSON_Parse failed");
        return;
    }

    const cJSON *device_id = cJSON_GetObjectItemCaseSensitive(root, "device_id");
    if (!cJSON_IsString(device_id)) {
        cJSON_Delete(root);
        TEST_FAIL_MESSAGE("missing device_id");
        return;
    }
    TEST_ASSERT_EQUAL_STRING("edge-pi-01", device_id->valuestring);
    TEST_ASSERT_EQUAL_INT(1, (int)require_number(root, "alarm"));
    TEST_ASSERT_EQUAL_UINT32(123456, (uint32_t)require_number(root, "timestamp"));

    const cJSON *gateway_pose = require_object(root, "gateway_pose");
    TEST_ASSERT_EQUAL_INT(1, (int)require_number(gateway_pose, "valid"));

    const cJSON *accel_raw = require_object(gateway_pose, "accel_raw");
    TEST_ASSERT_EQUAL_INT(1, (int)require_number(accel_raw, "x"));
    TEST_ASSERT_EQUAL_INT(-2, (int)require_number(accel_raw, "y"));
    TEST_ASSERT_EQUAL_INT(3, (int)require_number(accel_raw, "z"));

    const cJSON *accel_g = require_object(gateway_pose, "accel_g");
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.1f, (float)require_number(accel_g, "x"));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.2f, (float)require_number(accel_g, "y"));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.9f, (float)require_number(accel_g, "z"));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 14.0f, (float)require_number(gateway_pose, "tilt_deg"));

    cJSON_Delete(root);
}

/* ========== Protocol Parser ========== */

static void build_frame(uint8_t *buf, uint32_t *out_len,
                        uint8_t type, const uint8_t *payload, uint8_t payload_len)
{
    buf[0] = 0xAA;
    buf[1] = 0x55;
    buf[2] = payload_len + 2;
    buf[3] = type;
    memcpy(&buf[4], payload, payload_len);
    uint32_t total = payload_len + 4;
    uint16_t crc = crc_calculate(buf, total);
    buf[total] = crc & 0xFF;
    buf[total + 1] = (crc >> 8) & 0xFF;
    *out_len = total + 2;
}

void test_parse_valid_heartbeat(void)
{
    uint8_t raw_buf[256];
    RingBuf_t rb;
    RingBuf_init(&rb, raw_buf, sizeof(raw_buf));

    uint8_t frame[32];
    uint32_t frame_len;
    uint8_t payload[] = {0x00};
    build_frame(frame, &frame_len, 0x01, payload, sizeof(payload));

    RingBuf_writeblocks(&rb, frame, frame_len);

    SentinelFrame_t parsed = {0};
    TEST_ASSERT_TRUE(protocol_parse(&rb, &parsed));
    TEST_ASSERT_EQUAL_UINT8(0x01, parsed.type);
    TEST_ASSERT_EQUAL_UINT32(0, RingBuf_getreadable(&rb));
}

void test_parse_half_packet(void)
{
    uint8_t raw_buf[256];
    RingBuf_t rb;
    RingBuf_init(&rb, raw_buf, sizeof(raw_buf));

    uint8_t frame[32];
    uint32_t frame_len;
    uint8_t payload[] = {0x00};
    build_frame(frame, &frame_len, 0x01, payload, sizeof(payload));

    RingBuf_writeblocks(&rb, frame, frame_len - 2);

    SentinelFrame_t parsed = {0};
    TEST_ASSERT_FALSE(protocol_parse(&rb, &parsed));
    TEST_ASSERT_GREATER_THAN_UINT32(0, RingBuf_getreadable(&rb));

    RingBuf_writeblocks(&rb, frame + frame_len - 2, 2);
    TEST_ASSERT_TRUE(protocol_parse(&rb, &parsed));
    TEST_ASSERT_EQUAL_UINT8(0x01, parsed.type);
}

void test_parse_sticky_packets(void)
{
    uint8_t raw_buf[512];
    RingBuf_t rb;
    RingBuf_init(&rb, raw_buf, sizeof(raw_buf));

    uint8_t frame1[32], frame2[32];
    uint32_t len1, len2;
    uint8_t p1[] = {0x00};
    uint8_t p2[] = {0x11, 0x22};
    build_frame(frame1, &len1, 0x01, p1, sizeof(p1));
    build_frame(frame2, &len2, 0x02, p2, sizeof(p2));

    RingBuf_writeblocks(&rb, frame1, len1);
    RingBuf_writeblocks(&rb, frame2, len2);

    SentinelFrame_t parsed = {0};
    TEST_ASSERT_TRUE(protocol_parse(&rb, &parsed));
    TEST_ASSERT_EQUAL_UINT8(0x01, parsed.type);

    TEST_ASSERT_TRUE(protocol_parse(&rb, &parsed));
    TEST_ASSERT_EQUAL_UINT8(0x02, parsed.type);
    TEST_ASSERT_EQUAL_UINT8(0x11, parsed.data.raw_payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x22, parsed.data.raw_payload[1]);

    TEST_ASSERT_EQUAL_UINT32(0, RingBuf_getreadable(&rb));
}

void test_parse_crc_error(void)
{
    uint8_t raw_buf[256];
    RingBuf_t rb;
    RingBuf_init(&rb, raw_buf, sizeof(raw_buf));

    uint8_t frame[32];
    uint32_t frame_len;
    uint8_t payload[] = {0x00};
    build_frame(frame, &frame_len, 0x01, payload, sizeof(payload));

    frame[frame_len - 1] ^= 0xFF;

    RingBuf_writeblocks(&rb, frame, frame_len);

    SentinelFrame_t parsed = {0};
    TEST_ASSERT_FALSE(protocol_parse(&rb, &parsed));
}

void test_parse_garbage_then_valid(void)
{
    uint8_t raw_buf[512];
    RingBuf_t rb;
    RingBuf_init(&rb, raw_buf, sizeof(raw_buf));

    uint8_t garbage[] = {0x12, 0x34, 0x56, 0x78, 0xFF};
    RingBuf_writeblocks(&rb, garbage, sizeof(garbage));

    uint8_t frame[32];
    uint32_t frame_len;
    uint8_t payload[] = {0xAB};
    build_frame(frame, &frame_len, 0x01, payload, sizeof(payload));
    RingBuf_writeblocks(&rb, frame, frame_len);

    SentinelFrame_t parsed = {0};
    TEST_ASSERT_TRUE(protocol_parse(&rb, &parsed));
    TEST_ASSERT_EQUAL_UINT8(0x01, parsed.type);
    TEST_ASSERT_EQUAL_UINT8(0xAB, parsed.data.raw_payload[0]);
}

void test_parse_empty_buffer(void)
{
    uint8_t raw_buf[64];
    RingBuf_t rb;
    RingBuf_init(&rb, raw_buf, sizeof(raw_buf));

    SentinelFrame_t parsed = {0};
    TEST_ASSERT_FALSE(protocol_parse(&rb, &parsed));
}

/* ========== main ========== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_crc16_known_value);
    RUN_TEST(test_ringbuf_basic_ops);
    RUN_TEST(test_config_thresholds);
    RUN_TEST(test_config_file_missing);
    RUN_TEST(test_gateway_pose_snapshot);
    RUN_TEST(test_gateway_pose_check_threshold);
    RUN_TEST(test_ioctl_commands);
    RUN_TEST(test_alarm_pose_json);

    RUN_TEST(test_parse_valid_heartbeat);
    RUN_TEST(test_parse_half_packet);
    RUN_TEST(test_parse_sticky_packets);
    RUN_TEST(test_parse_crc_error);
    RUN_TEST(test_parse_garbage_then_valid);
    RUN_TEST(test_parse_empty_buffer);

    return UNITY_END();
}
