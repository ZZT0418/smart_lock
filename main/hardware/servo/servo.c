#include "servo.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "servo"
/************************* 舵机控制接口 *************************/
// 功能：验证成功后，舵机旋转开锁（示例：旋转90度，延时5秒复位）
void servo_unlock(void)
{
    // ********** 此处添加舵机驱动代码 **********
    ESP_LOGI(TAG, "验证成功，门已开锁！");
    servo_set_angle(90); // 你的舵机旋转代码
    vTaskDelay(pdMS_TO_TICKS(5000));
    servo_set_angle(0);  // 舵机复位
    ESP_LOGI(TAG, "门已上锁！");
}

void servo_init(void)
{
    //配置ledc定时器
    ledc_timer_config_t timer_cfg ={
        .clk_cfg = LEDC_AUTO_CLK,                   //使用自动时钟
        .duty_resolution = LEDC_TIMER_10_BIT,       //10位占空比,分辨率1024
        .freq_hz = 50,                              //频率50hz,则周期为20ms
        .speed_mode = LEDC_LOW_SPEED_MODE,         //使用高速模式
        .timer_num = LEDC_TIMER_0,                  //使用定时器0
    };
    //初始化ledc定时器
    ledc_timer_config(&timer_cfg);

    //配置ledc通道
    ledc_channel_config_t channel_cfg ={
        .channel = LEDC_CHANNEL_0,                  //通道0
        .duty = 0,                                  //占空比
        .gpio_num = GPIO_NUM_18,                    //GPIO18
        .intr_type = LEDC_INTR_DISABLE,             //不使用中断
        .speed_mode = LEDC_LOW_SPEED_MODE,         //使用高速模式
        .timer_sel = LEDC_TIMER_0,                  //使用定时器0
    };
    //初始化ledc通道
    ledc_channel_config(&channel_cfg);

}

/*=============================set_pwm_duty======================================
 * @brief 设置占空比 
 * @param duty 占空比
 * @return 无
 ===========================================================================*/
void set_pwm_duty(uint32_t duty)
{
    //设置占空比
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    //更新占空比
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/*=============================servo_set_angle===============================
 * @brief 设置舵机角度，支持0~180°
 * @param angle 角度
 * @return 无
 ===========================================================================*/
void servo_set_angle(uint8_t angle)
{
    // 边界保护：限制角度0~180°
    if(angle > 180) angle = 180;
    
    // 线性计算duty：从0°(25)到180°(125)，每度增加(125-25)/180≈0.555
    uint32_t duty = 25 + (angle * 100) / 180;
    
    set_pwm_duty(duty);

}