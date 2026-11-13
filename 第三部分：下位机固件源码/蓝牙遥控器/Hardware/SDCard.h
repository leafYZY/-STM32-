#ifndef __SDCARD_H
#define __SDCARD_H

#include <stdint.h>

/*
 * SD Card SPI Driver
 * Hardware: SPI1, PA4=CS, PA5=SCK, PA6=MISO, PA7=MOSI
 * Block size: 512 bytes
 */

typedef enum {
	SD_OK = 0,
	SD_ERROR_INIT = 1,
	SD_ERROR_TIMEOUT = 2,
	SD_ERROR_CRC = 3,
	SD_ERROR_WRITE = 4,
	SD_ERROR_READ = 5,
	SD_ERROR_NOCARD = 6
} SD_Result;

extern uint8_t SD_Initialized;

SD_Result SD_Init(void);
SD_Result SD_ReadBlock(uint32_t blockAddr, uint8_t *buf);
SD_Result SD_WriteBlock(uint32_t blockAddr, const uint8_t *buf);

#endif
