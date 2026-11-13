#include "stm32f10x.h"
#include "Delay.h"
void LED_Init(void)
{
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	
	
	GPIO_InitTypeDef GPIO_InitStructure;				
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5|GPIO_Pin_4|GPIO_Pin_7|GPIO_Pin_6;			
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;		
	
	GPIO_Init(GPIOA, &GPIO_InitStructure);		


}

void LED0_OFF(void)//G
{
	
GPIO_ResetBits(GPIOA,GPIO_Pin_4);
	
}

void LED0_ON(void)
{
	
GPIO_SetBits(GPIOA,GPIO_Pin_4);

}

void LED1_OFF(void)//Y
{
	
GPIO_ResetBits(GPIOA,GPIO_Pin_5);
	
}

void LED1_ON(void)
{
	
GPIO_SetBits(GPIOA,GPIO_Pin_5);

}

void LED2_OFF(void)//R
{
	
GPIO_ResetBits(GPIOA,GPIO_Pin_6);
	
}

void LED2_ON(void)
{
	
GPIO_SetBits(GPIOA,GPIO_Pin_6);

}


void LED10_ON(void)
{

GPIO_ResetBits(GPIOA,GPIO_Pin_7);

}

void LED10_OFF(void)
{

GPIO_SetBits(GPIOA,GPIO_Pin_7);

}
