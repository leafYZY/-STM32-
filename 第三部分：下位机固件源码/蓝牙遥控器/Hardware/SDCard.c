#include "stm32f10x.h"
#include "Delay.h"
#include "SDCard.h"

/*
 * SD Card SPI Driver (SPI1)
 * PA4 = CS  (software controlled GPIO output)
 * PA5 = SCK (SPI1 alternate function)
 * PA6 = MISO (SPI1 input)
 * PA7 = MOSI (SPI1 alternate function)
 */

#define SD_CS_GPIO      GPIOA
#define SD_CS_PIN       GPIO_Pin_4
#define SD_SPI          SPI1

/* SD card type flags */
#define SD_TYPE_UNKNOWN 0
#define SD_TYPE_SDSC    1
#define SD_TYPE_SDHC    2

/* Private variables */
static uint8_t sd_type = SD_TYPE_UNKNOWN;
uint8_t SD_Initialized = 0;

/* ==================== Private Functions ==================== */

static void SD_CS_Select(void)
{
	GPIO_ResetBits(SD_CS_GPIO, SD_CS_PIN);
}

static void SD_CS_Deselect(void)
{
	GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
}

static uint8_t SD_SPI_RW(uint8_t data)
{
	/* Wait for TX buffer empty */
	while (SPI_I2S_GetFlagStatus(SD_SPI, SPI_I2S_FLAG_TXE) == RESET);
	SPI_I2S_SendData(SD_SPI, data);
	/* Wait for RX buffer not empty */
	while (SPI_I2S_GetFlagStatus(SD_SPI, SPI_I2S_FLAG_RXNE) == RESET);
	return SPI_I2S_ReceiveData(SD_SPI);
}

static void SD_SPI_InitGPIO(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOA, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;

	/* PA5 = SCK, PA7 = MOSI: Alternate Function Push-Pull */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/* PA6 = MISO: Input Pull-Up */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/* PA4 = CS: Push-Pull Output (software controlled) */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = SD_CS_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(SD_CS_GPIO, &GPIO_InitStructure);

	SD_CS_Deselect();  /* CS idle high */
}

static void SD_SPI_ConfigLowSpeed(void)
{
	SPI_InitTypeDef SPI_InitStructure;
	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;     /* CPOL=0 */
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;   /* CPHA=0, SPI Mode 0 */
	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256;  /* 72MHz/256 = 281kHz */
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial = 7;
	SPI_Init(SD_SPI, &SPI_InitStructure);
	SPI_Cmd(SD_SPI, ENABLE);
}

static void SD_SPI_ConfigHighSpeed(void)
{
	SPI_Cmd(SD_SPI, DISABLE);
	SPI_InitTypeDef SPI_InitStructure;
	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;  /* 72MHz/4 = 18MHz */
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial = 7;
	SPI_Init(SD_SPI, &SPI_InitStructure);
	SPI_Cmd(SD_SPI, ENABLE);
}

/* Wait for SD card to be ready (returns 0xFF) or timeout */
static SD_Result SD_WaitReady(void)
{
	uint32_t retry = 0;
	uint8_t r1;
	do {
		r1 = SD_SPI_RW(0xFF);
		retry++;
	} while (r1 != 0xFF && retry < 10000);
	return (r1 == 0xFF) ? SD_OK : SD_ERROR_TIMEOUT;
}

/* Send a command and return R1 response */
static uint8_t SD_SendCommand(uint8_t cmd, uint32_t arg, uint8_t crc)
{
	uint8_t r1;
	uint8_t retry = 0;

	/* Send dummy byte to sync */
	SD_SPI_RW(0xFF);

	/* For ACMD41, send CMD55 first */
	if (cmd == 0x29)  /* ACMD41 = 0x29 | 0x40 flag, we use special handling */
	{
		/* Will be handled by caller */
	}

	SD_CS_Select();

	/* Wait for card ready */
	if (SD_WaitReady() != SD_OK)
	{
		SD_CS_Deselect();
		return 0xFF;
	}

	/* Send command packet: 0x40|cmd, arg[31:24], arg[23:16], arg[15:8], arg[7:0], crc */
	SD_SPI_RW(0x40 | cmd);
	SD_SPI_RW((arg >> 24) & 0xFF);
	SD_SPI_RW((arg >> 16) & 0xFF);
	SD_SPI_RW((arg >> 8) & 0xFF);
	SD_SPI_RW(arg & 0xFF);
	SD_SPI_RW(crc);

	/* CMD12 (STOP_TRANSMISSION) may need an extra dummy byte */
	if (cmd == 12)
		SD_SPI_RW(0xFF);

	/* Poll for response (valid R1 has bit7=0) */
	do {
		r1 = SD_SPI_RW(0xFF);
		retry++;
	} while ((r1 & 0x80) && retry < 255);

	return r1;
}

/* ==================== Public Functions ==================== */

