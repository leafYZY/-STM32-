#ifndef __KEY_H
#define __KEY_H

/* 按键编码 */
#define KEY_NONE		0
#define KEY_SW1			1	// PB0
#define KEY_SW2			2	// PB1
#define KEY_UP			3	// PA11
#define KEY_DOWN		4	// PA12
#define KEY_LEFT		5	// PB5
#define KEY_RIGHT		6	// PB6
#define KEY_OK			7	// PB7
#define KEY_SW3_LEFT	8	// PA8 左拨
#define KEY_SW3_RIGHT	9	// PA8 右拨

void Key_Init(void);
uint8_t Key_GetNum(void);
uint8_t SW3_GetState(void);
uint8_t SW3_GetEvent(void);
void SW3_InitState(void);

#endif
