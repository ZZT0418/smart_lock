#ifndef __RC522_H__
#define __RC522_H__

#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

/********************* 引脚定义 *********************/
#define RC522_SPI_PORT        SPI2_HOST       // SPI2主机
#define RC522_SPI_MISO_PIN    GPIO_NUM_19     // MISO引脚
#define RC522_SPI_MOSI_PIN    GPIO_NUM_16     // MOSI引脚
#define RC522_SPI_SCK_PIN     GPIO_NUM_15     // SCK引脚
#define RC522_SPI_CS_PIN      GPIO_NUM_20     // SDA引脚

/********************* 延时宏 *********************/
#define RC522_DELAY_US(x)     esp_rom_delay_us(x)
#define RC522_DELAY_MS(x)     vTaskDelay(pdMS_TO_TICKS(x))

/********************* MF522 命令字 *********************/
#define PCD_IDLE              0x00    // 取消当前命令
#define PCD_AUTHENT           0x0E    // 验证密钥
#define PCD_RECEIVE           0x08    // 接收数据
#define PCD_TRANSMIT          0x04    // 发送数据
#define PCD_TRANSCEIVE        0x0C    // 发送并接收数据
#define PCD_RESETPHASE        0x0F    // 复位
#define PCD_CALCCRC           0x03    // CRC计算

/********************* Mifare_One 卡命令字 *********************/
#define PICC_REQIDL           0x26    // 寻天线区内未进入休眠状态
#define PICC_REQALL           0x52    // 寻天线区内全部卡
#define PICC_ANTICOLL1        0x93    // 防冲撞
#define PICC_ANTICOLL2        0x95    // 防冲撞
#define PICC_AUTHENT1A        0x60    // 验证A密钥
#define PICC_AUTHENT1B        0x61    // 验证B密钥
#define PICC_READ             0x30    // 读块
#define PICC_WRITE            0xA0    // 写块
#define PICC_HALT             0x50    // 休眠

/********************* MF522 寄存器地址 *********************/
// PAGE 0
#define CommandReg            0x01
#define ComIEnReg             0x02
#define ComIrqReg             0x04
#define DivIrqReg             0x05
#define ErrorReg              0x06
#define Status2Reg            0x08
#define FIFODataReg           0x09
#define FIFOLevelReg          0x0A
#define ControlReg            0x0C
#define BitFramingReg         0x0D
#define CollReg               0x0E
// PAGE 1
#define ModeReg               0x11
#define TxControlReg          0x14
#define TxAutoReg             0x15
// PAGE 2
#define CRCResultRegM         0x21
#define CRCResultRegL         0x22
#define TModeReg              0x2A
#define TPrescalerReg         0x2B
#define TReloadRegH           0x2C
#define TReloadRegL           0x2D

/********************* 通信状态返回码 *********************/
#define MI_OK                 0x26
#define MI_NOTAGERR           0xCC
#define MI_ERR                0xBB

/********************* 全局变量（SPI设备句柄） *********************/
extern spi_device_handle_t rc522_spi_handle;

/********************* 函数声明 *********************/
// 初始化RC522（含SPI初始化）
esp_err_t MFRC522_Init(void);
// 复位RC522
uint8_t MFRC522_Reset(void);
// 写RC522寄存器
void Write_MFRC522(uint8_t Address, uint8_t value);
// 读RC522寄存器
uint8_t Read_MFRC522(uint8_t Address);
// 开启天线
void MFRC522_AntennaOn(void);
// 关闭天线
void MFRC522_AntennaOff(void);
// 寻卡
uint8_t MFRC522_Request(uint8_t req_code, uint8_t *pTagType);
// 防冲撞（获取卡号）
uint8_t MFRC522_Anticoll(uint8_t *pSnr);
// 选卡
uint8_t MFRC522_SelectTag(uint8_t *pSnr);
// 验证密钥
uint8_t MFRC522_AuthState(uint8_t auth_mode, uint8_t addr, uint8_t *pKey, uint8_t *pSnr);
// 读块
uint8_t MFRC522_Read(uint8_t addr, uint8_t *pData);
// 写块
uint8_t MFRC522_Write(uint8_t addr, uint8_t *pData);
// 休眠卡
uint8_t MFRC522_Halt(void);

#endif