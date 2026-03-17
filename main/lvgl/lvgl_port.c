#include "lvgl_port.h"              //自定义头文件
#include "esp_lvgl_port.h"          //ESP-IDF 官方提供的 LVGL 移植辅助组件
#include "esp_lcd_panel_io.h"       //LCD 接口抽象层
#include "esp_lcd_panel_vendor.h"   //供应商特定的 LCD 控制器驱动（如 ST7789）
#include "esp_lcd_panel_ops.h"      //LCD 面板操作函数
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "xl9555.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ft6336u_driver.h"

#define TAG             "lvgl_port"
#define LCD_HEIGHT       240            // LCD的高
#define LCD_WIDTH        320            // LCD的宽

#define LCD_RST_IO      IO1_3

static esp_lcd_panel_handle_t panel_handle = NULL;      //LCD 面板操作句柄
static esp_lcd_panel_io_handle_t io_handle = NULL;      //LCD 接口操作句柄
static lv_display_t *lvgl_disp = NULL;                  //LVGL显示设备句柄

/*===========================触摸坐标获取回调函数===============================
 * @param  indev 输入设备
 * @param  data 需要设置的触摸坐标数据
 * @return 无
 *===========================================================================*/ 
void indev_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    int16_t x = 0,y = 0;
    int state = 0;
    ft6336u_read(&x,&y,&state);    //调用ft6336u驱动获取触摸坐标
    //横屏变竖屏，需要翻转X/Y坐标系
    data->point.x = LCD_WIDTH - 1 - y;
    data->point.y = x;
    data->state = state;
}


/*===========================lcd显示硬件初始化函数============================
 * 功能：lcd相关显示硬件初始化（8080 总线和 ST7789 控制器）。
 * 步骤：1.初始化8080总线；   2.初始化LCD_st7789
 * 参数：无
 * 返回：无
 *===========================================================================*/
void lvgl_display_hardware_init(void)
{
    //初始化8080总线
    ESP_LOGI(TAG, "初始化8080总线");
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,             // 使用默认的LCD时钟源
        .dc_gpio_num = GPIO_NUM_1,                  // 数据总线控制引脚
        .wr_gpio_num = GPIO_NUM_41,                 // 写入总线控制引脚
        .data_gpio_nums = {
            GPIO_NUM_40,                            // 8个数据总线引脚
            GPIO_NUM_38,
            GPIO_NUM_39,
            GPIO_NUM_48,
            GPIO_NUM_45,
            GPIO_NUM_21,
            GPIO_NUM_47,
            GPIO_NUM_14,
        },
        .bus_width = 8,                             // 总线位数
        .max_transfer_bytes =LCD_HEIGHT*LCD_WIDTH,  // 最大传输字节数,和屏幕大小有关
        .dma_burst_size = 64,                       // DMA传输字节数
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

    //配置8080接口参数
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = GPIO_NUM_2,                   // CS引脚
        .pclk_hz = 10*1000*1000,                     // 时钟频率
        .trans_queue_depth = 10,                     // 队列长度
        .dc_levels = {                               // DC引脚电平
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,                     // 数据总线电平，高电平时输出数据
        },
        .lcd_cmd_bits = 8,                          // 命令总线位数
        .lcd_param_bits = 8,                        // 参数总线位数
        .flags = { 
            .swap_color_bytes=1,                    // 交换颜色字节
        },
    };
    //应用8080接口配置
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

    //初始化LCD_st7789
    ESP_LOGI(TAG, "初始化LCD_st7789");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,                  //LCD屏复位引脚接到了XL9555上，所以无法直接控制，这里给NCv
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,     //颜色传输顺序时RGB顺序 
        .bits_per_pixel = 16,                           //颜色采用16位，也是就是RGB565，R占5位，G占6位，B占5位
    };
    //应用LCD_st7789配置
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

}

/*===========================lvgl图像界面初始化函数============================
 * 功能：用来调用esp_lvgl_port里面相关的api接口，完成lvgl移植
 * 步骤：1.初始化lvgl硬件；   2.初始化lvgl
 * 参数：无
 * 返回：无
 *===========================================================================*/
void my_lvgl_port_init(void)
{
    xl9555_pin_write(LCD_RST_IO,0);         //调用xl9555接口，复位lcd屏
    vTaskDelay(pdMS_TO_TICKS(100));         //等待100ms
    xl9555_pin_write(LCD_RST_IO,1); 

    lvgl_display_hardware_init();                           //初始化lvgl显示硬件
    ft6336u_driver_init(GPIO_NUM_13,GPIO_NUM_12);                  //初始化ft6336u触摸屏驱动
    
    //LCD 面板基础配置
    esp_lcd_panel_init(panel_handle);                       //向屏写入初始化命令
    esp_lcd_panel_swap_xy(panel_handle, true);              //交换X/Y坐标系
    esp_lcd_panel_mirror(panel_handle, false, true);        //x方向不设置镜像.y方向设置镜像
    esp_lcd_panel_disp_on_off(panel_handle, true);          //打开显示

    //初始化lvgl
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,         //LVGL任务优先级
        .task_stack = 8192,         //LVGL任务栈大小
        .task_affinity = 1,         //LVGL任务运行在哪个内核上
        .task_max_sleep_ms = 500,   //LVGL任务休眠时间
        .timer_period_ms = 5        //LVGL定时器时间间隔
    };
    lvgl_port_init(&lvgl_cfg);

    //添加LCD屏幕
    ESP_LOGD(TAG, "添加LCD屏幕");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_WIDTH * 80,
        .double_buffer = 0,
        .hres = LCD_WIDTH,
        .vres = LCD_HEIGHT,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = true,      //交换xy轴 
            .mirror_x = false,     //镜像x轴
            .mirror_y = true,     //镜像y轴
        },
        .flags = {
            .buff_dma = true,
        }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    /******************注意一下是新增的触摸内容*********************/
    lvgl_port_lock(0);
    lv_indev_t *lvgl_touch_indev = lv_indev_create();
    lv_indev_set_type(lvgl_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_touch_indev, indev_read);
    lvgl_port_unlock();

}