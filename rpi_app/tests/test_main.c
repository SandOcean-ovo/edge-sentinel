#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <math.h>

#include "ringbuf.h"
#include "crc16.h"
#include "gateway_pose.h"
#include "protocol_utils.h"
#include "conf.h"
#include "gateway_monitor_ioctl.h"
#include "cJSON.h"

#define GREEN "\033[0;32m"
#define RED   "\033[0;31m"
#define RESET "\033[0m"

static void assert_close(float actual, float expected, float eps)
{
    assert(fabsf(actual - expected) <= eps);
}

static void make_path(char *out, size_t out_size, const char *dir, const char *name)
{
    int n = snprintf(out, out_size, "%s/%s", dir, name);
    assert(n >= 0 && (size_t)n < out_size);
}

static void write_text_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    assert(fputs(content, fp) >= 0);
    assert(fclose(fp) == 0);
}

/* ========== CRC16 ========== */
void test_crc16(void) {
    printf("Running CRC16 tests... ");

    uint8_t sample_msg[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t result = crc_calculate(sample_msg, 6);
    assert(result == 0xBB53);

    printf(GREEN "PASSED" RESET "\n");
}

/* ========== RingBuf ========== */
void test_ringbuf(void) {
    printf("Running RingBuf tests... ");

    uint8_t raw_buffer[10];
    RingBuf_t rb;
    RingBuf_init(&rb, raw_buffer, 10);

    assert(RingBuf_getreadable(&rb) == 0);
    uint8_t tmp;
    assert(RingBuf_read(&rb, &tmp) == false);

    for (uint8_t i = 0; i < 9; i++) {
        assert(RingBuf_write(&rb, i) == true);
    }
    assert(RingBuf_write(&rb, 99) == false);
    assert(RingBuf_getreadable(&rb) == 9);

    uint8_t read_out[10];
    uint32_t read_len = RingBuf_readblocks(&rb, read_out, 5);
    assert(read_len == 5);
    assert(read_out[0] == 0);
    assert(read_out[4] == 4);
    assert(RingBuf_getreadable(&rb) == 4);

    uint8_t data_to_wrap[] = {10, 11, 12, 13};
    uint32_t write_len = RingBuf_writeblocks(&rb, data_to_wrap, 4);
    assert(write_len == 4);
    assert(rb.head == 3);

    uint8_t final_out[8];
    uint32_t final_read_len = RingBuf_readblocks(&rb, final_out, 8);
    assert(final_read_len == 8);
    assert(final_out[3] == 8);
    assert(final_out[4] == 10);
    assert(final_out[7] == 13);

    printf(GREEN "PASSED" RESET "\n");
}

/* ========== Config 阈值解析 ========== */
void test_config_thresholds(void) {
    printf("Running Config threshold tests... ");

    char root_template[] = "/tmp/edge_conf_test_XXXXXX";
    char *root = mkdtemp(root_template);
    assert(root != NULL);

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
    assert(load_config(conf_path, &cfg) == 0);

    assert(cfg.baudrate == 115200);
    assert_close(cfg.temp_threshold, 60.5f, 0.01f);
    assert_close(cfg.accel_peak_threshold, 2.5f, 0.01f);
    assert_close(cfg.accel_rms_threshold, 0.8f, 0.01f);
    assert_close(cfg.gyro_threshold, 45.0f, 0.01f);
    assert(strcmp(cfg.device_id, "test-unit") == 0);
    assert(strcmp(cfg.uart_dev, "/dev/ttyTEST0") == 0);
    assert(strcmp(cfg.log_file, "/tmp/gw.log") == 0);
    assert(strcmp(cfg.db_path, "/tmp/gw.db") == 0);
    assert(strcmp(cfg.ip, "192.168.0.1") == 0);
    assert(cfg.port == 9090);

    /* 默认值：未解析的字段应为 0 */
    GatewayConfig_t empty = {0};
    assert(empty.accel_peak_threshold == 0.0f);
    assert(empty.accel_rms_threshold == 0.0f);
    assert(empty.gyro_threshold == 0.0f);

    unlink(conf_path);
    rmdir(root);

    printf(GREEN "PASSED" RESET "\n");
}

/* ========== 配置加载失败路径 ========== */
void test_config_file_missing(void) {
    printf("Running Config missing file test... ");

    GatewayConfig_t cfg = {0};
    assert(load_config("/tmp/edge_nonexistent_conf_99999.conf", &cfg) != 0);

    printf(GREEN "PASSED" RESET "\n");
}

/* ========== Gateway pose 快照 ========== */
void test_gateway_pose_snapshot(void) {
    printf("Running GatewayPose tests... ");

    char root_template[] = "/tmp/edge_pose_test_XXXXXX";
    char *root = mkdtemp(root_template);
    assert(root != NULL);

    char dev_dir[512];
    make_path(dev_dir, sizeof(dev_dir), root, "iio:device0");
    assert(mkdir(dev_dir, 0700) == 0);

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
    assert(gateway_pose_read_snapshot_from_root(root, &pose) == 0);
    assert(pose.valid == 1);
    assert(pose.accel_x_raw == 0);
    assert(pose.accel_y_raw == 0);
    assert(pose.accel_z_raw == 16384);
    assert_close(pose.accel_x_g, 0.0f, 0.001f);
    assert_close(pose.accel_y_g, 0.0f, 0.001f);
    assert_close(pose.accel_z_g, 1.0f, 0.001f);
    assert_close(pose.roll_deg, 0.0f, 0.001f);
    assert_close(pose.pitch_deg, 0.0f, 0.001f);
    assert_close(pose.tilt_deg, 0.0f, 0.001f);

    make_path(path, sizeof(path), dev_dir, "name");
    unlink(path);
    make_path(path, sizeof(path), dev_dir, "in_accel_x_raw");
    unlink(path);
    make_path(path, sizeof(path), dev_dir, "in_accel_y_raw");
    unlink(path);
    make_path(path, sizeof(path), dev_dir, "in_accel_z_raw");
    unlink(path);
    assert(rmdir(dev_dir) == 0);
    assert(rmdir(root) == 0);

    printf(GREEN "PASSED" RESET "\n");
}

/* ========== 用户态兜底：阈值比较 ========== */
void test_gateway_pose_check_threshold(void) {
    printf("Running Pose threshold check tests... ");

    const char *reason = NULL;
    /* 阈值：peak=1.5g, rms=0.7g (静置姿态 az≈1g 时 RMS≈0.577，0.7 留出余量), gyro=30deg/s */
    const float peak = 1.5f, rms = 0.7f, gyro = 30.0f;

    /* 1. 静置正常姿态 —— 不触发 (az≈1g 为重力) */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 0.0f, .accel_y_g = 0.0f, .accel_z_g = 1.0f,
                               .tilt_deg = 0.0f};
        assert(gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason) == 0);
        assert(reason == NULL);
    }

    /* 2. X 轴峰值超标 (>1.5g) */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 2.0f, .accel_y_g = 0.0f, .accel_z_g = 1.0f,
                               .tilt_deg = 5.0f};
        assert(gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason) != 0);
        assert(reason != NULL);
        assert(strstr(reason, "peak") != NULL);
    }

    /* 3. Y 轴负向峰值超标 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 0.0f, .accel_y_g = -2.5f, .accel_z_g = 1.0f,
                               .tilt_deg = 5.0f};
        assert(gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason) != 0);
        assert(strstr(reason, "peak") != NULL);
    }

    /* 4. Z 轴峰值超标 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 0.0f, .accel_y_g = 0.0f, .accel_z_g = 3.0f,
                               .tilt_deg = 5.0f};
        assert(gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason) != 0);
        assert(strstr(reason, "peak") != NULL);
    }

    /* 5. RMS 超标 (三轴均 1.5g, RMS=1.5 > 0.7) 但峰值未超 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 1.4f, .accel_y_g = 1.4f, .accel_z_g = 1.4f,
                               .tilt_deg = 5.0f};
        assert(gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason) != 0);
        assert(strstr(reason, "rms") != NULL);
    }

    /* 6. 倾斜超标 (>30deg) */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 0.0f, .accel_y_g = 0.0f, .accel_z_g = 0.7f,
                               .tilt_deg = 45.0f};
        assert(gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason) != 0);
        assert(strstr(reason, "gyro") != NULL);
    }

    /* 7. 负向倾斜也超标 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 0.0f, .accel_y_g = 0.0f, .accel_z_g = 0.7f,
                               .tilt_deg = -60.0f};
        assert(gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason) != 0);
        assert(strstr(reason, "gyro") != NULL);
    }

    /* 8. invalid pose (valid=0) —— 不触发 */
    {
        gateway_pose_t pose = {.valid = 0,
                               .accel_x_g = 10.0f, .accel_y_g = 10.0f, .accel_z_g = 10.0f,
                               .tilt_deg = 90.0f};
        assert(gateway_pose_check_threshold(&pose, peak, rms, gyro, &reason) == 0);
    }

    /* 9. NULL pose —— 不触发 */
    {
        assert(gateway_pose_check_threshold(NULL, peak, rms, gyro, &reason) == 0);
    }

    /* 10. 阈值为 0 表示关闭检测 */
    {
        gateway_pose_t pose = {.valid = 1,
                               .accel_x_g = 10.0f, .accel_y_g = 10.0f, .accel_z_g = 10.0f,
                               .tilt_deg = 90.0f};
        assert(gateway_pose_check_threshold(&pose, 0.0f, 0.0f, 0.0f, &reason) == 0);
    }

    printf(GREEN "PASSED" RESET "\n");
}

