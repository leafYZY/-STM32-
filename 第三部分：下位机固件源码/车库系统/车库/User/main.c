#include "stm32f10x.h"                  
#include "Delay.h"
#include "OLED.h"
#include "LED.h"
#include "Servo.h"
#include "KEY.h"
#include "PlateDB.h"	/*车牌库：车牌 + 余额（内存版）*/
#include "HONGWAI.h"
#include "Serial.h"		/*二维码模块 USART3 PB10/PB11*/
#include "BLE_Serial.h"	/*蓝牙模块 USART2 PA2/PA3*/
#include "Voice.h"		/*语音合成模块 USART1 PA9/PA10*/
#include "Lit.h"			/*智能路灯初始化*/
#include "Timer.h"		/*TIM2定时器 1ms中断 精确计时*/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*===== OLED 显示区域定义 =====*/
#define ROW_TITLE    0    /* Y=0~15:  标题栏 */
#define ROW_BAL      16   /* Y=16~31: 余额/空位 */
#define ROW_PLATE    32   /* Y=32~47: 车牌/状态消息 */
#define ROW_TIME     48   /* Y=48~55: 日期时间(6x8) */
#define ROW_SYS      56   /* Y=56~63: 系统状态(6x8) */

#define MSG_X        32   /* 状态消息起始X */
#define MSG_W        96   /* 状态消息宽度 128-32=96 */
#define MSG_H        16   /* 状态消息高度 */

#define BAL_X        32   /* 余额数字起始X(余：占32px) */
#define BAL_W        24   /* 余额数字宽度(3位x8) */
#define SPC_X        90   /* 空位数字起始X(空：占32px,58+32=90) */

/*全局变量*/
/*车牌与余额统一由 PlateDB.c 管理：PlateDB_Find / PlateDB_GetBalance / PlateDB_AddBalance*/
uint8_t Angle = 10;		/*舵机角度 10=关 100=开（已对调：开闸=100 关闸=10，上电初始为关闸状态）*/
uint8_t gate_state = 0;	/*0=空闲红灯 1=等待黄灯 2=开闸绿灯*/
char current_plate[20];	/*当前识别到的车牌号*/
uint8_t work_mode = 1;	/*工作模式：0=遥控模式(蓝牙)  1=自动模式  默认自动*/
uint8_t sensor_enable[6] = {1,1,1,1,1,1};	/*车位传感器使能: 1=启用 0=禁用(遥控器可远程切换)*/
uint8_t last_ble_sec = 0;		/*最近一次收到蓝牙数据的时刻（Timer_Second）*/
uint8_t remote_open_sec = 0;	/*蓝牙开闸的时刻（Timer_Second）*/
uint8_t remote_open_flag = 0;	/*1=闸门由蓝牙打开，需要超时保护*/

/*时间（用定时器替代延时，此处暂用简单计数）*/
uint8_t H = 20, M = 0, S = 0;
int A = 2025, B = 8, C = 15;

/*OLED刷新标志*/
uint8_t oled_dirty = 0;       /*标记需要刷新OLED*/
uint8_t prev_mode = 0xFF;     /*记录上次模式(用于检测变化)*/

/*计算 Timer_Second（0~59 循环）从 start 到 now 的秒数，处理 59->0 回绕*/
uint8_t Sec_Elapsed(uint8_t start, uint8_t now)
{
	if (now >= start) return (uint8_t)(now - start);
	return (uint8_t)(60 - start + now);
}

/*检测剩余车位数量*/
uint8_t Get_FreeSpaces(void)
{
	uint8_t count = 0;
	if (sensor_enable[0] && Space1_Get() == 1) count++;
	if (sensor_enable[1] && Space2_Get() == 1) count++;
	if (sensor_enable[2] && Space3_Get() == 1) count++;
	if (sensor_enable[3] && Space4_Get() == 1) count++;
	if (sensor_enable[4] && Space5_Get() == 1) count++;
	if (sensor_enable[5] && Space6_Get() == 1) count++;
	return count;
}

/*推荐最近空闲车位，返回1~6号，0表示无空闲*/
uint8_t Recommend_Space(void)
{
	if (sensor_enable[0] && Space1_Get() == 1) return 1;
	if (sensor_enable[1] && Space2_Get() == 1) return 2;
	if (sensor_enable[2] && Space3_Get() == 1) return 3;
	if (sensor_enable[3] && Space4_Get() == 1) return 4;
	if (sensor_enable[4] && Space5_Get() == 1) return 5;
	if (sensor_enable[5] && Space6_Get() == 1) return 6;
	return 0;
}

