#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h" 
#include "esp_spiffs.h"                            
#include "esp_lvgl_port.h"          //官方lvgl相关组件头文件
#include "lvgl_port.h"              //lcd与lvgl初始化相关头文件
#include "gui_guider.h"             //ui界面相关头文件
#include "custom.h"                 //ui用户自定义头文件
#include "events_init.h"
#include "xl9555.h"
#include "button.h"
#include "ft6336u_driver.h"
#include "servo.h"   
#include "as608.h"
#include "finger_manager.h"
#include "ble_unlock.h"
#include "onenet_mqtt.h" 
#include "onenet_dm.h"
#include "wifi_ap.h"

// #include "rc522.h"
// static const uint8_t AUTHORIZED_UID[4] = {0x63, 0xD3, 0x2A, 0x07};

#define TAG "main"

uint32_t current_addr = 0xFFFFFFFF; // AS608默认地址
extern uint16_t ValidN;//模块内有效模板个数
lv_ui guider_ui;

#define XL9555_SDA              GPIO_NUM_10         //xl9555的SDA引脚
#define XL9555_SCL              GPIO_NUM_11         //xl9555的SCL引脚
#define INT_IO                  GPIO_NUM_17         //xl9555的中断引脚
#define LCD_RST_IO              IO1_3               //LCD复位引脚
#define LCD_BL_IO               IO1_2               //LCD背光引脚
#define TP_RST_IO               IO1_0               //触摸屏复位引脚
#define TP_INT_IO               IO1_1               //触摸屏中断引脚
#define KEY0_IO                 IO0_1                 //key0引脚
#define KEY1_IO                 IO0_2                 //key1引脚
#define KEY2_IO                 IO0_3                 //key2引脚
#define KEY3_IO                 IO0_4                 //key3引脚

//按位表示
static volatile uint16_t xl9555_io_level = 0xFFFF;
/**=================xl9555中断回调函数===================
 * @brief xl9555中断回调函数
 * @param io_num 中断引脚编号
 * @param level 中断引脚电平
 *======================================================*/
void xl9555_input_cb(uint16_t io_num,int level)
{
    if(level)
        xl9555_io_level |= io_num;
    else
        xl9555_io_level &= ~io_num;
    if(io_num == TP_INT_IO)         //如果检测到TP_INT引脚有电平变化
    {
        ft6336u_interrupt_info(!level);    //调用ft6336u_int_info通知ft6336u
    }
}
/**=================按键获取电平回调函数=======================
 * @brief 按键获取电平回调函数
 * @param gpio 按键引脚编号
 * @return 无
 *======================================================*/
int get_button_level(int gpio)
{
    return (xl9555_io_level&gpio)?1:0;
}


/**=================按键0长按回调函数=======================
 * @brief 按键长按回调函数,启动wifi配网
 * @param 无
 * @return 无
 *======================================================*/
void long_press0(int gpio)
{
    ESP_LOGI(TAG, "进入配网模式...");
    // 启动配网模式
    My_WIFI_Init_AP();
    start_http_server();
}

/**=================按键1长按回调函数=======================
 * @brief 按键长按回调函数,添加指纹
 * @param 无
 * @return 无
 *======================================================*/
void long_press1(int gpio)
{
    finger_add();
}

/**=================按键2长按回调函数=======================
 * @brief 按键长按回调函数,长按删除最后录入的一个指纹
 * @param 无
 * @return 无
 *======================================================*/
void long_press2(int gpio)
{
    finger_delete_last();
}




/**=================按键初始化===========================
 * @brief 按键key0~key3的初始化
 * @param 无
 * @return 无
 *======================================================*/
void button0_init(void)
{
    button_config_t button0_cfg =
    {
        .active_level = 0,                              //引脚电平为低时为按下
        .getlevel_cb = get_button_level,                //引脚电平获取函数
        .gpio_num = KEY0_IO,                            //引脚编号,key0
        .long_cb = long_press0,                          //长按回调函数
        .long_press_time = 3000,                        //长按时间
    };
    button_event_set(&button0_cfg);                     //应用按键配置
} 

