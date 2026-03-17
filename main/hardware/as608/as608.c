#include "as608.h"
#include "string.h"
#include "esp_log.h"
#include "esp_err.h"


#define TAG "as608"
static uint32_t as608_dev_addr = AS608_DEFAULT_ADDR; // 设备地址，默认0xFFFFFFFF
static uint8_t uart_rx_buf[UART_BUF_SIZE] = {0};     // 串口接收缓冲区

/************************* 底层UART初始化 *************************/
// 功能：初始化UART1，严格匹配AS608串口参数
// 返回：ESP_OK=成功，其他=失败
esp_err_t esp32_uart_init(void)
{
    // 1. 配置UART参数
    uart_config_t uart_config = {
        .baud_rate = AS608_UART_BAUDRATE,  // 57600波特率
        .data_bits = UART_DATA_8_BITS,     // 8位数据位
        .parity    = UART_PARITY_DISABLE,  // 无校验
        .stop_bits = UART_STOP_BITS_1,     // 1位停止位
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, // 无流控
        .source_clk = UART_SCLK_APB,       // 时钟源APB
    };
    // 2. 配置UART参数
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    // 3. 设置UART引脚（TX=15, RX=16，RTS/CTS悬空）
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT,
                                UART_TX_IO,
                                UART_RX_IO,
                                UART_PIN_NO_CHANGE,
                                UART_PIN_NO_CHANGE));
    // 4. 安装UART驱动：接收缓冲区256，发送缓冲区256，队列长度10
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT,
                                        UART_BUF_SIZE,
                                        UART_BUF_SIZE,
                                        10,
                                        NULL,
                                        0));
    ESP_LOGI(TAG, "UART1初始化成功，TX:15, RX:16");
    return ESP_OK;
}

/************************* 发送单个字节 *************************/
// 功能：底层字节发送，封装uart_write_bytes，简化代码
static void as608_send_byte(uint8_t data)
{
    uart_write_bytes(UART_PORT, &data, 1);
}

/************************* 计算校验和 *************************/
// 功能：计算协议包校验和（包标识到数据末尾的和，取低16位）
// 参数：buf=数据缓冲区，len=数据长度（从包标识开始）
// 返回：16位校验和（大端模式）
static uint16_t as608_calc_checksum(uint8_t *buf, uint16_t len)
{
    uint16_t sum = 0;
    for (int i = 0; i < len; i++)
    {
        sum += buf[i]; // 所有字节相加
    }
    return sum & 0xFFFF; // 取低16位
}

/************************* 发送指令包 *************************/
// 功能：按AS608协议封装并发送指令包
// 参数：cmd_buf=指令数据（包标识+长度+指令+参数），cmd_len=指令数据长度
// 返回：ENSURE_OK=发送成功，其他=失败
uint8_t as608_send_cmd(uint8_t *cmd_buf, uint16_t cmd_len)
{
    if (cmd_buf == NULL || cmd_len == 0)
    {
        ESP_LOGE(TAG, "指令包为空");
        return ENSURE_TIMEOUT;
    }
    // 1. 清空接收缓冲区（避免残留数据干扰）
    uart_flush_input(UART_PORT);
    memset(uart_rx_buf, 0, UART_BUF_SIZE);

    // 2. 发送包头：0xEF 0x01（固定）
    as608_send_byte(0xEF);
    as608_send_byte(0x01);

    // 3. 发送设备地址：4字节，高位在前（默认0xFFFFFFFF）
    as608_send_byte((as608_dev_addr >> 24) & 0xFF);
    as608_send_byte((as608_dev_addr >> 16) & 0xFF);
    as608_send_byte((as608_dev_addr >> 8) & 0xFF);
    as608_send_byte(as608_dev_addr & 0xFF);

    // 4. 发送指令数据（包标识+长度+指令+参数）
    for (int i = 0; i < cmd_len; i++)
    {
        as608_send_byte(cmd_buf[i]);
    }

    // 5. 计算并发送校验和：包标识到参数末尾的和
    uint16_t checksum = as608_calc_checksum(cmd_buf, cmd_len);
    as608_send_byte((checksum >> 8) & 0xFF); // 高8位
    as608_send_byte(checksum & 0xFF);        // 低8位

    //ESP_LOGI(TAG, "指令包发送成功，长度：%d", cmd_len + 8); // 8=包头+地址
    return ENSURE_OK;
}

