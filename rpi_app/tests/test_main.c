#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ringbuf.h"
#include "crc16.h"

// 颜色输出宏 (Linux/macOS 终端)
#define GREEN "\033[0;32m"
#define RED   "\033[0;31m"
#define RESET "\033[0m"

// --- CRC16 模块测试 ---
void test_crc16(void) {
    printf("Running CRC16 tests... ");
    
    // 典型的 Modbus RTU 报文: [01 03 00 00 00 01]
    // 预期 CRC16: 0x840A (注意：Modbus 规范中 0x84 为低字节，0x0A 为高字节)
    uint8_t modbus_msg[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t result = crc_calculate(modbus_msg, 6);
    
    // 根据你的 crc16.c 实现: return (CRCLo | (CRCHi << 8));
    // 如果 CRCLo=0x84, CRCHi=0x0A, 结果应为 0x0A84
    assert(result == 0x0A84); 
    
    printf(GREEN "PASSED" RESET "\n");
}

// --- RingBuf 模块测试 ---
void test_ringbuf(void) {
    printf("Running RingBuf tests... ");
    
    uint8_t raw_buffer[10]; // 长度为 10 的缓冲区
    RingBuf_t rb;
    RingBuf_init(&rb, raw_buffer, 10);

    // 1. 初始状态检查
    assert(RingBuf_getreadable(&rb) == 0);
    uint8_t tmp;
    assert(RingBuf_read(&rb, &tmp) == false); // 空缓冲区读取应失败

    // 2. 单字节写满检查 (size=10, 实际上只能存 9 个，保留一个位置区分空满)
    for(uint8_t i = 0; i < 9; i++) {
        assert(RingBuf_write(&rb, i) == true);
    }
    assert(RingBuf_write(&rb, 99) == false); // 第 10 个应该写不进去了
    assert(RingBuf_getreadable(&rb) == 9);

    // 3. 块读取检查
    uint8_t read_out[10];
    uint32_t read_len = RingBuf_readblocks(&rb, read_out, 5);
    assert(read_len == 5);
    assert(read_out[0] == 0);
    assert(read_out[4] == 4);
    assert(RingBuf_getreadable(&rb) == 4);

    // 4. 回环 (Wrap-around) 写入测试
    // 此时 tail=5, head=9。剩余空间应为 5 (计算: 10-9 + 5-1 = 5)
    uint8_t data_to_wrap[] = {10, 11, 12, 13};
    uint32_t write_len = RingBuf_writeblocks(&rb, data_to_wrap, 4);
    assert(write_len == 4);
    // 逻辑：数据 10 存在索引 9，数据 11, 12, 13 存在索引 0, 1, 2。新的 head 应为 3
    assert(rb.head == 3); 

    // 5. 回环读取测试
    uint8_t final_out[8];
    uint32_t final_read_len = RingBuf_readblocks(&rb, final_out, 8);
    assert(final_read_len == 8);
    assert(final_out[3] == 8);  // 原始剩余的最后一个
    assert(final_out[4] == 10); // 回环后的第一个
    assert(final_out[7] == 13);
    
    printf(GREEN "PASSED" RESET "\n");
}

int main(void) {
    printf("=== Starting Unit Tests ===\n");
    test_crc16();
    test_ringbuf();
    printf("=== All Tests " GREEN "SUCCESS" RESET " ===\n");
    return 0;
}