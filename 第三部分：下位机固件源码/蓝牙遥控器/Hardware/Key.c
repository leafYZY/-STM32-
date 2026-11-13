#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "Key.h"

/* 按键GPIO定义 */
#define SW1_GPIO	GPIOB
#define SW1_PIN		GPIO_Pin_0
#define SW2_GPIO	GPIOB
#define SW2_PIN		GPIO_Pin_1
#define KEY_UP_GPIO	GPIOA
#define KEY_UP_PIN	GPIO_Pin_11
#define KEY_DOWN_GPIO	GPIOA
#define KEY_DOWN_PIN	GPIO_Pin_12
#define KEY_LEFT_GPIO	GPIOB
#define KEY_LEFT_PIN	GPIO_Pin_5
#define KEY_RIGHT_GPIO	GPIOB
#define KEY_RIGHT_PIN	GPIO_Pin_6
#define KEY_OK_GPIO	GPIOB
#define KEY_OK_PIN	GPIO_Pin_7
#define SW3_GPIO	GPIOA
#define SW3_PIN		GPIO_Pin_8

static uint8_t SW3_LastState = 0;

void Key_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	
	GPIO_InitStructure.GPIO_Pin = SW1_PIN | SW2_PIN | KEY_LEFT_PIN | KEY_RIGHT_PIN | KEY_OK_PIN;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Pin = KEY_UP_PIN | KEY_DOWN_PIN | SW3_PIN;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

static uint8_t Key_ScanOnce(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin, uint8_t KeyCode)
{
	if (GPIO_ReadInputDataBit(GPIOx, GPIO_Pin) == 0)
	{
		Delay_ms(20);
		if (GPIO_ReadInputDataBit(GPIOx, GPIO_Pin) == 0)
		{
			while (GPIO_ReadInputDataBit(GPIOx, GPIO_Pin) == 0);
			Delay_ms(20);
			return KeyCode;
		}
	}
	return 0;
}

uint8_t Key_GetNum(void)
{
	uint8_t KeyNum;
	
	KeyNum = Key_ScanOnce(SW2_GPIO, SW2_PIN, KEY_SW2);  // SW2优先级最高，任何状态都有效
	if (KeyNum) return KeyNum;
	
	KeyNum = Key_ScanOnce(SW1_GPIO, SW1_PIN, KEY_SW1);
	if (KeyNum) return KeyNum;
	
	KeyNum = Key_ScanOnce(KEY_UP_GPIO, KEY_UP_PIN, KEY_UP);
	if (KeyNum) return KeyNum;
	
	KeyNum = Key_ScanOnce(KEY_DOWN_GPIO, KEY_DOWN_PIN, KEY_DOWN);
	if (KeyNum) return KeyNum;
	
	KeyNum = Key_ScanOnce(KEY_LEFT_GPIO, KEY_LEFT_PIN, KEY_LEFT);
	if (KeyNum) return KeyNum;
	
	KeyNum = Key_ScanOnce(KEY_RIGHT_GPIO, KEY_RIGHT_PIN, KEY_RIGHT);
	if (KeyNum) return KeyNum;
	
	KeyNum = Key_ScanOnce(KEY_OK_GPIO, KEY_OK_PIN, KEY_OK);
	if (KeyNum) return KeyNum;
	
	return 0;
}

uint8_t SW3_GetState(void)
{
	// SW3拨动开关：PA8
	// 根据网表，SW3.1=GND，SW3.2=PA8，SW3.3=V_BATT
	// 左拨（接GND）读取为0；右拨（接V_BATT）读取为1
	return GPIO_ReadInputDataBit(SW3_GPIO, SW3_PIN);
//	SW3_GPIO, SW3_PIN
}

uint8_t SW3_GetEvent(void)
{
	uint8_t state = SW3_GetState();
	if (state != SW3_LastState)
	{
		SW3_LastState = state;
		if (state)
			return KEY_SW3_LEFT;   // 左拨 -> 蓝牙模式 
		else
			return KEY_SW3_RIGHT;    // 右拨 -> 自动扫码模式 
	}
	return 0;
}

void SW3_InitState(void)
{
	SW3_LastState = SW3_GetState();
}