/************************* 等待应答包 *************************/
// 功能：等待AS608应答，解析确认码，存储应答数据
// 参数：resp_buf=存储应答数据的缓冲区，timeout_ms=超时时间（ms）
// 返回：确认码（ENSURE_OK/ENSURE_TIMEOUT/其他错误码）
uint8_t as608_wait_resp(uint8_t *resp_buf, uint32_t timeout_ms)
{
    uint32_t start_tick = xTaskGetTickCount(); // 记录开始时间
    uint16_t recv_len = 0;                     // 已接收字节数

    // 循环接收，直到超时或接收到完整应答包
    while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(timeout_ms))
    {
        // 读取串口接收的数据，非阻塞
        int len = uart_read_bytes(UART_PORT, uart_rx_buf + recv_len, UART_BUF_SIZE - recv_len, 0);
        if (len > 0)
        {
            recv_len += len;
            // 校验包头：必须以0xEF 0x01开头，且至少接收到10字节（最小应答包长度）
            if (recv_len >= 10 && uart_rx_buf[0] == 0xEF && uart_rx_buf[1] == 0x01)
            {
                // 校验包标识：应答包固定为0x07
                if (uart_rx_buf[6] == 0x07)
                {
                    // 解析确认码：应答包第9字节（索引9）
                    uint8_t ensure_code = uart_rx_buf[9];
                    // 复制应答数据到外部缓冲区（如果需要）
                    if (resp_buf != NULL)
                    {
                        memcpy(resp_buf, uart_rx_buf, recv_len);
                    }
                    //ESP_LOGI(TAG, "收到应答包，确认码：0x%02X", ensure_code);
                    return ensure_code;
                }
                else
                {
                    ESP_LOGE(TAG, "应答包包标识错误，收到：0x%02X", uart_rx_buf[6]);
                    return ENSURE_TIMEOUT;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5)); // 短暂延时，降低CPU占用
    }

    // 超时未收到有效应答
    ESP_LOGE(TAG, "通信超时，已接收：%d字节", recv_len);
    return ENSURE_TIMEOUT;
}

/************************* 采集指纹图像 *************************/
// 功能：探测传感器上的手指，采集原始图像存入图像缓冲区
// 返回：确认码（ENSURE_OK=成功，ENSURE_NO_FINGER=无手指，其他=失败）
uint8_t as608_get_image(void)
{
    // 指令包结构：包标识(0x01)+包长度(0x0003)+指令码(0x01)
    uint8_t cmd_buf[] = {0x01, 0x00, 0x03, 0x01};
    // 发送指令包
    if (as608_send_cmd(cmd_buf, sizeof(cmd_buf)) != ENSURE_OK)
    {
        return ENSURE_TIMEOUT;
    }
    // 等待应答，超时2000ms
    return as608_wait_resp(NULL, 2000);
}

/************************* 生成指纹特征 *************************/
// 功能：将图像缓冲区的原始图像生成特征，存入指定缓冲区
// 参数：buf_id=缓冲区编号（1=Buffer1，2=Buffer2，其他按2处理）
// 返回：确认码（ENSURE_OK=成功，其他=失败）
uint8_t as608_gen_char(uint8_t buf_id)
{
    // 修正缓冲区编号：非1则设为2
    buf_id = (buf_id == 1) ? 1 : 2;
    // 指令包结构：包标识(0x01)+包长度(0x0004)+指令码(0x02)+缓冲区编号
    uint8_t cmd_buf[] = {0x01, 0x00, 0x04, 0x02, buf_id};
    if (as608_send_cmd(cmd_buf, sizeof(cmd_buf)) != ENSURE_OK)
    {
        return ENSURE_TIMEOUT;
    }
    return as608_wait_resp(NULL, 2000);
}

