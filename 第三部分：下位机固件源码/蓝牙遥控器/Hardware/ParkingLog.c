#include "stm32f10x.h"
#include <string.h>
#include <stdlib.h>
#include "SDCard.h"
#include "ParkingLog.h"

/*
 * Parking Record Storage Implementation
 *
 * Block 0: Header block - first 4 bytes = record count (little-endian)
 * Block 1+: Data blocks - 16 records per block, 32 bytes each
 *
 * Record N location:
 *   Block  = PARKING_DATA_START + N / RECORDS_PER_BLOCK
 *   Offset = (N % RECORDS_PER_BLOCK) * PARKING_RECORD_SIZE
 */

static uint16_t g_recordCount = 0;
static uint8_t  g_sdReady = 0;

/* 512-byte buffer for block read/modify/write */
static uint8_t g_blockBuf[512];

uint8_t ParkingLog_Init(void)
{
	SD_Result res;

	res = SD_Init();
	if (res != SD_OK)
	{
		g_sdReady = 0;
		g_recordCount = 0;
		return 0;
	}

	g_sdReady = 1;

	/* Read header block to get record count */
	res = SD_ReadBlock(PARKING_HEADER_BLOCK, g_blockBuf);
	if (res != SD_OK)
	{
		g_recordCount = 0;
		return 0;
	}

	/* Extract record count from first 2 bytes (little-endian, max 65535) */
	g_recordCount = (uint16_t)g_blockBuf[0] | ((uint16_t)g_blockBuf[1] << 8);

	/* Sanity check: if count is unreasonably large, reset to 0 */
	if (g_recordCount > 60000)
		g_recordCount = 0;

	return 1;
}

uint8_t ParkingLog_IsReady(void)
{
	return g_sdReady;
}

uint16_t ParkingLog_GetCount(void)
{
	return g_recordCount;
}

uint8_t ParkingLog_AddRecord(const char *plate, uint8_t h, uint8_t m, uint8_t s, uint8_t space)
{
	uint32_t blockAddr;
	uint16_t offset;
	uint16_t recordIdx;
	SD_Result res;

	if (!g_sdReady)
		return 0;

	if (plate == NULL)
		return 0;

	recordIdx = g_recordCount;

	/* Calculate block and offset for this record */
	blockAddr = PARKING_DATA_START + (recordIdx / RECORDS_PER_BLOCK);
	offset = (recordIdx % RECORDS_PER_BLOCK) * PARKING_RECORD_SIZE;

	/* If this record is not at offset 0, we need to read the existing block first */
	if (offset != 0)
	{
		res = SD_ReadBlock(blockAddr, g_blockBuf);
		if (res != SD_OK)
			return 0;
	}
	else
	{
		/* New block: clear buffer */
		memset(g_blockBuf, 0, 512);
	}

	/* Fill in the record at the correct offset */
	ParkingRecord_t *rec = (ParkingRecord_t *)(&g_blockBuf[offset]);
	memset(rec, 0, sizeof(ParkingRecord_t));

	/* Copy plate (max 7 chars + null) */
	strncpy(rec->plate, plate, 7);
	rec->plate[7] = '\0';

	rec->hour = h;
	rec->minute = m;
	rec->second = s;
	rec->space = space;

	/* Write the block back */
	res = SD_WriteBlock(blockAddr, g_blockBuf);
	if (res != SD_OK)
		return 0;

	/* Update record count */
	g_recordCount++;

	/* Write updated record count to header block */
	memset(g_blockBuf, 0, 512);
	g_blockBuf[0] = (uint8_t)(g_recordCount & 0xFF);
	g_blockBuf[1] = (uint8_t)((g_recordCount >> 8) & 0xFF);

	res = SD_WriteBlock(PARKING_HEADER_BLOCK, g_blockBuf);
	if (res != SD_OK)
	{
		/* Record was written but count update failed.
		 * The record is still on the card, count may be stale.
		 * On next init, the count will reflect what was saved. */
		return 0;
	}

	return 1;
}

