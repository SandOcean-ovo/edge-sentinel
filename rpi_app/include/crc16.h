#ifndef EDGE_CRC16_H
#define EDGE_CRC16_H

#include <stdint.h>


extern const uint8_t TblCRCHi[];

extern const uint8_t TblCRCLo[];

uint16_t crc_calculate(uint8_t* pdata, uint8_t len);


#endif