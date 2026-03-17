#include "finger_manager.h"
#include "as608.h"
#include "servo.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "finger_manager"
static uint16_t g_valid_finger_num = 0; // 全局有效指纹数量



/************************* 录入指纹 *************************/
// 功能：实现指纹录入完整流程（采集2次图像→生成特征→比对→合并→存储）
// 逻辑：连续采集2次指纹，验证一致后生成模板存入指纹库第一个空位置
void finger_add(void)
{
    uint8_t ret;
    uint16_t empty_page = 0; // 指纹库空位置编号

    // 1. 获取当前有效指纹数量，确定空位置
    ret = as608_get_valid_num(&g_valid_finger_num);
    if (ret != ENSURE_OK)
    {
        ESP_LOGE(TAG, "获取有效指纹数量失败：%s", as608_get_err_msg(ret));
        return;
    }
    if (g_valid_finger_num >= AS608_MAX_STORAGE)
    {
        ESP_LOGE(TAG, "指纹库已满，无法录入（最大200枚）");
        return;
    }
    empty_page = g_valid_finger_num; // 空位置=当前有效数量（编号从0开始）
    ESP_LOGI(TAG, "开始录入指纹，空位置编号：%d", empty_page);

    // 2. 第一次采集指纹并生成特征（存入Buffer1）
    ESP_LOGI(TAG, "请按第一次指纹");
    int retry1 = 10; // 重试10次，超时退出
    while (retry1--)
    {
        ret = as608_get_image();
        if (ret == ENSURE_OK)
        {
            // 采集图像成功，生成特征
            ret = as608_gen_char(1);
            if (ret == ENSURE_OK)
            {
                ESP_LOGI(TAG, "第一次指纹采集成功");
                break;
            }
            else
            {
                ESP_LOGE(TAG, "第一次生成特征失败：%s", as608_get_err_msg(ret));
            }
        }
        else if (ret != ENSURE_NO_FINGER)
        {
            ESP_LOGE(TAG, "第一次采集图像失败：%s", as608_get_err_msg(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (retry1 < 0)
    {
        ESP_LOGE(TAG, "第一次指纹采集超时");
        return;
    }

    // 3. 第二次采集指纹并生成特征（存入Buffer2）
    ESP_LOGI(TAG, "请按第二次指纹");
    int retry2 = 10;
    while (retry2--)
    {
        ret = as608_get_image();
        if (ret == ENSURE_OK)
        {
            ret = as608_gen_char(2);
            if (ret == ENSURE_OK)
            {
                ESP_LOGI(TAG, "第二次指纹采集成功");
                break;
            }
            else
            {
                ESP_LOGE(TAG, "第二次生成特征失败：%s", as608_get_err_msg(ret));
            }
        }
        else if (ret != ENSURE_NO_FINGER)
        {
            ESP_LOGE(TAG, "第二次采集图像失败：%s", as608_get_err_msg(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (retry2 < 0)
    {
        ESP_LOGE(TAG, "第二次指纹采集超时");
        return;
    }

    // 4. 比对两次特征，验证是否为同一手指
    ESP_LOGI(TAG, "正在验证两次指纹是否一致...");
    ret = as608_match();
    if (ret != ENSURE_OK)
    {
        ESP_LOGE(TAG, "两次指纹不一致：%s", as608_get_err_msg(ret));
        return;
    }

    // 5. 合并特征生成模板
    ret = as608_reg_model();
    if (ret != ENSURE_OK)
    {
        ESP_LOGE(TAG, "特征合并失败：%s", as608_get_err_msg(ret));
        return;
    }

    // 6. 将模板存入指纹库指定空位置
    ret = as608_store_char(2, empty_page);
    if (ret == ENSURE_OK)
    {
        ESP_LOGI(TAG, "指纹录入成功！编号：%d", empty_page);
        // 更新有效指纹数量
        as608_get_valid_num(&g_valid_finger_num);
    }
    else
    {
        ESP_LOGE(TAG, "存储指纹失败：%s", as608_get_err_msg(ret));
    }
}

/************************* 删除最后一个指纹 *************************/
// 功能：删除指纹库中最后录入的一个指纹（最常用的删除逻辑）
void finger_delete_last(void)
{
    uint8_t ret;
    // 获取当前有效指纹数量
    ret = as608_get_valid_num(&g_valid_finger_num);
    if (ret != ENSURE_OK)
    {
        ESP_LOGE(TAG, "获取有效指纹数量失败：%s", as608_get_err_msg(ret));
        return;
    }
    if (g_valid_finger_num == 0)
    {
        ESP_LOGW(TAG, "指纹库为空，无需删除");
        return;
    }

    // 删除最后一个指纹：起始编号=g_valid_finger_num-1，删除数量=1
    uint16_t del_page = g_valid_finger_num - 1;
    ret = as608_delete_char(del_page, 1);
    if (ret == ENSURE_OK)
    {
        ESP_LOGI(TAG, "删除指纹成功，编号：%d", del_page);
        // 更新有效指纹数量
        as608_get_valid_num(&g_valid_finger_num);
    }
    else
    {
        ESP_LOGE(TAG, "删除指纹失败：%s", as608_get_err_msg(ret));
    }
}

/************************* 刷指纹验证（主循环调用） *************************/
// 功能：刷指纹验证，匹配成功则调用舵机开锁
void finger_verify(void)
{
    uint8_t ret;
    As608_Search_Result search_res = {0}; // 搜索结果

    // 1. 检测是否有指纹
    ret = as608_get_image();
    if (ret == ENSURE_NO_FINGER)
    {
        // 无手指，直接返回，不打印日志（避免刷屏）
        return;
    }
    else if (ret != ENSURE_OK)
    {
        ESP_LOGE(TAG, "采集指纹图像失败：%s", as608_get_err_msg(ret));
        return;
    }

    // 2. 生成指纹特征（存入Buffer1）
    ret = as608_gen_char(1);
    if (ret != ENSURE_OK)
    {
        ESP_LOGE(TAG, "生成指纹特征失败：%s", as608_get_err_msg(ret));
        return;
    }

    // 3. 搜索指纹库（从0开始，搜索全部有效指纹）
    ret = as608_get_valid_num(&g_valid_finger_num);
    if (ret != ENSURE_OK || g_valid_finger_num == 0)
    {
        ESP_LOGW(TAG, "指纹库为空，无法验证");
        return;
    }
    ret = as608_search(1, 0, g_valid_finger_num, &search_res);
    if (ret == ENSURE_OK)
    {
        // 匹配成功，判断分数阈值（≥100为有效）
        if (search_res.match_soc >= 100)
        {
            ESP_LOGI(TAG, "指纹验证成功！编号：%d，匹配分数：%d", search_res.page_id, search_res.match_soc);
            servo_unlock(); // 舵机开锁
        }
        else
        {
            ESP_LOGE(TAG, "指纹匹配分数过低：%d（阈值≥100）", search_res.match_soc);
        }
    }
    else
    {
        ESP_LOGE(TAG, "指纹验证失败：%s", as608_get_err_msg(ret));
    }
}