/* ========== ioctl 命令码 ========== */
void test_ioctl_commands(void) {
    printf("Running IOCTL command tests... ");

    /* 验证三个命令码互不相同 */
    assert(GATEWAY_MONITOR_IOC_SET_PEAK_THR != GATEWAY_MONITOR_IOC_SET_RMS_THR);
    assert(GATEWAY_MONITOR_IOC_SET_PEAK_THR != GATEWAY_MONITOR_IOC_SET_GYRO_THR);
    assert(GATEWAY_MONITOR_IOC_SET_RMS_THR != GATEWAY_MONITOR_IOC_SET_GYRO_THR);

    /* 验证设备路径定义 */
    assert(strcmp(GATEWAY_MONITOR_DEVICE, "/dev/gateway_monitor") == 0);

    /* 验证魔数正确 */
    assert(GATEWAY_MONITOR_IOC_MAGIC == 'G');

    /* 验证命令码非零（确保 _IOW 宏正确展开） */
    assert(GATEWAY_MONITOR_IOC_SET_PEAK_THR != 0);
    assert(GATEWAY_MONITOR_IOC_SET_RMS_THR != 0);
    assert(GATEWAY_MONITOR_IOC_SET_GYRO_THR != 0);

    printf(GREEN "PASSED" RESET "\n");
}

