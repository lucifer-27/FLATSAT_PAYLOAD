#ifndef SD_SPI_H
#define SD_SPI_H

#include "stdint.h"

uint8_t SD_Init(void);

uint8_t SD_ReadBlock(uint32_t block,
                     uint8_t *buffer);

uint8_t SD_WriteBlock(uint32_t block,
                      const uint8_t *buffer);

#endif
