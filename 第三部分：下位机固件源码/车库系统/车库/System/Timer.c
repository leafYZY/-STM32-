#include "stm32f10x.h"
#include "Timer.h"

/*全局时间变量*/
uint8_t  Timer_Hour    = 20;	/*默认初始时间 20:00:00*/
uint8_t  Timer_Minute  = 0;
uint8_t  Timer_Second  = 0;
uint16_t Timer_MilliSec = 0;

/**
  * 函    数：定时器初始化（TIM2，1ms中断）
  * 参    数：无
  * 返 回 值：无
  * 说    明：TIM2时钟 = 72MHz
  *          PSC = 72-1  → 计数频率 = 72MHz/72 = 1MHz（1us/tick）
  *          ARR = 1000-1 → 周期 = 1000us = 1ms
  *          每次中断 Timer_MilliSec++，满1000进位到秒
  */
void Timer_Init(void)
{
	/*开启时钟*/
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	/*选择内部时钟源*/
	TIM_InternalClockConfig(TIM2);

	/*时基单元配置*/
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_Period = 1000 - 1;		/*ARR，1ms周期*/
	TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1;		/*PSC，1us计数*/
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

	/*清除更新标志位，避免一启动就进中断*/
	TIM_ClearFlag(TIM2, TIM_FLAG_Update);

	/*使能更新中断*/
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

	/*NVIC配置*/
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;	/*抢占优先级2（低于串口）*/
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;			/*响应优先级1*/
	NVIC_Init(&NVIC_InitStructure);

	/*启动定时器*/
	TIM_Cmd(TIM2, ENABLE);
}

/**
  * 函    数：TIM2中断服务函数
  * 说    明：每1ms进入一次，维护时/分/秒/毫秒
  */
void TIM2_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
	{
		Timer_MilliSec++;
		if (Timer_MilliSec >= 1000)
		{
			Timer_MilliSec = 0;
			Timer_Second++;
			if (Timer_Second > 59)
			{
				Timer_Second = 0;
				Timer_Minute++;
				if (Timer_Minute > 59)
				{
					Timer_Minute = 0;
					Timer_Hour++;
					if (Timer_Hour > 23)
					{
						Timer_Hour = 0;
					}
				}
			}
		}

		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	}
}