SD_Result SD_Init(void)
{
	uint8_t r1;
	uint8_t ocr[4];
	uint8_t retry;
	uint32_t timeout;

	SD_Initialized = 0;
	sd_type = SD_TYPE_UNKNOWN;

	/* Initialize GPIO first */
	SD_SPI_InitGPIO();

	/* Configure SPI at low speed for initialization */
	SD_SPI_ConfigLowSpeed();

	/* Send 80 dummy clock pulses with CS high (10 bytes of 0xFF) */
	SD_CS_Deselect();
	for (uint8_t i = 0; i < 10; i++)
		SD_SPI_RW(0xFF);

	/* Step 1: CMD0 - GO_IDLE_STATE, enter SPI mode */
	SD_CS_Select();
	retry = 0;
	do {
		r1 = SD_SendCommand(0, 0, 0x95);
		retry++;
	} while (r1 != 0x01 && retry < 50);

	if (r1 != 0x01)
	{
		SD_CS_Deselect();
		return SD_ERROR_INIT;
	}

	/* Step 2: CMD8 - SEND_IF_COND, check SD v2.0 */
	r1 = SD_SendCommand(8, 0x000001AA, 0x87);
	if (r1 == 0x01)
	{
		/* SD v2.0: read 4-byte response (OCR) */
		for (uint8_t i = 0; i < 4; i++)
			ocr[i] = SD_SPI_RW(0xFF);

		/* Step 3: ACMD41 with HCS=1 (CMD55 + ACMD41) */
		timeout = 0;
		do {
			r1 = SD_SendCommand(55, 0, 0x01);  /* CMD55 */
			if (r1 > 1) break;
			r1 = SD_SendCommand(41, 0x40000000, 0x01);  /* ACMD41 with HCS bit */
			timeout++;
		} while (r1 != 0x00 && timeout < 1000);

		if (r1 != 0x00)
		{
			SD_CS_Deselect();
			return SD_ERROR_INIT;
		}

		/* Step 4: CMD58 - READ_OCR, check if SDHC */
		r1 = SD_SendCommand(58, 0, 0x01);
		if (r1 == 0x00)
		{
			for (uint8_t i = 0; i < 4; i++)
				ocr[i] = SD_SPI_RW(0xFF);
			if (ocr[0] & 0x40)
				sd_type = SD_TYPE_SDHC;  /* CCS bit = 1 → SDHC/SDXC */
			else
				sd_type = SD_TYPE_SDSC;
		}
		else
		{
			sd_type = SD_TYPE_SDSC;
		}
	}
	else
	{
		/* SD v1.x: use ACMD41 without HCS */
		timeout = 0;
		do {
			r1 = SD_SendCommand(55, 0, 0x01);
			if (r1 > 1) break;
			r1 = SD_SendCommand(41, 0, 0x01);
			timeout++;
		} while (r1 != 0x00 && timeout < 1000);

		if (r1 != 0x00)
		{
			SD_CS_Deselect();
			return SD_ERROR_INIT;
		}
		sd_type = SD_TYPE_SDSC;
	}

	SD_CS_Deselect();
	SD_SPI_RW(0xFF);  /* Send dummy byte after deselect */

	/* Switch to high speed for data transfer */
	SD_SPI_ConfigHighSpeed();

	SD_Initialized = 1;
	return SD_OK;
}

SD_Result SD_ReadBlock(uint32_t blockAddr, uint8_t *buf)
{
	uint8_t r1;
	uint16_t i;

	if (!SD_Initialized)
		return SD_ERROR_NOCARD;

	/* For SDSC, convert block address to byte address */
	if (sd_type == SD_TYPE_SDSC)
		blockAddr <<= 9;  /* multiply by 512 */

	SD_CS_Select();

	/* CMD17 - READ_SINGLE_BLOCK */
	r1 = SD_SendCommand(17, blockAddr, 0x01);
	if (r1 != 0x00)
	{
		SD_CS_Deselect();
		return SD_ERROR_READ;
	}

	/* Wait for data start token (0xFE) */
	uint32_t retry = 0;
	uint8_t token;
	do {
		token = SD_SPI_RW(0xFF);
		retry++;
	} while (token != 0xFE && retry < 10000);

	if (token != 0xFE)
	{
		SD_CS_Deselect();
		return SD_ERROR_TIMEOUT;
	}

	/* Read 512 bytes of data */
	for (i = 0; i < 512; i++)
		buf[i] = SD_SPI_RW(0xFF);

	/* Read and discard 2-byte CRC */
	SD_SPI_RW(0xFF);
	SD_SPI_RW(0xFF);

	SD_CS_Deselect();
	SD_SPI_RW(0xFF);  /* Dummy byte */

	return SD_OK;
}

SD_Result SD_WriteBlock(uint32_t blockAddr, const uint8_t *buf)
{
	uint8_t r1;
	uint16_t i;

	if (!SD_Initialized)
		return SD_ERROR_NOCARD;

	/* For SDSC, convert block address to byte address */
	if (sd_type == SD_TYPE_SDSC)
		blockAddr <<= 9;

	SD_CS_Select();

	/* CMD24 - WRITE_BLOCK */
	r1 = SD_SendCommand(24, blockAddr, 0x01);
	if (r1 != 0x00)
	{
		SD_CS_Deselect();
		return SD_ERROR_WRITE;
	}

	/* Send dummy byte before data */
	SD_SPI_RW(0xFF);

	/* Send data start token */
	SD_SPI_RW(0xFE);

	/* Write 512 bytes of data */
	for (i = 0; i < 512; i++)
		SD_SPI_RW(buf[i]);

	/* Send dummy 2-byte CRC */
	SD_SPI_RW(0xFF);
	SD_SPI_RW(0xFF);

	/* Read data response token (bits 3:0 should be 0b0101 = data accepted) */
	r1 = SD_SPI_RW(0xFF);
	if ((r1 & 0x1F) != 0x05)
	{
		SD_CS_Deselect();
		return SD_ERROR_WRITE;
	}

	/* Wait for write to complete (card returns 0x00 during write, 0xFF when done) */
	if (SD_WaitReady() != SD_OK)
	{
		SD_CS_Deselect();
		return SD_ERROR_TIMEOUT;
	}

	SD_CS_Deselect();
	SD_SPI_RW(0xFF);  /* Dummy byte */

	return SD_OK;
}