void button1_init(void)
{
    button_config_t button1_cfg =
    {
        .active_level = 0,                              //引脚电平为低时为按下
        .getlevel_cb = get_button_level,                //引脚电平获取函数
        .gpio_num = KEY1_IO,                            //引脚编号,key1
        .long_cb = long_press1,                          //长按回调函数
        .long_press_time = 3000,                        //长按时间
    };
    button_event_set(&button1_cfg);                     //应用按键配置
}  

void button2_init(void)
{
    button_config_t button2_cfg =
    {
        .active_level = 0,                              //引脚电平为低时为按下
        .getlevel_cb = get_button_level,                //引脚电平获取函数
        .gpio_num = KEY2_IO,                            //引脚编号,key2
        .long_cb = long_press2,                          //长按回调函数
        .long_press_time = 3000,                        //长按时间
    };
    button_event_set(&button2_cfg);                     //应用按键配置
} 


/**=================卡片授权验证函数=======================
 * @brief 验证卡片 UID 是否已授权
 * @param card_uid 卡片 UID 数组（4 字节）
 * @return true-已授权，false-未授权
 *======================================================*/
// static bool card_is_authorized(const uint8_t *card_uid)
// {
//     if (card_uid == NULL) {
//         return false;
//     }
    
//     // 逐字节比较 UID
//     for (int i = 0; i < 4; i++) {
//         if (card_uid[i] != AUTHORIZED_UID[i]) {
//             return false;
//         }
//     }
    
//     return true;
// }
// uint8_t buf[20] = {0};  // 数据缓冲区
// void rc522_task(void *pvParameters)
// {
//     uint8_t status;
//     unsigned int temp, i;

//     // 初始化 RC522
//     esp_err_t ret = MFRC522_Init();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "RC522 初始化失败！");
//         vTaskDelete(NULL);
//         return;
//     }
//     ESP_LOGI(TAG, "RC522 初始化成功！");
//     ESP_LOGI(TAG, "授权卡片 UID: %02X %02X %02X %02X", 
//              AUTHORIZED_UID[0], AUTHORIZED_UID[1], AUTHORIZED_UID[2], AUTHORIZED_UID[3]);

//     while (1) {
//         // 1. 寻卡（寻所有卡）
//         status = MFRC522_Request(PICC_REQALL, buf);
//         if (status != MI_OK) {
//             RC522_DELAY_MS(100);
//             continue;
//         }

//         // 打印卡类型
//         ESP_LOGI(TAG, "卡类型：");
//         for (i = 0; i < 2; i++) {
//             temp = buf[i];
//             printf("%02X ", temp);
//         }
//         printf("\n");

//         // 2. 防冲撞（获取卡号）
//         status = MFRC522_Anticoll(buf);
//         if (status != MI_OK) {
//             RC522_DELAY_MS(100);
//             continue;
//         }

//         // 打印卡号
//         ESP_LOGI(TAG, "卡号：");
//         uint8_t card_uid[4];
//         for (i = 0; i < 4; i++) {
//             temp = buf[i];
//             printf("%02X ", temp);
//             card_uid[i] = buf[i];  // 保存 UID 用于验证
//         }
//         printf("\n");

//         // 3. 验证卡片是否授权
//         if (card_is_authorized(card_uid)) {
//             ESP_LOGI(TAG, ">>> 卡片授权通过！执行开锁 <<<");
            
//             // 3.1 选卡
//             MFRC522_SelectTag(buf);
            
//             // 3.2 认证扇区 0（使用默认密钥 A）
//             uint8_t key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
//             status = MFRC522_AuthState(PICC_AUTHENT1A, 0x00, key, buf);
//             if (status == MI_OK) {
//                 ESP_LOGI(TAG, "扇区认证成功");
                
