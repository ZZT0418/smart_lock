#ifndef __AS608_H
#define __AS608_H

#include <stdint.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/************************* 串口配置 *************************/
#define UART_PORT           UART_NUM_1  // 使用UART1
#define UART_TX_IO          GPIO_NUM_15 // ESP32TX → AS608RX
#define UART_RX_IO          GPIO_NUM_16 // ESP32RX → AS608TX
#define AS608_UART_BAUDRATE 57600       // 使用AS608默认波特率
#define UART_BUF_SIZE       256         // 串口接收缓冲区大小

/************************* AS608默认参数 *************************/
#define AS608_DEFAULT_ADDR  0xFFFFFFFF  // 默认设备地址（4字节）
#define AS608_MAX_STORAGE   200         // AS608最大存储200枚指纹

/************************* 指令码（手册原版） *************************/
#define CMD_GET_IMAGE       0x01    // 采集指纹图像
#define CMD_GEN_CHAR        0x02    // 生成指纹特征（存入缓冲区）
#define CMD_MATCH           0x03    // 比对Buffer1/2的特征
#define CMD_SEARCH          0x04    // 指纹库搜索特征
#define CMD_REG_MODEL       0x05    // 合并Buffer1/2特征生成模板
#define CMD_STORE_CHAR      0x06    // 将模板存入指纹库指定地址
#define CMD_DELETE_CHAR     0x0C    // 删除指定范围指纹
#define CMD_EMPTY           0x0D    // 清空指纹库
#define CMD_READ_SYS_PARA   0x0F    // 读取系统参数
#define CMD_VALID_NUM       0x1D    // 读取有效指纹模板数量

/************************* 应答确认码（核心） *************************/
#define ENSURE_OK           0x00    // 操作成功
#define ENSURE_NO_FINGER    0x02    // 传感器无手指
#define ENSURE_IMG_ERR      0x03    // 录入图像失败
#define ENSURE_FEATURE_ERR  0x07    // 特征点太少无法生成特征
#define ENSURE_MATCH_FAIL   0x08    // 指纹比对失败
#define ENSURE_SEARCH_FAIL  0x09    // 指纹库未搜索到匹配
#define ENSURE_MERGE_FAIL   0x0A    // 特征合并失败（两次指纹不一致）
#define ENSURE_ADDR_ERR     0x0B    // 指纹库地址超出范围
#define ENSURE_DELETE_FAIL  0x10    // 删除指纹失败
#define ENSURE_EMPTY_FAIL   0x11    // 清空指纹库失败
#define ENSURE_FLASH_ERR    0x18    // FLASH读写错误
#define ENSURE_FULL         0x1F    // 指纹库已满
#define ENSURE_TIMEOUT      0xFF    // 通信超时

/************************* 结构体定义 *************************/
// 搜索结果：匹配到的指纹编号+匹配分数
typedef struct {
    uint16_t page_id;   // 指纹库中匹配的编号（0~199）
    uint16_t match_soc; // 匹配分数（0~255，越高越匹配，建议阈值≥100）
} As608_Search_Result;

// 系统参数：存储模块核心配置（用于调试）
typedef struct {
    uint16_t max_count; // 最大存储容量
    uint8_t  security;  // 安全等级（1~5，等级越高越严格）
    uint32_t dev_addr;  // 设备当前地址
    uint8_t  pkt_size;  // 数据包大小
    uint8_t  baud_code; // 波特率代码（对应9600*N）
} As608_Sys_Para;

/************************* 函数声明 *************************/
// 底层驱动
esp_err_t esp32_uart_init(void);                                                                // 初始化esp32的UART1
uint8_t as608_send_cmd(uint8_t *cmd_buf, uint16_t cmd_len);                                     // 发送指令包
uint8_t as608_wait_resp(uint8_t *resp_buf, uint32_t timeout_ms);                                // 等待应答

// 可能用到的指令
uint8_t as608_get_image(void);                                                                  // 采集指纹图像
uint8_t as608_gen_char(uint8_t buf_id);                                                         // 生成特征（buf_id:1=Buffer1,2=Buffer2）
uint8_t as608_match(void);                                                                      // 比对两个缓冲区的特征
uint8_t as608_search(uint8_t buf_id, uint16_t start, uint16_t num, As608_Search_Result *res);   // 搜索指纹库
uint8_t as608_reg_model(void);                                                                  // 合并特征生成模板
uint8_t as608_store_char(uint8_t buf_id, uint16_t page_id);                                     // 存储模板到指纹库
uint8_t as608_delete_char(uint16_t start, uint16_t num);                                        // 删除指纹
uint8_t as608_get_valid_num(uint16_t *valid_num);                                               // 获取有效指纹数量
const char* as608_get_err_msg(uint8_t err_code);                                                // 获取错误信息（调试）

#endif