/* ========== 告警姿态 JSON ========== */

static cJSON *require_object(cJSON *parent, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    assert(cJSON_IsObject(item));
    return item;
}

static double require_number(cJSON *parent, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    assert(cJSON_IsNumber(item));
    return item->valuedouble;
}

void test_alarm_pose_json(void) {
    printf("Running AlarmPose JSON tests... ");

    gateway_pose_t pose = {
        .accel_x_raw = 1,
        .accel_y_raw = -2,
        .accel_z_raw = 3,
        .accel_scale = 0.00059855f,
        .accel_x_g = 0.1f,
        .accel_y_g = -0.2f,
        .accel_z_g = 0.9f,
        .roll_deg = -12.5f,
        .pitch_deg = 6.25f,
        .tilt_deg = 14.0f,
        .timestamp = 123456,
        .valid = 1,
    };

    char json[1024];
    assert(protocol_encode_alarm_pose("edge-pi-01", 1, &pose, json, sizeof(json)) == 0);

    cJSON *root = cJSON_Parse(json);
    assert(root != NULL);

    cJSON *device_id = cJSON_GetObjectItemCaseSensitive(root, "device_id");
    assert(cJSON_IsString(device_id));
    assert(strcmp(device_id->valuestring, "edge-pi-01") == 0);
    assert((int)require_number(root, "alarm") == 1);
    assert((uint32_t)require_number(root, "timestamp") == 123456);

    cJSON *gateway_pose = require_object(root, "gateway_pose");
    assert((int)require_number(gateway_pose, "valid") == 1);

    cJSON *accel_raw = require_object(gateway_pose, "accel_raw");
    assert((int)require_number(accel_raw, "x") == 1);
    assert((int)require_number(accel_raw, "y") == -2);
    assert((int)require_number(accel_raw, "z") == 3);

    cJSON *accel_g = require_object(gateway_pose, "accel_g");
    assert_close((float)require_number(accel_g, "x"), 0.1f, 0.0001f);
    assert_close((float)require_number(accel_g, "y"), -0.2f, 0.0001f);
    assert_close((float)require_number(accel_g, "z"), 0.9f, 0.0001f);
    assert_close((float)require_number(gateway_pose, "tilt_deg"), 14.0f, 0.0001f);

    cJSON_Delete(root);
    printf(GREEN "PASSED" RESET "\n");
}

int main(void) {
    printf("=== Starting Unit Tests ===\n");
    test_crc16();
    test_ringbuf();
    test_config_thresholds();
    test_config_file_missing();
    test_gateway_pose_snapshot();
    test_gateway_pose_check_threshold();
    test_ioctl_commands();
    test_alarm_pose_json();
    printf("=== All Tests " GREEN "SUCCESS" RESET " ===\n");
    return 0;
}
