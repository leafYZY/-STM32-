#ifndef __BLE_SERIAL_H
#define __BLE_SERIAL_H

#include <stdint.h>

extern char BLE_RxPacket[100];
extern uint8_t BLE_RxFlag;

void BLE_Serial_Init(void);
void BLE_Serial_SendString(char *Buf);

#endif
