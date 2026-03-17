#include "rc522.h"
#include "string.h"

/********************* 全局变量 *********************/
spi_device_handle_t rc522_spi_handle = NULL;

/********************* SPI硬件初始化 *********************/
static esp_err_t rc522_spi_init(void)
{
    // 1. 配置SPI总线参数
    spi_bus_config_t bus_cfg = {
        .miso_io_num = RC522_SPI_MISO_PIN,
        .mosi_io_num = RC522_SPI_MOSI_PIN,
        .sclk_io_num = RC522_SPI_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };

    // 2. 初始化SPI总线
    esp_err_t ret = spi_bus_initialize(RC522_SPI_PORT, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        return ret;
    }

    // 3. 配置RC522 SPI设备参数
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 1 * 1000 * 1000,  // SPI时钟1MHz（RC522最大支持10MHz）
        .mode = 0,                          // SPI模式0（CPOL=0, CPHA=0）
        .spics_io_num = RC522_SPI_CS_PIN,   // CS引脚
        .queue_size = 7,                    // 队列大小
        .pre_cb = NULL,                     
        .post_cb = NULL,
    };

    // 4. 添加RC522设备到SPI总线
    ret = spi_bus_add_device(RC522_SPI_PORT, &dev_cfg, &rc522_spi_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

/********************* RC522初始化 *********************/
esp_err_t MFRC522_Init(void)
{
    // 1. 初始化SPI硬件
    esp_err_t ret = rc522_spi_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // 2. 复位RC522
    MFRC522_Reset();

    // 3. 开启天线
    MFRC522_AntennaOn();

    return ESP_OK;
}

/********************* 读RC522寄存器 *********************/
//参数：uint8_t Address-寄存器地址
uint8_t Read_MFRC522(uint8_t Address)  
{
    uint8_t tx_buf[2] = {0};            
    uint8_t rx_buf[2] = {0};            
    spi_transaction_t t = {0};

    // RC522寄存器地址格式：bit7=1（读）, bit6~1=地址, bit0=0
    tx_buf[0] = ((Address << 1) & 0x7E) | 0x80;  // 读地址
    tx_buf[1] = 0x00;                            // 占位（读取数据）

    // 配置SPI事务
    t.length = 16;                  // 总位数（2字节）
    t.tx_buffer = tx_buf;
    t.rx_buffer = rx_buf;
    t.user = NULL;

    // 发送SPI事务
    spi_device_transmit(rc522_spi_handle, &t);

    return rx_buf[1];  // 第二个字节是读取的数据
}

/********************* 写RC522寄存器 *********************/
//参数：uint8_t Address-寄存器地址； uint8_t value-写入的数据
void Write_MFRC522(uint8_t Address, uint8_t value)  
{
    uint8_t tx_buf[2] = {0};                        
    spi_transaction_t t = {0};

    // RC522寄存器地址格式：bit7=0（写）, bit6~1=地址, bit0=0
    tx_buf[0] = (Address << 1) & 0x7E;  // 写地址
    tx_buf[1] = value;                  // 要写入的数据

    // 配置SPI事务
    t.length = 16;                  // 总位数（2字节）
    t.tx_buffer = tx_buf;
    t.rx_buffer = NULL;
    t.user = NULL;

    // 发送SPI事务
    spi_device_transmit(rc522_spi_handle, &t);
}

/********************* 复位RC522 *********************/
uint8_t MFRC522_Reset(void)  
{
    Write_MFRC522(CommandReg, PCD_RESETPHASE);  // 软复位
    RC522_DELAY_US(100);

    // 等待复位完成
    while (Read_MFRC522(CommandReg) & 0x10) {
        RC522_DELAY_US(10);
    }

    // 初始化RC522寄存器（适配Mifare卡）
    Write_MFRC522(ModeReg, 0x3D);            // 通信模式：CRC初始值0x6363
    Write_MFRC522(TReloadRegL, 30);          // 定时器重装值低字节
    Write_MFRC522(TReloadRegH, 0);           // 定时器重装值高字节
    Write_MFRC522(TModeReg, 0x8D);           // 定时器模式
    Write_MFRC522(TPrescalerReg, 0x3E);      // 定时器预分频
    Write_MFRC522(TxAutoReg, 0x40);          // 自动发送

    return MI_OK;
}

/********************* 设置寄存器位掩码 *********************/
//参数：uint8_t reg-寄存器地址； uint8_t mask-位掩码
static void SetBitMask(uint8_t reg, uint8_t mask)  
{
    uint8_t tmp = Read_MFRC522(reg);               
    Write_MFRC522(reg, tmp | mask);
}

/********************* 清除寄存器位掩码 *********************/
//参数：uint8_t reg-寄存器地址； uint8_t mask-位掩码
static void ClearBitMask(uint8_t reg, uint8_t mask)  
{
    uint8_t tmp = Read_MFRC522(reg);                 
    Write_MFRC522(reg, tmp & ~mask);
}

/********************* 开启天线 *********************/
void MFRC522_AntennaOn(void)
{
    uint8_t tmp = Read_MFRC522(TxControlReg);  
    if (!(tmp & 0x03)) {
        SetBitMask(TxControlReg, 0x03);
    }
}

/********************* 关闭天线 *********************/
void MFRC522_AntennaOff(void)
{
    ClearBitMask(TxControlReg, 0x03);
}

/********************* RC522与卡片通信 *********************/
//参数：uint8_t Command-命令字； uint8_t *pInData-发送数据； uint8_t InLenByte-发送数据长度
static uint8_t MFRC522_ToCard(uint8_t Command, uint8_t *pInData, uint8_t InLenByte,  
                           uint8_t *pOutData, uint32_t *pOutLenBit)                 
{
    uint8_t status = MI_ERR;  
    uint8_t irqEn = 0x00;    
    uint8_t waitFor = 0x00;  
    uint8_t lastBits;        
    uint8_t n;               
    uint32_t i;              

    switch (Command) {
        case PCD_AUTHENT:
            irqEn = 0x12;
            waitFor = 0x10;
            break;
        case PCD_TRANSCEIVE:
            irqEn = 0x77;
            waitFor = 0x30;
            break;
        default:
            break;
    }

    Write_MFRC522(ComIEnReg, irqEn | 0x80);  // 使能中断
    ClearBitMask(ComIrqReg, 0x80);           // 清除所有中断标志
    Write_MFRC522(CommandReg, PCD_IDLE);     // 取消当前命令
    SetBitMask(FIFOLevelReg, 0x80);          // 清空FIFO

    // 写入发送数据到FIFO
    for (i = 0; i < InLenByte; i++) {
        Write_MFRC522(FIFODataReg, pInData[i]);
    }

    // 发送命令
    Write_MFRC522(CommandReg, Command);
    if (Command == PCD_TRANSCEIVE) {
        SetBitMask(BitFramingReg, 0x80);  // 开启发送后自动CRC
    }

    // 等待中断
    i = 1500;  // 超时时间（约25ms）
    do {
        n = Read_MFRC522(ComIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitFor));

    ClearBitMask(BitFramingReg, 0x80);  // 关闭自动CRC

    if (i != 0) {
        // 检查错误
        if (!(Read_MFRC522(ErrorReg) & 0x1B)) {
            status = MI_OK;
            if (n & irqEn & 0x01) {
                status = MI_NOTAGERR;  // 无卡
            }

            // 接收数据处理
            if (Command == PCD_TRANSCEIVE) {
                n = Read_MFRC522(FIFOLevelReg);
                lastBits = Read_MFRC522(ControlReg) & 0x07;
                if (lastBits) {
                    *pOutLenBit = (n - 1) * 8 + lastBits;
                } else {
                    *pOutLenBit = n * 8;
                }

                if (n == 0) {
                    n = 1;
                }
                if (n > 18) {
                    n = 18;
                }

                // 读取FIFO数据
                for (i = 0; i < n; i++) {
                    pOutData[i] = Read_MFRC522(FIFODataReg);
                }
            }
        } else {
            status = MI_ERR;  // 通信错误
        }
    }

    SetBitMask(ControlReg, 0x80);  // 停止定时器
    Write_MFRC522(CommandReg, PCD_IDLE);

    return status;
}

/********************* CRC计算 *********************/
//参数：uint8_t *pIndata-数据指针； uint8_t len-数据长度； uint8_t *pOutData-CRC结果指针
static void CalulateCRC(uint8_t *pIndata, uint8_t len, uint8_t *pOutData)  
{
    uint8_t i, n;  // unsigned char → uint8_t
    ClearBitMask(DivIrqReg, 0x04);  // 清除CRC中断标志
    Write_MFRC522(CommandReg, PCD_IDLE);
    SetBitMask(FIFOLevelReg, 0x80);  // 清空FIFO

    // 写入数据到FIFO
    for (i = 0; i < len; i++) {
        Write_MFRC522(FIFODataReg, pIndata[i]);
    }

    Write_MFRC522(CommandReg, PCD_CALCCRC);  // 启动CRC计算

    // 等待CRC计算完成
    i = 0xFF;
    do {
        n = Read_MFRC522(DivIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x04));

    // 读取CRC结果
    pOutData[0] = Read_MFRC522(CRCResultRegL);
    pOutData[1] = Read_MFRC522(CRCResultRegM);
}

/********************* 寻卡 *********************/
uint8_t MFRC522_Request(uint8_t req_code, uint8_t *pTagType)  
{
    uint8_t status;                                            
    uint32_t unLen;                                           
    uint8_t ucComMF522Buf[18] = {0};                          

    ClearBitMask(Status2Reg, 0x08);
    Write_MFRC522(BitFramingReg, 0x07);  // 发送7位
    SetBitMask(TxControlReg, 0x03);      // 开启发送

    ucComMF522Buf[0] = req_code;
    status = MFRC522_ToCard(PCD_TRANSCEIVE, ucComMF522Buf, 1, ucComMF522Buf, &unLen);

    if ((status == MI_OK) && (unLen == 0x10)) {
        *pTagType = ucComMF522Buf[0];
        *(pTagType + 1) = ucComMF522Buf[1];
    } else {
        status = MI_ERR;
    }

    return status;
}

/********************* 防冲撞（获取卡号） *********************/
//参数：pSnr为卡号
uint8_t MFRC522_Anticoll(uint8_t *pSnr)  
{
    uint8_t status;                       
    uint8_t i, snr_check = 0;            
    uint32_t unLen;                     
    uint8_t ucComMF522Buf[18] = {0};     

    ClearBitMask(Status2Reg, 0x08);
    Write_MFRC522(BitFramingReg, 0x00);  // 发送8位
    ClearBitMask(CollReg, 0x80);          // 清除冲突标志

    ucComMF522Buf[0] = PICC_ANTICOLL1;
    ucComMF522Buf[1] = 0x20;

    status = MFRC522_ToCard(PCD_TRANSCEIVE, ucComMF522Buf, 2, ucComMF522Buf, &unLen);

    if (status == MI_OK) {
        // 提取4字节卡号
        for (i = 0; i < 4; i++) {
            *(pSnr + i) = ucComMF522Buf[i];
            snr_check ^= ucComMF522Buf[i];
        }
        // 校验卡号
        if (snr_check != ucComMF522Buf[4]) {
            status = MI_ERR;
        }
    }

    SetBitMask(CollReg, 0x80);  // 设置冲突标志
    return status;
}

/********************* 选卡 *********************/
uint8_t MFRC522_SelectTag(uint8_t *pSnr)  
{
    uint8_t status;                       
    uint8_t i;                           
    uint32_t unLen;                       
    uint8_t ucComMF522Buf[18] = {0};      

    ucComMF522Buf[0] = PICC_ANTICOLL1;
    ucComMF522Buf[1] = 0x70;
    ucComMF522Buf[6] = 0;

    // 写入卡号
    for (i = 0; i < 4; i++) {
        ucComMF522Buf[i + 2] = *(pSnr + i);
        ucComMF522Buf[6] ^= *(pSnr + i);
    }

    // 计算CRC
    CalulateCRC(ucComMF522Buf, 7, &ucComMF522Buf[7]);

    ClearBitMask(Status2Reg, 0x08);
    status = MFRC522_ToCard(PCD_TRANSCEIVE, ucComMF522Buf, 9, ucComMF522Buf, &unLen);

    if ((status == MI_OK) && (unLen == 0x18)) {
        status = MI_OK;
    } else {
        status = MI_ERR;
    }

    return status;
}

/********************* 验证密钥 *********************/
//参数：auth_mode-验证模式； addr-块地址； pKey-密钥； pSnr-卡号
uint8_t MFRC522_AuthState(uint8_t auth_mode, uint8_t addr, uint8_t *pKey, uint8_t *pSnr)  
{
    uint8_t status;                                                                       
    uint32_t unLen;                                                                      
    uint8_t i, ucComMF522Buf[18] = {0};                                                  

    ucComMF522Buf[0] = auth_mode;
    ucComMF522Buf[1] = addr;

    // 写入密钥（6字节）
    for (i = 0; i < 6; i++) {
        ucComMF522Buf[i + 2] = *(pKey + i);
    }

    // 写入卡号（4字节）
    for (i = 0; i < 4; i++) {
        ucComMF522Buf[i + 8] = *(pSnr + i);
    }

    status = MFRC522_ToCard(PCD_AUTHENT, ucComMF522Buf, 12, ucComMF522Buf, &unLen);

    if ((status != MI_OK) || (!(Read_MFRC522(Status2Reg) & 0x08))) {
        status = MI_ERR;
    }

    return status;
}

/********************* 读块 *********************/
//参数：addr-块地址； pData-数据指针
uint8_t MFRC522_Read(uint8_t addr, uint8_t *pData)  
{
    uint8_t status;                                   
    uint32_t unLen;                                  
    uint8_t i, ucComMF522Buf[18] = {0};              

    ucComMF522Buf[0] = PICC_READ;
    ucComMF522Buf[1] = addr;

    // 计算CRC
    CalulateCRC(ucComMF522Buf, 2, &ucComMF522Buf[2]);

    status = MFRC522_ToCard(PCD_TRANSCEIVE, ucComMF522Buf, 4, ucComMF522Buf, &unLen);

    if ((status == MI_OK) && (unLen == 0x90)) {
        // 读取16字节数据
        for (i = 0; i < 16; i++) {
            *(pData + i) = ucComMF522Buf[i];
        }
    } else {
        status = MI_ERR;
    }

    return status;
}

/********************* 写块 *********************/
//参数：addr-块地址； pData-数据指针
uint8_t MFRC522_Write(uint8_t addr, uint8_t *pData)  
{
    uint8_t status;                                   
    uint32_t unLen;                                  
    uint8_t i, ucComMF522Buf[18] = {0};            

    ucComMF522Buf[0] = PICC_WRITE;
    ucComMF522Buf[1] = addr;

    // 计算CRC
    CalulateCRC(ucComMF522Buf, 2, &ucComMF522Buf[2]);

    // 发送写块命令
    status = MFRC522_ToCard(PCD_TRANSCEIVE, ucComMF522Buf, 4, ucComMF522Buf, &unLen);

    if ((status != MI_OK) || (unLen != 4) || ((ucComMF522Buf[0] & 0x0F) != 0x0A)) {
        status = MI_ERR;
    }

    // 发送16字节数据
    if (status == MI_OK) {
        for (i = 0; i < 16; i++) {
            ucComMF522Buf[i] = *(pData + i);
        }

        // 计算CRC
        CalulateCRC(ucComMF522Buf, 16, &ucComMF522Buf[16]);

        // 发送数据
        status = MFRC522_ToCard(PCD_TRANSCEIVE, ucComMF522Buf, 18, ucComMF522Buf, &unLen);

        if ((status != MI_OK) || (unLen != 4) || ((ucComMF522Buf[0] & 0x0F) != 0x0A)) {
            status = MI_ERR;
        }
    }

    return status;
}

/********************* 休眠卡 *********************/
uint8_t MFRC522_Halt(void)  
{
    uint32_t unLen;        
    uint8_t ucComMF522Buf[4] = {0};  

    ucComMF522Buf[0] = PICC_HALT;
    ucComMF522Buf[1] = 0;

    // 计算CRC
    CalulateCRC(ucComMF522Buf, 2, &ucComMF522Buf[2]);

    // 发送休眠命令
    MFRC522_ToCard(PCD_TRANSCEIVE, ucComMF522Buf, 4, ucComMF522Buf, &unLen);

    return MI_OK;
}