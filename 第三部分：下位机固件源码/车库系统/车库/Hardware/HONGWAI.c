#include "stm32f10x.h"

void HONGWAI_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
	
	GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

	/*PB0 = 光敏电阻 GM（智能路灯）*/
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	/*PA0 = 闸口车辆通过传感器 传感器2（车辆离开闸口，关栏杆）*/
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/*PA1 = 闸口车辆到达传感器 传感器1（车辆到达闸口，触发扫码）*/
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/*PB12~PB15 = 车位红外传感器 S_OUT1~S_OUT3 + S_OUT6（L1层1~3号 + L2层3号）*/
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15 | GPIO_Pin_3;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	/*PA11~PA12 = 车位红外传感器 S_OUT5~S_OUT4（L2层2号 + L2层1号）*/
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

/*闸口车辆通过传感器 传感器2（车辆离开，关栏杆）PA0*/
uint8_t HW1_Get(void)
{
	return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0);
}

/*闸口车辆到达传感器 传感器1（车辆到达，触发扫码）PA1*/
uint8_t HW2_Get(void)
{
	return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1);
}

/*光敏电阻 GM：1=白天 0=黑夜/阴暗 PB0*/
uint8_t GM_Get(void)
{
	return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0);
}

/*车库车位传感器 S_OUT1~S_OUT6*/
uint8_t Space1_Get(void) { return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_12); }	/*S_OUT1 L1层1号车位*/
uint8_t Space2_Get(void) { return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13); }	/*S_OUT2 L1层2号车位*/
uint8_t Space3_Get(void) { return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14); }	/*S_OUT3 L1层3号车位*/
uint8_t Space4_Get(void) { return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_15); }	/*S_OUT4 L2层4号车位*/
uint8_t Space5_Get(void) { return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_15); }	/*S_OUT5 L2层5号车位*/
uint8_t Space6_Get(void) { return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_3); }	/*S_OUT6 L2层6号车位*/
