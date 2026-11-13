#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

char Serial_RxPacket[100];				//QR模块接收数据包数组
uint8_t Serial_RxFlag;					//QR模块接收数据包标志位
static uint8_t pRxPacket = 0;				//接收数据位置

/**
  * 函    数：串口初始化（USART3，接二维码模块，PB10/PB11）
  * 参    数：无
  * 返 回 值：无
  */
void Serial_Init(void)
{
	pRxPacket = 0;
	
	/*开启时钟*/
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);	//USART3在APB1上
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);	//开启GPIOB时钟
	
	/*GPIO初始化*/
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;				//PB10 = USART3_TX
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;				//PB11 = USART3_RX
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	/*USART初始化*/
	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate = 9600;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_Init(USART3, &USART_InitStructure);
	
	/*NVIC中断分组（只需设置一次，若已在main中设置可删除此行）*/
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	
	/*中断输出配置*/
	USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
	
	/*NVIC配置*/
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructure);
	
	/*USART使能*/
	USART_Cmd(USART3, ENABLE);
}

/**
  * 函    数：串口发送一个字节
  */
void Serial_SendByte(uint8_t Byte)
{
	USART_SendData(USART3, Byte);
	while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
}

/**
  * 函    数：串口发送字符串
  */
void Serial_SendString(char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i++)
	{
		Serial_SendByte(String[i]);
	}
}

/**
  * 函    数：封装printf函数
  */
void Serial_Printf(char *format, ...)
{
	char String[100];
	va_list arg;
	va_start(arg, format);
	vsprintf(String, format, arg);
	va_end(arg);
	Serial_SendString(String);
}

/**
  * 函    数：USART3中断函数（二维码模块数据接收）
  * 说    明：QR模块以'.'作为数据包结束符
  *          修复3个Bug：
  *          1. 添加null终止符，确保strcmp正确比较
  *          2. 过滤\r\n等控制字符，防止残留到下一包数据
  *          3. Serial_RxFlag==1时丢弃新数据，防止覆盖未处理的数据
  *          4. 增加缓冲区溢出保护
  */
void USART3_IRQHandler(void)
{
	if (USART_GetITStatus(USART3, USART_IT_RXNE) == SET)
	{
		uint8_t RxData = USART_ReceiveData(USART3);

		/*主循环未处理完上一包数据时，丢弃新数据*/
		if (Serial_RxFlag == 0)
		{
			if (RxData == '.')
			{
				/*添加null终止符，确保strcmp正确比较*/
				Serial_RxPacket[pRxPacket] = '\0';
				Serial_RxFlag = 1;
				pRxPacket = 0;
			}
			else if (RxData != '\r' && RxData != '\n')
			{
				/*过滤回车换行等控制字符，防止残留到下一包数据*/
				if (pRxPacket < 99)
				{
					Serial_RxPacket[pRxPacket++] = RxData;
				}
			}
		}

		USART_ClearITPendingBit(USART3, USART_IT_RXNE);
	}
}
