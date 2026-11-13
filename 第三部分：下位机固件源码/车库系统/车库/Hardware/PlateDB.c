#include "PlateDB.h"
#include <string.h>

/*==============================================================================
 * 车牌库（纯内存版）
 *
 *  说明：注册进来的车牌只保存在 RAM 里，掉电或按复位键后清空，
 *        重新上电回到出厂预置的 4 条记录：
 *            A888888 100元   A666666 100元   C123456 100元   U131420 10元
 *
 *  容量：16 条（PLATE_DB_MAX），出厂预置占 4 条，还能再注册 12 个。
 *
 *  想改成掉电不丢失：Flash 末两页 0x0800F800 / 0x0800FC00 目前完全空闲
 *  （工程代码只占 23.2KB / 64KB），把 g_PlateDB 整体写进去、Init 时读回来即可，
 *  改动只涉及本文件的 Init / Register / AddBalance 三个函数，main.c 不用动。
 *============================================================================*/

static PlateRec_t g_PlateDB[PLATE_DB_MAX];
static uint8_t    g_PlateCount = 0;

/*出厂预置车牌及其余额*/
static const char    *const g_FactoryPlate[4]   = { "A888888", "A666666", "C123456", "U131420" };
static const int16_t        g_FactoryBalance[4] = { 100, 100, 100, 10 };

/**
  * 函    数：车牌库初始化
  * 说    明：清空全部记录，写入出厂预置的4个车牌
  */
void PlateDB_Init(void)
{
	uint8_t i;

	memset(g_PlateDB, 0, sizeof(g_PlateDB));
	g_PlateCount = 0;

	for (i = 0; i < 4; i++)
	{
		strncpy(g_PlateDB[i].plate, g_FactoryPlate[i], PLATE_LEN);
		g_PlateDB[i].plate[PLATE_LEN] = '\0';
		g_PlateDB[i].balance = g_FactoryBalance[i];
		g_PlateDB[i].used    = 1;
	}
	g_PlateCount = 4;
}

/**
  * 函    数：查找车牌
  * 返 回 值：记录索引 0~15；-1 表示未注册
  */
int8_t PlateDB_Find(const char *plate)
{
	uint8_t i;

	if (plate == 0) return -1;

	for (i = 0; i < PLATE_DB_MAX; i++)
	{
		if (g_PlateDB[i].used && strcmp(g_PlateDB[i].plate, plate) == 0)
		{
			return (int8_t)i;
		}
	}
	return -1;
}

/**
  * 函    数：注册新车牌
  * 参    数：plate 车牌（7字符）；init_balance 初始余额
  * 返 回 值：记录索引；-1 表示库已满或车牌已存在
  */
int8_t PlateDB_Register(const char *plate, int16_t init_balance)
{
	uint8_t i;

	if (plate == 0) return -1;
	if (PlateDB_Find(plate) >= 0) return -1;	/*已存在：调用前应先 Find 处理*/

	for (i = 0; i < PLATE_DB_MAX; i++)
	{
		if (g_PlateDB[i].used == 0)
		{
			strncpy(g_PlateDB[i].plate, plate, PLATE_LEN);
			g_PlateDB[i].plate[PLATE_LEN] = '\0';
			g_PlateDB[i].balance = init_balance;
			g_PlateDB[i].used    = 1;
			g_PlateCount++;
			return (int8_t)i;
		}
	}
	return -1;
}

/**
  * 函    数：读余额
  * 返 回 值：余额；索引非法返回 -1
  */
int16_t PlateDB_GetBalance(int8_t idx)
{
	if (idx < 0 || idx >= PLATE_DB_MAX || g_PlateDB[idx].used == 0) return -1;
	return g_PlateDB[idx].balance;
}

/**
  * 函    数：加减余额（delta 为负即扣费）
  * 说    明：结果限制在 -30000~30000，防止 int16_t 溢出
  * 返 回 值：1=成功 0=索引非法
  */
uint8_t PlateDB_AddBalance(int8_t idx, int16_t delta)
{
	int32_t v;

	if (idx < 0 || idx >= PLATE_DB_MAX || g_PlateDB[idx].used == 0) return 0;

	v = (int32_t)g_PlateDB[idx].balance + (int32_t)delta;
	if (v >  30000) v =  30000;
	if (v < -30000) v = -30000;
	g_PlateDB[idx].balance = (int16_t)v;
	return 1;
}

/**
  * 函    数：当前已使用的车牌数
  */
uint8_t PlateDB_Count(void)
{
	return g_PlateCount;
}

/**
  * 函    数：车牌库是否已满
  */
uint8_t PlateDB_IsFull(void)
{
	return (g_PlateCount >= PLATE_DB_MAX) ? 1 : 0;
}
