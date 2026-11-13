#ifndef __PARKINGLOG_H
#define __PARKINGLOG_H

#include <stdint.h>

/*
 * Parking Record Storage Module
 * Uses SD card to store parking entries received via Bluetooth
 *
 * Record format (32 bytes):
 *   plate[8]  - license plate (null-terminated, max 7 chars)
 *   hour      - entry hour (0-23)
 *   minute    - entry minute (0-59)
 *   second    - entry second (0-59)
 *   space     - parking space number (1-3=L1层1~3号, 4-6=L2层1~3号, 0=unassigned)
 *   reserved[20] - reserved for future use
 *
 * Storage layout on SD card:
 *   Block 0:     Header (4-byte record count + padding)
 *   Block 1+:    Records (16 records per 512-byte block)
 */

#define PARKING_RECORD_SIZE   32
#define RECORDS_PER_BLOCK     16
#define PARKING_HEADER_BLOCK  0
#define PARKING_DATA_START    1

typedef struct {
	char     plate[8];    /* License plate, null-terminated */
	uint8_t  hour;        /* Entry hour 0-23 */
	uint8_t  minute;      /* Entry minute 0-59 */
	uint8_t  second;      /* Entry second 0-59 */
	uint8_t  space;       /* Parking space 1-3=L1层1~3号, 4-6=L2层1~3号, 0=unassigned */
	uint8_t  reserved[20];/* Reserved */
} ParkingRecord_t;

/* Initialize parking log (calls SD_Init internally) */
uint8_t ParkingLog_Init(void);

/* Add a parking record */
uint8_t ParkingLog_AddRecord(const char *plate, uint8_t h, uint8_t m, uint8_t s, uint8_t space);

/* Get a parking record by index (0-based). Returns 1 on success, 0 on failure */
uint8_t ParkingLog_GetRecord(uint16_t index, ParkingRecord_t *record);

/* Get total record count */
uint16_t ParkingLog_GetCount(void);

/* Parse BLE packet "@P<plate>|<HHMMSS>|<space>#*" and store record */
/* Returns 1 on success, 0 on failure */
uint8_t ParkingLog_AddFromBLE(const char *packet);

/* Check if SD card is initialized */
uint8_t ParkingLog_IsReady(void);

/* Clear all parking records (resets Block 0 count + clears Block 1+) */
/* Returns 1 on success, 0 on failure (SD not ready or write failed) */
uint8_t ParkingLog_ClearAll(void);

#endif
