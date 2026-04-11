#ifndef EDGE_RING_H
#define EDGE_RING_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct
{
    uint8_t *pdata;
    uint32_t size;
    volatile uint32_t head; // 写指针
    volatile uint32_t tail; // 读指针
} RingBuf_t;

void RingBuf_init(RingBuf_t *pbuf, uint8_t *pdata, uint32_t size);

bool RingBuf_write(RingBuf_t *pbuf, uint8_t data);

bool RingBuf_read(RingBuf_t *pbuf, uint8_t *pdes);

/**
 * @brief 从缓冲区读取，但不从缓冲区删除
 */
bool RingBuf_peek(RingBuf_t *pbuf, uint8_t *pdes, uint32_t offset);

uint32_t RingBuf_writeblocks(RingBuf_t *pbuf, uint8_t *psrc, uint32_t len);

uint32_t RingBuf_readblocks(RingBuf_t *pbuf, uint8_t *pdes, uint32_t len);

uint32_t RingBuf_getreadable(RingBuf_t *pbuf);

uint32_t RingBuf_skip(RingBuf_t *pbuf, uint32_t len);

#endif