/************************* 比对两个缓冲区的特征 *************************/
// 功能：比对Buffer1和Buffer2中的指纹特征（用于录入时验证两次指纹是否一致）
// 返回：确认码（ENSURE_OK=匹配，ENSURE_MATCH_FAIL=不匹配，其他=失败）
uint8_t as608_match(void)
{
    // 指令包结构：包标识(0x01)+包长度(0x0003)+指令码(0x03)
    uint8_t cmd_buf[] = {0x01, 0x00, 0x03, 0x03};
    if (as608_send_cmd(cmd_buf, sizeof(cmd_buf)) != ENSURE_OK)
    {
        return ENSURE_TIMEOUT;
    }
    return as608_wait_resp(NULL, 2000);
}

/************************* 搜索指纹库 *************************/
// 功能：以指定缓冲区的特征搜索指纹库，返回匹配结果
// 参数：buf_id=缓冲区编号，start=搜索起始编号，num=搜索数量，res=存储搜索结果
// 返回：确认码（ENSURE_OK=找到，ENSURE_SEARCH_FAIL=未找到，其他=失败）
uint8_t as608_search(uint8_t buf_id, uint16_t start, uint16_t num, As608_Search_Result *res)
{
    if (res == NULL)
    {
        return ENSURE_TIMEOUT;
    }
    buf_id = (buf_id == 1) ? 1 : 2;
    // 指令包结构：包标识(0x01)+包长度(0x0008)+指令码(0x04)+buf_id+start(2字节)+num(2字节)
    uint8_t cmd_buf[] = {
        0x01, 0x00, 0x08, 0x04, buf_id,
        (start >> 8) & 0xFF, start & 0xFF, // 起始编号，高位在前
        (num >> 8) & 0xFF, num & 0xFF      // 搜索数量，高位在前
    };
    uint8_t resp_buf[UART_BUF_SIZE] = {0};
    // 发送指令并等待应答
    uint8_t ret = as608_send_cmd(cmd_buf, sizeof(cmd_buf));
    if (ret != ENSURE_OK)
    {
        return ret;
    }
    ret = as608_wait_resp(resp_buf, 2000);
    // 若搜索成功，解析匹配结果
    if (ret == ENSURE_OK)
    {
        res->page_id = (resp_buf[10] << 8) | resp_buf[11]; // 匹配编号
        res->match_soc = (resp_buf[12] << 8) | resp_buf[13];// 匹配分数
    }
    return ret;
}

/************************* 合并特征生成模板 *************************/
// 功能：将Buffer1和Buffer2的特征合并，生成指纹模板（录入指纹核心步骤）
// 返回：确认码（ENSURE_OK=成功，ENSURE_MERGE_FAIL=合并失败，其他=失败）
uint8_t as608_reg_model(void)
{
    // 指令包结构：包标识(0x01)+包长度(0x0003)+指令码(0x05)
    uint8_t cmd_buf[] = {0x01, 0x00, 0x03, 0x05};
    if (as608_send_cmd(cmd_buf, sizeof(cmd_buf)) != ENSURE_OK)
    {
        return ENSURE_TIMEOUT;
    }
    return as608_wait_resp(NULL, 2000);
}

