#include "ringbuf.h"

void RingBuf_init(RingBuf_t* pbuf, uint8_t* pdata, uint32_t size)
{
    pbuf->head = 0;
    pbuf->tail = 0;
    pbuf->size = size;
    pbuf->pdata = pdata;
}

bool RingBuf_write(RingBuf_t* pbuf, uint8_t data)
{
    uint32_t next_write = pbuf->head + 1;
    if(next_write >= pbuf->size) next_write = 0; // 单纯的判断，比取模运算高效
    if(next_write == pbuf->tail) return false; // 判满

    pbuf->pdata[pbuf->head] = data;
    pbuf->head = next_write;
    return true;
}

bool RingBuf_read(RingBuf_t* pbuf, uint8_t* pdes)
{
    if(pbuf->tail == pbuf->head) return false; // 判空

    uint32_t next_read = pbuf->tail + 1;
    if(next_read >= pbuf->size) next_read = 0;

    *pdes = pbuf->pdata[pbuf->tail];
    pbuf->tail = next_read;
    return true;
}

uint32_t RingBuf_writeblocks(RingBuf_t* pbuf, uint8_t* psrc, uint32_t len)
{
    // 1. 计算当前剩余空间
    uint32_t free_space = (pbuf->tail > pbuf->head) ? (pbuf->tail - pbuf->head - 1) : (pbuf->size - pbuf->head + pbuf->tail - 1);
    
    if (len > free_space) len = free_space; // 空间不足则只写能写的
    if (len == 0) return 0;

    // 2. 计算到缓冲区末尾的距离
    uint32_t first_part = pbuf->size - pbuf->head;

    if (len <= first_part) {
        // 不需要回环，直接拷贝
        memcpy(&pbuf->pdata[pbuf->head], psrc, len);
        pbuf->head += len;
    } else {
        // 需要分成两段拷贝
        memcpy(&pbuf->pdata[pbuf->head], psrc, first_part);
        memcpy(&pbuf->pdata[0], &psrc[first_part], len - first_part);
        pbuf->head = len - first_part;
    }

    if (pbuf->head >= pbuf->size) pbuf->head = 0;
    return len; // 返回实际写入的字节数
}

uint32_t RingBuf_readblocks(RingBuf_t* pbuf, uint8_t* pdes, uint32_t len)
{
    uint32_t data_len = (pbuf->head >= pbuf->tail)? (pbuf->head - pbuf->tail) : (pbuf->size - pbuf->tail + pbuf->head);
    if(len > data_len) len = data_len;
    if(len == 0) return 0;

    uint32_t first_part = pbuf->size - pbuf->tail;
    if(len <= first_part)
    {
        memcpy(pdes, &pbuf->pdata[pbuf->tail], len);
        pbuf->tail += len;
    }
    else
    {
        memcpy(pdes, &pbuf->pdata[pbuf->tail], first_part);
        memcpy(&pdes[first_part], &pbuf->pdata[0], len - first_part);
        pbuf->tail = len - first_part;
    }

    if (pbuf->tail >= pbuf->size) pbuf->tail = 0;
    return len;
}

uint32_t RingBuf_getreadable(RingBuf_t* pbuf)
{
    return (pbuf->head >= pbuf->tail)? (pbuf->head - pbuf->tail) : (pbuf->size - pbuf->tail + pbuf->head);
}