#ifndef __HONGWAI_H
#define __HONGWAI_H

#include <stdint.h>

void HONGWAI_Init(void);

/*闸口传感器（传感器1/传感器2）*/
uint8_t HW1_Get(void);	/*PA0 传感器2 车辆通过闸口（离开）*/
uint8_t HW2_Get(void);	/*PA1 传感器1 车辆到达闸口*/

/*光敏电阻*/
uint8_t GM_Get(void);		/*PB0 GM  1=白天 0=黑夜*/

/*车库6个车位红外传感器（S_OUT1~S_OUT6）*/
uint8_t Space1_Get(void);	/*PB12 S_OUT1 L1层1号车位*/
uint8_t Space2_Get(void);	/*PB13 S_OUT2 L1层2号车位*/
uint8_t Space3_Get(void);	/*PB14 S_OUT3 L1层3号车位*/
uint8_t Space4_Get(void);	/*PA12 S_OUT4 L2层4号车位*/
uint8_t Space5_Get(void);	/*PA11 S_OUT5 L2层5号车位*/
uint8_t Space6_Get(void);	/*PB15 S_OUT6 L2层6号车位*/

#endif
