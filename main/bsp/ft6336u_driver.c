#include "ft6336u_driver.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <stdint.h>

#define TAG "ft6336u"
#define FT6336U_ADDR    0x38

static i2c_master_bus_handle_t i2c_bus_handle = NULL;       //i2c总线句柄
static i2c_master_dev_handle_t i2c_dev_handle = NULL;       //i2c从机设备句柄
static volatile bool ft6336u_int_flag = false;              //INT引脚状态

/*=======================ft6336u 驱动初始化=====================================
 * 功能：初始化 FT6336U 触摸屏
 * 参数：sda、scl
 * 返回：esp_err_t 错误类型
 *============================================================================*/
esp_err_t ft6336u_driver_init(gpio_num_t sda,gpio_num_t scl)
{
    esp_err_t ret;
    
    // 初始化 I2C 总线
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,         //使用默认时钟
        .sda_io_num = sda,                         //SDA 引脚
        .scl_io_num = scl,                         //SCL 引脚
        .i2c_port   = -1,                          //-1 表示自动选择 i2c 端口
        .glitch_ignore_cnt = 7,                     //滤波周期，典型值为 7
        .flags.enable_internal_pullup = 1,         //启用内部上拉电阻，消除警告
    };
    
    ESP_LOGI(TAG, "Creating I2C bus for FT6336U (SDA=%d, SCL=%d)", sda, scl);
    //创建总线，应用配置
    ret = i2c_new_master_bus(&bus_cfg,&i2c_bus_handle);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create I2C bus: %d", ret);
        return ret;
    }

    //添加一个从机设备
    i2c_device_config_t dev_cfg = { 
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,      //选择从设备的地址长度
        .device_address  = FT6336U_ADDR,            //设备地址
        .scl_speed_hz    = 400*1000,                //I2C 通信频率，400K 为快速模式
        .scl_wait_us     = 0,                       //超时等待时间，0 表示使用默认值
    };
    
    ESP_LOGI(TAG, "Adding FT6336U device (ADDR: 0x%02X)", FT6336U_ADDR);
    ret = i2c_master_bus_add_device(i2c_bus_handle,&dev_cfg,&i2c_dev_handle);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add FT6336U device: %d", ret);
        return ret;
    }

    
    uint8_t write_data_buf[2];              //写入数据的缓存区，可存 2 个 8 位数据（2 字节）
    write_data_buf[0] = 0xA3;               //要读取的寄存器地址（厂商 ID 寄存器）

    uint8_t read_data_buf[1];               //接收数据的缓存区，可接收 1 个字节数据

    bool touch_detected = false;            //是否检测到触摸屏
    
    //读取 0xA3 寄存器 (存的是芯片的厂商 ID)，如果读取成功，则打印芯片厂商 ID
    ESP_LOGI(TAG, "Reading chip vendor ID from register 0xA3...");
    ret = i2c_master_transmit_receive(  i2c_dev_handle,             //设备句柄
                                        &write_data_buf[0],         //发送的数据：寄存器地址 0xA3
                                        1,                          //发送长度：1 字节（仅 8 位地址）
                                        &read_data_buf[0],          //接收缓冲区：存放读取到的厂商 ID
                                        1,                          //接收长度：1 字节
                                        500);                       //超时等待时间
    if(ret == ESP_OK) 
    {
        touch_detected = true;
        ESP_LOGI(TAG, "FT6336U detected! Vendor ID: 0x%02X", read_data_buf[0]);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read FT6336U vendor ID: %d", ret);
        ESP_LOGW(TAG, "Please check I2C connections and pull-up resistors");
    }
    
    write_data_buf[0] = 0xA4;               //要写入数据的寄存器地址（模式控制寄存器）
    write_data_buf[1] = 0x00;               //要写入的寄存器值（0x00 表示启用轮询模式）
    //发送数据给 ft6336u，使其 0xA4 寄存器写入 0 值，启用中断轮询模式
    ESP_LOGI(TAG, "Configuring FT6336U to polling mode");
    ret = i2c_master_transmit(i2c_dev_handle,&write_data_buf[0],2,500);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure FT6336U mode: %d", ret);
    }
    
    return touch_detected?ESP_OK:ESP_FAIL;
}

/*=======================ft6336u读取触摸屏数据==================================
 * 功能：读取触摸屏数据
 * 参数：x坐标；y坐标；state：触摸状态 0松手 1按下。
 * 返回：无
 *============================================================================*/
void ft6336u_read(int16_t *x,int16_t *y,int *state)
{
    static int16_t last_x = 0;  // 上一个x坐标
    static int16_t last_y = 0;  // 上一个y坐标

    uint8_t reg_addr[1];        //寄存器地址
    uint8_t data_coord[5];      //坐标
    if(!ft6336u_int_flag)       //如果没有检测到INT引脚有下降沿，直接返回，不调用i2c函数节省操作时间
    {
        *x = last_x;
        *y = last_y;
        *state = 0;
        return;
    }
    //读取触点个数、坐标
    reg_addr[0] = 0x02;
    i2c_master_transmit_receive(i2c_dev_handle, &reg_addr[0], 1, &data_coord[0], 5, 500);
    if ((data_coord[0]&0x0f) != 1) {    //如果触点个数不等于1，我们不处理
        *x = last_x;
        *y = last_y;
        *state = 0;
        return;
    }
    //计算触点x坐标
    int16_t current_x = ((data_coord[1] & 0x0F) << 8) | (data_coord[2] & 0xFF);
    //计算触点Y坐标
    int16_t current_y = ((data_coord[3] & 0x0F) << 8) | (data_coord[4] & 0xFF);
    if(last_x != current_x || current_y != last_y)
    {
        last_x = current_x;
        last_y = current_y;
    }
    *x = last_x;
    *y = last_y;
    *state = 1;    //1表示有检测到按下
}

/*=========================INT中断引脚通知=================================
 * 功能：处理INT中断引脚通知
 * 参数：是否检测到中断；  true：是,false：否
 * 返回：无
 *=========================================================================*/
void ft6336u_interrupt_info(bool flag)
{
    ft6336u_int_flag = flag;
}