/************************* 存储模板到指纹库 *************************/
// 功能：将指定缓冲区的模板存入指纹库的指定编号
// 参数：buf_id=缓冲区编号，page_id=指纹库编号（0~199）
// 返回：确认码（ENSURE_OK=成功，ENSURE_ADDR_ERR=地址越界，其他=失败）
uint8_t as608_store_char(uint8_t buf_id, uint16_t page_id)
{
    if (page_id >= AS608_MAX_STORAGE)
    {
        return ENSURE_ADDR_ERR;
    }
    buf_id = (buf_id == 1) ? 1 : 2;
    // 指令包结构：包标识(0x01)+包长度(0x0006)+指令码(0x06)+buf_id+page_id(2字节)
    uint8_t cmd_buf[] = {
        0x01, 0x00, 0x06, 0x06, buf_id,
        (page_id >> 8) & 0xFF, page_id & 0xFF
    };
    if (as608_send_cmd(cmd_buf, sizeof(cmd_buf)) != ENSURE_OK)
    {
        return ENSURE_TIMEOUT;
    }
    return as608_wait_resp(NULL, 2000);
}

/************************* 删除指纹 *************************/
// 功能：删除指纹库中从start开始的num个指纹
// 参数：start=起始编号，num=删除数量
// 返回：确认码（ENSURE_OK=成功，ENSURE_DELETE_FAIL=失败，其他=失败）
uint8_t as608_delete_char(uint16_t start, uint16_t num)
{
    if (start + num > AS608_MAX_STORAGE)
    {
        return ENSURE_ADDR_ERR;
    }
    // 指令包结构：包标识(0x01)+包长度(0x0007)+指令码(0x0C)+start(2字节)+num(2字节)
    uint8_t cmd_buf[] = {
        0x01, 0x00, 0x07, 0x0C,
        (start >> 8) & 0xFF, start & 0xFF,
        (num >> 8) & 0xFF, num & 0xFF
    };
    if (as608_send_cmd(cmd_buf, sizeof(cmd_buf)) != ENSURE_OK)
    {
        return ENSURE_TIMEOUT;
    }
    return as608_wait_resp(NULL, 2000);
}

/************************* 获取有效指纹数量 *************************/
// 功能：读取指纹库中已存储的有效指纹数量
// 参数：valid_num=存储有效数量的变量
// 返回：确认码（ENSURE_OK=成功，其他=失败）
uint8_t as608_get_valid_num(uint16_t *valid_num)
{
    if (valid_num == NULL)
    {
        return ENSURE_TIMEOUT;
    }
    // 指令包结构：包标识(0x01)+包长度(0x0003)+指令码(0x1D)
    uint8_t cmd_buf[] = {0x01, 0x00, 0x03, 0x1D};
    uint8_t resp_buf[UART_BUF_SIZE] = {0};
    uint8_t ret = as608_send_cmd(cmd_buf, sizeof(cmd_buf));
    if (ret != ENSURE_OK)
    {
        return ret;
    }
    ret = as608_wait_resp(resp_buf, 2000);
    if (ret == ENSURE_OK)
    {
        // 解析有效数量：应答包第10、11字节
        *valid_num = (resp_buf[10] << 8) | resp_buf[11];
    }
    return ret;
}

/************************* 获取错误信息 *************************/
// 功能：将错误码转换为字符串，方便调试
const char* as608_get_err_msg(uint8_t err_code)
{
    switch (err_code)
    {
        case ENSURE_OK:          return "操作成功";
        case ENSURE_NO_FINGER:   return "传感器无手指";
        case ENSURE_IMG_ERR:     return "录入图像失败";
        case ENSURE_FEATURE_ERR: return "特征点太少，无法生成特征";
        case ENSURE_MATCH_FAIL:  return "指纹比对失败（两次录入不一致）";
        case ENSURE_SEARCH_FAIL: return "指纹库未找到匹配";
        case ENSURE_MERGE_FAIL:  return "特征合并失败";
        case ENSURE_ADDR_ERR:    return "指纹库地址超出范围";
        case ENSURE_DELETE_FAIL: return "删除指纹失败";
        case ENSURE_EMPTY_FAIL:  return "清空指纹库失败";
        case ENSURE_FLASH_ERR:   return "FLASH读写错误";
        case ENSURE_FULL:        return "指纹库已满";
        case ENSURE_TIMEOUT:     return "通信超时";
        default:                 return "未知错误码";
    }
}