uint8_t ParkingLog_GetRecord(uint16_t index, ParkingRecord_t *record)
{
	uint32_t blockAddr;
	uint16_t offset;
	SD_Result res;

	if (!g_sdReady || record == NULL || index >= g_recordCount)
		return 0;

	blockAddr = PARKING_DATA_START + (index / RECORDS_PER_BLOCK);
	offset = (index % RECORDS_PER_BLOCK) * PARKING_RECORD_SIZE;

	res = SD_ReadBlock(blockAddr, g_blockBuf);
	if (res != SD_OK)
		return 0;

	memcpy(record, &g_blockBuf[offset], sizeof(ParkingRecord_t));

	return 1;
}

uint8_t ParkingLog_AddFromBLE(const char *packet)
{
	/*
	 * Expected packet format: "@P<plate>|<HHMMSS>|<space>#*"
	 * Example: "@PA888888|200530|03#*"
	 *
	 * plate:    1-7 alphanumeric chars
	 * HHMMSS:   6 digits (hour, minute, second)
	 * space:    1-2 digits (parking space number: 1-3=L1层1~3号, 4-6=L2层1~3号)
	 */
	const char *p;
	char plate[8];
	uint8_t hour, minute, second, space;
	uint8_t i;

	if (packet == NULL || packet[0] != '@' || packet[1] != 'P')
		return 0;

	/* Skip "@P" */
	p = packet + 2;

	/* Parse plate: read until '|' or '#' */
	i = 0;
	while (*p && *p != '|' && *p != '#' && i < 7)
	{
		plate[i] = *p;
		i++;
		p++;
	}
	plate[i] = '\0';

	if (i == 0 || *p != '|')
		return 0;

	p++;  /* skip '|' */

	/* Parse time HHMMSS (6 digits) */
	if (p[0] < '0' || p[0] > '9' ||
		p[1] < '0' || p[1] > '9' ||
		p[2] < '0' || p[2] > '9' ||
		p[3] < '0' || p[3] > '9' ||
		p[4] < '0' || p[4] > '9' ||
		p[5] < '0' || p[5] > '9')
		return 0;

	hour   = (p[0] - '0') * 10 + (p[1] - '0');
	minute = (p[2] - '0') * 10 + (p[3] - '0');
	second = (p[4] - '0') * 10 + (p[5] - '0');
	p += 6;

	if (*p != '|')
		return 0;

	p++;  /* skip '|' */

	/* Parse space number (1-2 digits until '#' or '*') */
	space = 0;
	if (*p >= '0' && *p <= '9')
	{
		space = *p - '0';
		p++;
		if (*p >= '0' && *p <= '9')
		{
			space = space * 10 + (*p - '0');
		}
	}

	/* Validate values */
	if (hour > 23 || minute > 59 || second > 59)
		return 0;

	return ParkingLog_AddRecord(plate, hour, minute, second, space);
}

/*
 * Clear all parking records.
 * - Resets header block (Block 0) so count = 0
 * - Clears first data block (Block 1) so any partial/garbage record is wiped
 * - Updates in-memory count to 0
 */
uint8_t ParkingLog_ClearAll(void)
{
	SD_Result res;

	if (!g_sdReady)
		return 0;

	/* Clear header block (Block 0): record count = 0 */
	memset(g_blockBuf, 0, 512);
	res = SD_WriteBlock(PARKING_HEADER_BLOCK, g_blockBuf);
	if (res != SD_OK)
		return 0;

	/* Clear first data block (Block 1): wipe any existing record slot */
	memset(g_blockBuf, 0, 512);
	res = SD_WriteBlock(PARKING_DATA_START, g_blockBuf);
	if (res != SD_OK)
		return 0;

	/* Update in-memory count */
	g_recordCount = 0;

	return 1;
}