/*语音播报封装（简易文本发送，按LU6288模块手册调整帧格式）*/
void Say(char *msg)
{
	char b[80];
	sprintf(b, "<G>%s", msg);
	Voice_Play(b);
}

/*===== OLED显示辅助函数 =====*/

/*绘制静态UI框架（仅在初始化时调用一次）*/
void OLED_DrawStaticUI(void)
{
	/*清屏*/
	OLED_Clear();
	
	/*标题栏: 车库系统 + 模式*/
	OLED_ShowChinese(0, ROW_TITLE, "车库系统");
	OLED_ShowString(96, ROW_TITLE, "AUTO", OLED_8X16);  /*默认自动模式；字库缺"遥控"，用英文缩写*/
	
	/*标题栏反色（白底黑字效果）*/
	OLED_ReverseArea(0, ROW_TITLE, 128, 16);
	
	/*固定标签*/
	OLED_ShowChinese(0, ROW_BAL, "余：");    /*余额标签 32px */
	OLED_ShowChinese(58, ROW_BAL, "空：");   /*空位标签 32px */
	OLED_ShowChinese(0, ROW_PLATE, "牌：");  /*车牌标签 32px */
}

/*更新状态消息区域（车牌号或状态文字）*/
void OLED_ShowMsg(char *text, uint8_t is_chinese)
{
	OLED_ClearArea(MSG_X, ROW_PLATE, MSG_W, MSG_H);
	if (is_chinese)
		OLED_ShowChinese(MSG_X, ROW_PLATE, text);
	else
		OLED_ShowString(MSG_X, ROW_PLATE, text, OLED_8X16);
}

/*更新余额数字*/
void OLED_ShowBalance(int16_t val)
{
	OLED_ClearArea(BAL_X, ROW_BAL, BAL_W, MSG_H);
	if (val >= 0)
		OLED_ShowNum(BAL_X, ROW_BAL, (uint32_t)val, 3, OLED_8X16);
}

/*更新空位数字*/
void OLED_ShowSpaces(uint8_t val)
{
	OLED_ClearArea(SPC_X, ROW_BAL, 16, MSG_H);
	OLED_ShowNum(SPC_X, ROW_BAL, val, 2, OLED_8X16);
}

/*更新日期时间显示*/
void OLED_ShowDateTime(void)
{
	/*日期: 2024/10/26 (6x8字体, 每字符6px) */
	OLED_ShowNum(0, ROW_TIME, A, 4, OLED_6X8);    /*年份 24px */
	OLED_ShowString(24, ROW_TIME, "/", OLED_6X8);  /*分隔 6px */
	OLED_ShowNum(30, ROW_TIME, B, 2, OLED_6X8);   /*月份 12px */
	OLED_ShowString(42, ROW_TIME, "/", OLED_6X8);  /*分隔 6px */
	OLED_ShowNum(48, ROW_TIME, C, 2, OLED_6X8);   /*日期 12px */
	
	/*时间: 20:00:00 */
	OLED_ShowNum(64, ROW_TIME, H, 2, OLED_6X8);     /*时 12px */
	OLED_ShowString(76, ROW_TIME, ":", OLED_6X8);    /*冒号 6px */
	OLED_ShowNum(82, ROW_TIME, M, 2, OLED_6X8);      /*分 12px */
	OLED_ShowString(94, ROW_TIME, ":", OLED_6X8);    /*冒号 6px */
	OLED_ShowNum(100, ROW_TIME, S, 2, OLED_6X8);     /*秒 12px */
}

/*更新系统状态行（闸门/LED状态）*/
void OLED_ShowSysStatus(void)
{
	char *gate_str = (gate_state == 2) ? "OPEN" : "CLOSE";
	char *led_str;
	
	if (gate_state == 0) led_str = "RED";
	else if (gate_state == 1) led_str = "YEL";
	else led_str = "GRN";
	
	/*清空系统状态行*/
	OLED_ClearArea(0, ROW_SYS, 128, 8);
	
	/*显示: GATE:OPEN  LED:GRN */
	OLED_Printf(0, ROW_SYS, OLED_6X8, "GATE:%-5s LED:%-3s", gate_str, led_str);
}

