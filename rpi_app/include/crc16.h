#ifndef EDGE_CRC16_H
#define EDGE_CRC16_H

#include <stdint.h>

uint16_t crc_calculate(const uint8_t* pdata, uint8_t len);

#endif
