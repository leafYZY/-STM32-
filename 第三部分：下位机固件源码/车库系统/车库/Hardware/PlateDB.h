#ifndef __PLATEDB_H
#define __PLATEDB_H

#include <stdint.h>

/*车牌库容量：含出厂预置的4个车牌*/
#define PLATE_DB_MAX	16
/*车牌长度：1位大写字母 + 6位数字，共7个字符*/
#define PLATE_LEN		7

/*车牌记录*/
typedef struct
{
	char	plate[PLATE_LEN + 1];	/*车牌字符串，以'\0'结尾*/
	int16_t	balance;				/*余额（元）*/
	uint8_t	used;					/*1=该槽已使用 0=空槽*/
	uint8_t	rsv;					/*占位，保持4字节对齐*/
} PlateRec_t;

/*清空车牌库并写入出厂预置车牌（上电时调用一次）*/
void	PlateDB_Init(void);
/*查找车牌，返回记录索引；-1 表示未注册*/
int8_t	PlateDB_Find(const char *plate);
/*注册新车牌，返回记录索引；-1 表示库已满或车牌已存在*/
int8_t	PlateDB_Register(const char *plate, int16_t init_balance);
/*读余额，索引非法返回 -1*/
int16_t	PlateDB_GetBalance(int8_t idx);
/*加减余额（delta 可为负），成功返回1*/
uint8_t	PlateDB_AddBalance(int8_t idx, int16_t delta);
/*当前已用车牌数*/
uint8_t	PlateDB_Count(void);
/*车牌库是否已满*/
uint8_t	PlateDB_IsFull(void);

#endif