int main(void)
{
	/*NVIC中断分组，仅设置一次*/
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	
	/*模块初始化*/
	LED_Init();			/*PA4绿灯GLED PA5黄灯YLED PA6红灯RLED PA7路灯LED*/
	OLED_Init();		/*PB6=SCL PB7=SDA (软件I2C)*/
	KEY_Init();			/*PB8=KEY1 PB9=KEY2（已从PB6/PB7移开，避免与OLED I2C冲突）*/
	HONGWAI_Init();	/*PB0光敏GM PA0闸口通过(传感器2) PA1闸口到达(传感器1) PB12~PB15+PA11~PA12车位(S_OUT1~6)*/
	Servo_Init();		/*PB1=TIM3_CH4 舵机SG_PWM*/
	Servo_SetAngle(Angle);	/*舵机归位（关闸状态）*/
	Lit_Init();			/*PA7路灯初始化（默认关闭）*/
	Serial_Init();		/*QR模块 USART3 PB10(TX)/PB11(RX)*/
	BLE_Serial_Init();	/*BLE模块 USART2 PA2(TX)/PA3(RX)*/
	Voice_Init();		/*语音模块 USART1 PA9(TX)/PA10(RX)*/
	Timer_Init();		/*TIM2 1ms中断 精确计时*/
	PlateDB_Init();		/*车牌库初始化：出厂预置4个车牌，注册的车牌存于内存*/
	
	/*OLED静态UI绘制（仅初始化一次）*/
	OLED_DrawStaticUI();
	OLED_ShowSysStatus();
	OLED_ShowDateTime();
	OLED_ShowSpaces(Get_FreeSpaces());
	OLED_Update();
	
	/*系统启动：交通灯常亮红灯*/
	LED2_ON(); LED0_OFF(); LED1_OFF();
	
	while (1)
	{
		/*----- 从TIM2定时器获取精确时间 -----*/
		H = Timer_Hour;
		M = Timer_Minute;
		S = Timer_Second;
		
		/*----- 智能路灯 -----*/
		if (GM_Get() == 1)		/*黑夜/阴暗：点亮路灯*/
			GPIO_ResetBits(GPIOA, GPIO_Pin_7);
		else					/*白天：关闭路灯*/
			GPIO_SetBits(GPIOA, GPIO_Pin_7);
		
		/*----- 按键调时间（预留）-----*/
		uint8_t KEYNum = KEY_GetNum();
		if (KEYNum == 1) H++;
		if (KEYNum == 2) M++;
		
		/*----- 检测车辆到达闸口（触发扫码流程）-----*/
		if (HW2_Get() == 0 && gate_state == 0 && work_mode == 1)
		{
			gate_state = 1;
			LED2_OFF(); LED1_ON(); LED0_OFF();	/*黄灯：等待通行*/
			Say("欢迎光临智能车库，请扫描入库二维码。");
			/*OLED显示欢迎消息*/
			OLED_ShowMsg("欢迎光临", 1);
			oled_dirty = 1;
		}
		
		/*----- 处理二维码识别数据（USART3）仅自动模式-----*/
		if (Serial_RxFlag == 1 && work_mode == 1)
		{
			/* 二维码协议（定长解析，不用 strstr，避免 "ZC"/"CZ" 互相干扰）
			 *   A777777.          纯车牌  ：按正常流程进场（查库 -> 扣费 -> 开闸）
			 *   A777777ZC.        注册车牌：录入系统，余额 0，不开闸
			 *   A777777ZCCZ100.   注册车牌：录入系统并充值 100 元
			 * 车牌格式：1位大写字母 + 6位数字（共 7 个字符）
			 */
			char code[32];
			char plate[PLATE_LEN + 1];
			uint8_t len = 0;
			uint8_t valid = 1;
			uint8_t i;

			/* 拷贝一份并统一转大写，同时裁掉行尾空白（部分扫码模块会补空格）*/
			for (i = 0; i < 31 && Serial_RxPacket[i] != '\0'; i++)
			{
				char c = Serial_RxPacket[i];
				if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
				code[i] = c;
			}
			code[i] = '\0';
			len = i;
			while (len > 0 && (code[len - 1] == ' ' || code[len - 1] == '\t'))
			{
				code[--len] = '\0';
			}

			/* 合法性检查：长度 >= 7；第1位必须是大写字母；第2~7位必须是数字*/
			if (len < PLATE_LEN) valid = 0;
			if (valid && (code[0] < 'A' || code[0] > 'Z')) valid = 0;
			if (valid)
			{
				for (i = 0; i < PLATE_LEN; i++)
				{
					plate[i] = code[i];
					if (i >= 1 && (code[i] < '0' || code[i] > '9')) valid = 0;
				}
				plate[PLATE_LEN] = '\0';
			}

			if (!valid)
			{
				/* 不是本系统的码（扫到了商品条码或其他二维码）*/
				OLED_ShowMsg("INVALID", 0);
				oled_dirty = 1;
				Say("二维码无效，请扫描本车库的车牌二维码。");
			}
			else if (len == PLATE_LEN)
			{
				/*----- 纯车牌：正常进场流程 -----*/
				int8_t idx = PlateDB_Find(plate);

				strcpy(current_plate, plate);	/*只拷7字节，避免后面的标志位混进来*/

				if (idx >= 0)	/*注册车辆*/
				{
					uint8_t free_spaces = Get_FreeSpaces();
					int16_t bal = PlateDB_GetBalance(idx);

					if (free_spaces == 0)
					{
						/*车位已满*/
						Say("车库已满，暂无空余车位，请稍后。");
						OLED_ShowMsg("车位已满", 1);
						oled_dirty = 1;
					}
					else if (bal < 20)	/*余额不足*/
					{
						OLED_ShowBalance(bal);
						OLED_ShowMsg("余额不足", 1);
						oled_dirty = 1;
						Say("余额不足，请及时充值。");
					}
					else	/*余额充足：开闸*/
					{
						PlateDB_AddBalance(idx, -20);	/*扣款*/
						gate_state = 2;
						LED1_OFF(); LED0_ON(); LED2_OFF();	/*绿灯：通行*/
						Angle = 100;
						Servo_SetAngle(Angle);	/*开闸*/
						/*OLED显示车牌和余额*/
						OLED_ShowMsg(current_plate, 0);
						OLED_ShowBalance(PlateDB_GetBalance(idx));
						oled_dirty = 1;
						/*语音播报*/
						Say("识别成功，正在开闸，请安全驶入。");
						/*播报剩余车位及推荐车位*/
						{
							uint8_t rec = Recommend_Space();
							char msg[64] = {0};
							if (rec >= 1 && rec <= 3)
								sprintf(msg, "当前剩余车位%d个，请到L1层%d号车位停车。", free_spaces, rec);
							else if (rec >= 4 && rec <= 6)
								sprintf(msg, "当前剩余车位%d个，请到L2层%d号车位停车。", free_spaces, rec - 3);
							else
								sprintf(msg, "当前剩余车位%d个。", free_spaces);
							Say(msg);

							/* 发送停车记录到遥控器（经蓝牙） */
							/* 格式: @P<车牌>|<HHMMSS>|<车位号>#* */
							{
								char ble_msg[40];
								sprintf(ble_msg, "@P%s|%02d%02d%02d|%02d#*",
									current_plate, H, M, S, rec);
								BLE_Serial_SendString(ble_msg);
							}
						}
					}
				}
				else	/*未注册车辆（外来车辆）*/
				{
					OLED_ShowMsg("外来车辆", 1);
					oled_dirty = 1;
					Say("外来车辆，请联系管理人员。");
				}
			}
			else if (code[PLATE_LEN] == 'Z' && code[PLATE_LEN + 1] == 'C')
			{
				/*----- 注册车牌：A777777ZC. / A777777ZCCZ100. -----*/
				int16_t amount = 0;
				int8_t idx = PlateDB_Find(plate);

				/* 解析可选的充值段 "CZ<金额>" */
				if (len >= PLATE_LEN + 4 && code[PLATE_LEN + 2] == 'C' && code[PLATE_LEN + 3] == 'Z')
				{
					int v = atoi(&code[PLATE_LEN + 4]);
					if (v < 0)   v = 0;
					if (v > 999) v = 999;	/*OLED只能显示3位，上限999*/
					amount = (int16_t)v;
				}

				if (idx >= 0)	/*已注册过：只累加余额，不重复录入*/
				{
					if (amount > 0) PlateDB_AddBalance(idx, amount);
					OLED_ShowMsg(plate, 0);
					OLED_ShowBalance(PlateDB_GetBalance(idx));
					oled_dirty = 1;
					if (amount > 0)
					{
						char msg2[48] = {0};
						sprintf(msg2, "该车牌已注册，充值%d元。", amount);
						Say(msg2);
					}
					else
					{
						Say("该车牌已注册，无需重复录入。");
					}
				}
				else
				{
					int8_t nidx = PlateDB_Register(plate, amount);

					if (nidx < 0)	/*车牌库已满*/
					{
						OLED_ShowMsg("库已满", 1);
						oled_dirty = 1;
						Say("车牌库已满，请联系管理人员。");
					}
					else
					{
						OLED_ShowMsg(plate, 0);
						OLED_ShowBalance(PlateDB_GetBalance(nidx));
						oled_dirty = 1;
						if (amount > 0)
						{
							char msg3[48] = {0};
							sprintf(msg3, "注册成功，已充值%d元。", amount);
							Say(msg3);
						}
						else if (gate_state == 1)
						{
							Say("注册成功，请再次扫描车牌二维码入场。");
						}
						else
						{
							Say("注册成功。");
						}
					}
				}
			}
			else if (code[PLATE_LEN] == 'C' && code[PLATE_LEN + 1] == 'Z')
			{
				/*----- 直接充值：A777777CZ100.（车牌必须已注册）-----*/
				int16_t amount = 0;
				int8_t idx = PlateDB_Find(plate);

				if (len >= PLATE_LEN + 3)
				{
					int v = atoi(&code[PLATE_LEN + 2]);
					if (v < 0)   v = 0;
					if (v > 999) v = 999;	/*OLED只能显示3位，上限999*/
					amount = (int16_t)v;
				}

				if (idx < 0)	/*未注册的车牌不能直接充值*/
				{
					OLED_ShowMsg(plate, 0);
					oled_dirty = 1;
					Say("该车牌未注册，请先扫描注册二维码。");
				}
				else if (amount <= 0)
				{
					OLED_ShowMsg(plate, 0);
					oled_dirty = 1;
					Say("充值金额无效。");
				}
				else
				{
					PlateDB_AddBalance(idx, amount);
					OLED_ShowMsg(plate, 0);
					OLED_ShowBalance(PlateDB_GetBalance(idx));
					oled_dirty = 1;
					{
						char msg4[48] = {0};
						sprintf(msg4, "充值%d元成功。", amount);
						Say(msg4);
					}
				}
			}
			else
			{
				/* 是车牌，但后面跟了不认识的标志位*/
				OLED_ShowMsg("INVALID", 0);
				oled_dirty = 1;
				Say("二维码无效，请重新扫描。");
			}

			/*先清空数据，再清除标志位，防止ISR在memset完成前写入新数据*/
			memset(Serial_RxPacket, 0, 100);
			Serial_RxFlag = 0;
		}

		/*----- 处理蓝牙遥控数据（USART2）-----*/
		if (BLE_RxFlag == 1)
		{
			last_ble_sec = Timer_Second;	/*收到任何蓝牙帧（含心跳）都算遥控器在线*/
			if (strcmp(BLE_RxPacket, "K") == 0)	/*蓝牙开闸*/
			{
				gate_state = 2;
				LED1_OFF(); LED0_ON(); LED2_OFF();
				Angle = 100;
				Servo_SetAngle(Angle);
				Say("识别成功，正在开闸，请安全驶入。");
				remote_open_flag = 1;		/*标记：本次开闸由蓝牙触发*/
				remote_open_sec = Timer_Second;
				BLE_Serial_SendString("@KOK#*");
			}
			else if (strcmp(BLE_RxPacket, "G") == 0)	/*蓝牙关闸*/
			{
			remote_open_flag = 0;
			gate_state = 0;
			LED0_OFF(); LED1_OFF(); Angle = 10; Servo_SetAngle(Angle); LED2_ON();
			BLE_Serial_SendString("@GOK#*");
			}
			else if (strcmp(BLE_RxPacket, "A") == 0)	/*切换回自动模式*/
			{
				work_mode = 1;
				Say("已切换到自动模式。");
				BLE_Serial_SendString("@AOK#*");
			}
			else if (strcmp(BLE_RxPacket, "M") == 0)	/*切换到遥控模式*/
			{
				work_mode = 0;
				Say("已切换到遥控模式，请使用遥控器控制。");
				BLE_Serial_SendString("@MOK#*");
			}
			else if (BLE_RxPacket[0] == 'S')	/*车位传感器开关 S1=1*/
			{
				uint8_t num = BLE_RxPacket[1] - '0';
				uint8_t val = BLE_RxPacket[3] - '0';
				if (num >= 1 && num <= 6)
				{
					sensor_enable[num - 1] = val;
					BLE_Serial_SendString("@SOK#*");
				}
			}
			else if (strcmp(BLE_RxPacket, "Q") == 0)	/*状态查询 @Q#*（手机 APP 用）*/
			{
				/* 回复: @Q<模式><闸门><空位2位><传感器6位>#*
				 *   模式   : 1=自动 0=遥控
				 *   闸门   : 0=关闸(红) 1=等待(黄) 2=开闸(绿)
				 *   空位   : 00~06 两位
				 *   传感器 : 依次为 1~6 号，1=启用 0=禁用
				 * 例: "@Q1205011111#*" = 自动模式 + 已开闸 + 5 个空位 + 6 路检测器全开
				 */
				char st[24];
				sprintf(st, "@Q%c%c%02d%c%c%c%c%c%c#*",
					work_mode ? '1' : '0',
					(char)('0' + (gate_state > 2 ? 0 : gate_state)),
					(int)Get_FreeSpaces(),
					sensor_enable[0] ? '1' : '0',
					sensor_enable[1] ? '1' : '0',
					sensor_enable[2] ? '1' : '0',
					sensor_enable[3] ? '1' : '0',
					sensor_enable[4] ? '1' : '0',
					sensor_enable[5] ? '1' : '0');
				BLE_Serial_SendString(st);
			}
			/* 注：原蓝牙音量命令 V0~V100 已移除，本模块音量不支持串口调节 */
			BLE_RxFlag = 0;
		}
		
		/*----- 遥控器断连回落：5 秒没收到任何蓝牙帧就切回自动模式 -----*/
		if (work_mode == 0 && Sec_Elapsed(last_ble_sec, Timer_Second) >= 5)
		{
			work_mode = 1;
			oled_dirty = 1;
			Say("遥控器已断开，已切换回自动模式。");
		}
		
		/*----- 蓝牙开闸超时保护：15 秒内没有车辆通过就自动关闸 -----*/
		if (remote_open_flag && gate_state == 2 && Sec_Elapsed(remote_open_sec, Timer_Second) >= 15)
		{
			remote_open_flag = 0;
			gate_state = 0;
			LED0_OFF(); LED1_OFF(); LED2_ON();
			Angle = 10;
			Servo_SetAngle(Angle);
			OLED_ShowMsg("", 0);
			OLED_ShowBalance(-1);
			oled_dirty = 1;
			Say("长时间未检测到车辆，闸门已自动关闭。");
		}
		
		/*----- 车辆通过闸口（关栏杆）-----*/
		if (HW1_Get() == 0 && gate_state == 2)
		{
		remote_open_flag = 0;	/*车辆已通过，取消超时保护*/
		gate_state = 0;
		LED0_OFF(); LED1_OFF(); Angle = 10; Servo_SetAngle(Angle); LED2_ON();
			/*清空状态消息和余额显示*/
			OLED_ShowMsg("", 0);
			OLED_ShowBalance(-1);
			oled_dirty = 1;
		}
		
		/*----- OLED动态内容更新 -----*/
		
		/*更新空位数（每帧刷新，传感器状态可能变化）*/
		OLED_ShowSpaces(Get_FreeSpaces());
		
		/*更新日期时间*/
		OLED_ShowDateTime();
		
		/*更新系统状态行（闸门/LED）*/
		OLED_ShowSysStatus();
		
		/*模式变化时更新标题栏*/
		if (work_mode != prev_mode)
		{
			OLED_ClearArea(96, ROW_TITLE, 32, 16);
			OLED_ShowString(96, ROW_TITLE, work_mode ? "AUTO" : "MAN", OLED_8X16);
			OLED_ReverseArea(96, ROW_TITLE, 32, 16);
			prev_mode = work_mode;
		}
		
		/*统一刷新OLED*/
		OLED_Update();
		
		Delay_ms(100);
	}
}
