#include "servo.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "servo"
static QueueHandle_t xUnlockQueue = NULL;
static SemaphoreHandle_t xExecMutex = NULL;
static LockControlState_t g_lock_state = LOCK_STATE_IDLE;       // 锁门状态初始为闲置

int64_t Current_unlock_time = 0;

/************************* 舵机控制接口 *************************/
// 功能：验证成功后，舵机旋转开锁，并获取开锁时间戳
// @return : 返回int64类型的UTC毫秒级时间戳
void servo_unlock(void)
{
    // ********** 此处添加舵机驱动代码 **********
    servo_set_angle(90); // 你的舵机旋转代码
    Lock_state = LOCK_STATE_POEN;

    // 获取当前时间戳
    // esp_timer_get_time() 获取微秒级时间，除以1000转为毫秒级
    // 强制转换为int64_t，确保数据类型与OneNet平台要求一致
    Current_unlock_time = (int64_t)esp_timer_get_time() / 1000;

    vTaskDelay(pdMS_TO_TICKS(5000));
    servo_set_angle(0);  // 舵机复位
    ESP_LOGI(TAG, "门已上锁！");
    Lock_state = LOCK_STATE_CLOSE;
}
/**
 * @brief 内部开锁执行任务
 * 该任务专门负责处理队列中的开锁请求，持有信号量并执行耗时动作
 */
static void unlock_executor_task(void *pvParameters) {
    Unlockways_t way;
    
    while (1) {
        // 阻塞等待队列中的开锁请求
        if (xQueueReceive(xUnlockQueue, &way, portMAX_DELAY) == pdTRUE) { 
            // 1. 获取执行权 (此时不在 UI 上下文，安全)
            if (xSemaphoreTake(xExecMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                
                // 2. 状态检查
                if (g_lock_state == LOCK_STATE_BUSY) {
                    ESP_LOGW(TAG, "丢弃重复请求: Way=%d", way);
                    xSemaphoreGive(xExecMutex);
                    continue;
                }

                g_lock_state = LOCK_STATE_BUSY;
                ESP_LOGI(TAG, ">>> 开始执行开锁 (Way: %d)", way);
                
                // 3. 更新全局变量供日志使用
                extern Unlockways_t Unlock_ways;
                Unlock_ways = way;

                // 4. 执行物理动作
                servo_unlock(); 
                
                // 在执行完开锁后，检查队列里是否还有因为“排队”而积压的请求
                // 如果有，全部清空，避免连续开锁
                while (uxQueueMessagesWaiting(xUnlockQueue) > 0) {
                    Unlockways_t discarded_way;
                    xQueueReceive(xUnlockQueue, &discarded_way, 0);
                    ESP_LOGW(TAG, "清除积压的开锁请求: Way=%d", discarded_way);
                }

                // 5. 恢复状态
                g_lock_state = LOCK_STATE_IDLE;
                ESP_LOGI(TAG, ">>> 开锁流程结束");
                
                xSemaphoreGive(xExecMutex);
            }
        }
    }
}

// 互斥信号量与队列初始化,用于保护锁状态的访问，防止多线程竞争导致状态混乱
void servo_mutex_lock(void)
{
    // 创建互斥量，保护执行过程
    xExecMutex = xSemaphoreCreateMutex();
    // 创建队列，深度为 5，存储开锁方式
    xUnlockQueue = xQueueCreate(5, sizeof(Unlockways_t));
    
    if (xExecMutex && xUnlockQueue) {
        // 创建执行任务，优先级设为 3 (中等优先级，低于 LVGL 但高于空闲任务)
        xTaskCreate(unlock_executor_task, "unlock_exec", 4096, NULL, 3, NULL);
        ESP_LOGI(TAG, "锁管理器初始化成功");
    } else {
        ESP_LOGE(TAG, "创建信号量或队列失败");
    }
}



/************************* 统一开锁请求接口 *************************/
/**
 * @brief 统一开锁请求处理（异步非阻塞）
 * @param way: 开锁方式 (指纹/蓝牙/密码等)
 * @return: true 请求已入队, false 系统忙或队列满
 * */
bool lock_manager_request_unlock(Unlockways_t way)
{
    if (xUnlockQueue == NULL) return false;
    
    // 非阻塞发送：如果队列满了，说明系统太忙，直接拒绝
    // 注意：这里不使用信号量，因为 xQueueSend 非常快且安全
    if (xQueueSend(xUnlockQueue, &way, 0) == pdPASS) {
        ESP_LOGI(TAG, "请求已入队: Way=%d", way);
        return true;
    } else {
        ESP_LOGW(TAG, "请求被拒绝: 队列满或系统忙");
        return false;
    }
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