//                 // 3.3 读取块数据（可选）
//                 uint8_t block_data[16];
//                 status = MFRC522_Read(0x01, block_data);
//                 if (status == MI_OK) {
//                     ESP_LOGI(TAG, "扇区 0 块 1 数据：");
//                     for (int j = 0; j < 16; j++) {
//                         printf("%02X ", block_data[j]);
//                     }
//                     printf("\n");
//                 }
                
//                 // 3.4 驱动舵机开锁
//                 servo_set_angle(90);
//                 ESP_LOGI(TAG, "舵机角度：90 度（开锁状态）");
//                 RC522_DELAY_MS(5000);  // 保持开锁 5 秒
                
//                 // 3.5 关闭舵机
//                 servo_set_angle(0);
//                 ESP_LOGI(TAG, "舵机角度：0 度（关锁状态）");
//             } else {
//                 ESP_LOGE(TAG, "扇区认证失败");
//             }
            
//             // 3.6 休眠卡片
//             MFRC522_Halt();
//         } else {
//             ESP_LOGW(TAG, ">>> 卡片未授权，拒绝开锁 <<<");
//             // 选卡并休眠
//             MFRC522_SelectTag(buf);
//             MFRC522_Halt();
//         }

//         RC522_DELAY_MS(500);  // 延时避免重复读取
//     }
// }

/**
 * @brief AS608 指纹识别任务函数
 * 
 * 该任务等待队列中的指纹识别命令，收到命令后执行指纹识别操作
 * 
 * @param pvParameters 任务参数（未使用）
 */
void as608_task(void *pvParameters)
{
     //初始化AS608与串口通信
    esp_err_t  esp32_ret= esp32_uart_init();
    if (esp32_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "UART1初始化失败");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(500)); // 延时稳定模块
    // 检测AS608模块是否在线（采集图像指令测试）
    uint8_t as608_ret = as608_get_image();
    if (as608_ret == ENSURE_OK || as608_ret == ENSURE_NO_FINGER)
    {
        ESP_LOGI(TAG, "AS608模块通信正常！");
    }
    else
    {
        ESP_LOGE(TAG, "AS608模块通信失败：%s", as608_get_err_msg(as608_ret));
        return;
    }
    while (1) 
    { 
        finger_verify();
        vTaskDelay(pdMS_TO_TICKS(300)); // 扫描间隔300ms，降低CPU占用
    }
}


void app_main(void)
{
    //xl9555初始化(用于lcd与按键等扩展IO的驱动)
    xl9555_init(XL9555_SDA, XL9555_SCL, INT_IO, xl9555_input_cb);
    xl9555_ioconfig((~(LCD_RST_IO | LCD_BL_IO | TP_RST_IO))&0xFFFF);  //配置引脚为输出模式（取反表示其位为0表示输出）

    //初始化nvs
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    //初始化舵机
    servo_init();
    //重置舵机角度
    servo_set_angle(0);

    //初始化lvgl
    my_lvgl_port_init();            //包括lcd相关显示硬件的初始化与lvgl的初始化
    lvgl_port_lock(0);
    setup_ui(&guider_ui);           //初始化ui界面
    custom_init(&guider_ui);        //初始化用户自定义
    events_init_screen (&guider_ui);
    lvgl_port_unlock();

    // 初始化蓝牙远程开锁
    esp_err_t ble_ret = ble_unlock_init();
    if (ble_ret != ESP_OK) {
        ESP_LOGE(TAG, "蓝牙初始化失败，请检查");
    }


    //key初始化
    button0_init();     //配网
    button1_init();     //添加指纹
    button2_init();     //删除指纹

    // 创建RC522任务（优先级1，栈大小4096）
    // xTaskCreate(rc522_task, "rc522_task", 4096, NULL, 1, NULL);

    // 创建AS608任务（优先级1，栈大小）
    xTaskCreate(as608_task, "as608_task", 3072, NULL, 1, NULL);
    


    



}
