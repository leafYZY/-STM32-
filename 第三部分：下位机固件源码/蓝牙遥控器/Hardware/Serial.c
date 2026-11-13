#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <stdarg.h>

char Serial_RxPacket[100];				//"@MSG\r\n"
uint8_t Serial_RxFlag;

void Serial_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	USART_InitTypeDef USART_InitStructure; 
	USART_InitStructure.USART_BaudRate = 9600;           // 波特率 9600
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;   // 无硬件流控
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;    // 开启发送(Tx)和接收(Rx)
	USART_InitStructure.USART_Parity = USART_Parity_No;          // 无校验位
	USART_InitStructure.USART_StopBits = USART_StopBits_1;       // 1个停止位
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;  // 8个数据位
	USART_Init(USART1, &USART_InitStructure);
	
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);     //开启串口的“接收缓冲区非空中断”
	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);    // 设置中断分组为 2
	
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;  // 中断通道为 USART1 中断
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;    // 使能该中断通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;   // 抢占优先级 1
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;          // 子优先级 1
	NVIC_Init(&NVIC_InitStructure);
	
	USART_Cmd(USART1, ENABLE);       //开启串口
}

void Serial_SendByte(uint8_t Byte)
{
	USART_SendData(USART1, Byte);
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
}

void Serial_SendArray(uint8_t *Array, uint16_t Length)
{
	uint16_t i;
	for (i = 0; i < Length; i ++)
	{
		Serial_SendByte(Array[i]);
	}
}

void Serial_SendString(const char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i ++)
	{
		Serial_SendByte(String[i]);
	}
}

uint32_t Serial_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y --)
	{
		Result *= X;
	}
	return Result;
}

void Serial_SendNumber(uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i ++)
	{
		Serial_SendByte(Number / Serial_Pow(10, Length - i - 1) % 10 + '0');
	}
}

int fputc(int ch, FILE *f)
{
	Serial_SendByte(ch);
	return ch;
}

void Serial_Printf(const char *format, ...)
{
	char String[100];
	va_list arg;
	va_start(arg, format);
	vsprintf(String, format, arg);
	va_end(arg);
	Serial_SendString(String);
}

void USART1_IRQHandler(void)
{
	static uint8_t RxState = 0;
	static uint8_t pRxPacket = 0;
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
	{
		uint8_t RxData = USART_ReceiveData(USART1);
		
		if (RxState == 0)
		{
			if (RxData == '@' && Serial_RxFlag == 0)
			{
				RxState = 1;
				pRxPacket = 0;
				Serial_RxPacket[pRxPacket] = '@';
				pRxPacket ++;
			}
		}
		else if (RxState == 1)
		{
			if (RxData == '*')
			{
				if (pRxPacket < sizeof(Serial_RxPacket) - 1)
				{
					Serial_RxPacket[pRxPacket] = '*';
					pRxPacket ++;
				}
				Serial_RxPacket[pRxPacket] = '\0';
				RxState = 0;
				Serial_RxFlag = 1;
			}
			else if (RxData == '\r')
			{
				RxState = 2;
			}
			else if (pRxPacket < sizeof(Serial_RxPacket) - 1)
			{
				Serial_RxPacket[pRxPacket] = RxData;
				pRxPacket ++;
			}
		}
		else if (RxState == 2)
		{
			if (RxData == '\n')
			{
				Serial_RxPacket[pRxPacket] = '\0';
				RxState = 0;
				Serial_RxFlag = 1;
			}
			else
			{
				RxState = 0;
			}
		}
		
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}
}
