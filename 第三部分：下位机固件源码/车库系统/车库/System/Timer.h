#ifndef __TIMER_H
#define __TIMER_H

#include "stm32f10x.h"

/*全局时间变量（在TIM2中断中自动更新）*/
extern uint8_t  Timer_Hour;		/*时 0~23*/
extern uint8_t  Timer_Minute;	/*分 0~59*/
extern uint8_t  Timer_Second;	/*秒 0~59*/
extern uint16_t Timer_MilliSec;	/*毫秒 0~999*/

/**
  * 函    数：定时器初始化（TIM2，1ms中断）
  * 参    数：无
  * 返 回 值：无
  * 说    明：使用TIM2产生1ms周期中断，自动维护时/分/秒/毫秒
  *          NVIC分组需在main中先调用 NVIC_PriorityGroupConfig()
  */
void Timer_Init(void);

#endif
