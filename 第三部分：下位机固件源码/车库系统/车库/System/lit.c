#include "stm32f10x.h"

/**
  * 函    数：智能路灯初始化（使用PA7，与LED.c中LED10一致）
  * 说    明：GM_Get()==0（黑夜/阴暗）时，PA7输出低电平点亮路灯
  *          GM_Get()==1（白天）时，PA7输出高电平关闭路灯
  *          此文件仅提供初始化，亮灭控制在main.c中统一处理
  */
void Lit_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;		/*PA7 = 智能路灯LED*/
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	/*默认关闭路灯（PA7高电平）*/
	GPIO_SetBits(GPIOA, GPIO_Pin_7);
}
