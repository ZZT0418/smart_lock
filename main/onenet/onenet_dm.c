#include "onenet_dm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "string.h"
#include "esp_log.h"
#include "servo.h"

extern int64_t Current_unlock_time;
#define TAG "onenet_dm"

LockState_t Lock_state = LOCK_STATE_CLOSE;       //默认状态为关闭
Unlockways_t Unlock_ways = UNLOCK_WAYS_WAITING;
LockAlarm_t Lock_alarm = LOCK_ALARM_OFF;         //默认未报警

/*==========================物模型初始化函数========================
 * @brief:物模型初始化
 * @param:无
 * @return:无
 *================================================================*/
// void onenet_dm_init(void)
// {
//     ESP_LOGI(TAG,"物模型初始化");
// }   

/*==========================属性处理函数============================
 * @brief:处理onenet下行的物模型数据
 * @param:platform_data:平台下发的json数据
 * @return:无
 *================================================================*/
void onenet_property_handle(cJSON *platform_dm_data)
{
    //从JSON数据中读取“params”
    cJSON *params_json = cJSON_GetObjectItem(platform_dm_data,"params");
    if(params_json != NULL)
    {
        //遍历 "params" 下的所有子节点
        cJSON *name_js=params_json->child;
        //遍历链表
        while(name_js != NULL)
        {
            if(strcmp(name_js->string,"LockState")==0)           //门锁状态属性
            {
                ESP_LOGI(TAG,"属性名称:%s",name_js->string);
                LockState_t Lock_state = cJSON_GetObjectItem(name_js,"value")->valueint;    //获取属性值
                switch (Lock_state)
                {
                    case LOCK_STATE_POEN:                           //门锁打开
                            ESP_LOGI(TAG,"门锁处于打开状态");
                            break;
                    case LOCK_STATE_CLOSE:                           //门锁关闭
                            ESP_LOGI(TAG,"门锁处于关闭状态");
                            break;
                    default:
                            ESP_LOGI(TAG,"门锁状态未知");
                            break;
                }
            }
            name_js = name_js->next;                                    
           
        }
    }

}      




/*==========================属性上报函数============================
 * @brief:物模型属性上报
 * @param:无
 * @return:上报的属性数据
 *================================================================*/
cJSON *onenet_property_upload(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;
    
    cJSON_AddStringToObject(root, "id", "123456");
    cJSON_AddStringToObject(root, "version", "1.0");
    
    cJSON *params = cJSON_AddObjectToObject(root, "params");
    if (params == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    
    //添加属性数据

    //门锁状态属性
    cJSON *LockState_object= cJSON_AddObjectToObject(params, "LockState");
    if (LockState_object==NULL)     //判空，为空，则删除创建的节点，释放内存，避免内存泄漏
    {
        cJSON_Delete(root);         
        return NULL;
    }
    
    cJSON_AddNumberToObject(LockState_object, "value", Lock_state);

    //开锁方式属性
    cJSON *Unlockways_object= cJSON_AddObjectToObject(params, "Unlockways");
    if (Unlockways_object==NULL)     //判空，为空，则删除创建的节点，释放内存，避免内存泄漏
    {
        cJSON_Delete(root);         
        return NULL;
    }
    cJSON_AddNumberToObject(Unlockways_object, "value", Unlock_ways);

    //门锁报警属性
    cJSON *LockAlarm_object= cJSON_AddObjectToObject(params, "LockAlarm");
    if (LockAlarm_object==NULL)     //判空，为空，则删除创建的节点，释放内存，避免内存泄漏
    {
        cJSON_Delete(root);         
        return NULL;
    }
    cJSON_AddNumberToObject(LockAlarm_object, "value", Lock_alarm);

    return root;
}

/*==========================事件上报函数============================
 * @brief:物模型设备事件上报
 * @param:无
 * @return:上报的事件数据
 =================================================================*/
cJSON *onenet_event_upload(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;
    
    cJSON_AddStringToObject(root, "id", "123456");
    cJSON_AddStringToObject(root, "version", "1.0");
    
    cJSON *params = cJSON_AddObjectToObject(root, "params");
    if (params == NULL) 
    {
        cJSON_Delete(root);
        return NULL;
    }

    //添加事件
    //开门日志事件
    cJSON *OpenLog_object= cJSON_AddObjectToObject(params, "OpenLog");
    if (OpenLog_object==NULL)     //判空，为空，则删除创建的节点，释放内存，避免内存泄漏
    {
        cJSON_Delete(root);         
        return NULL;
    }
    cJSON *OpenLog_value = cJSON_AddObjectToObject(OpenLog_object, "value");
    if (OpenLog_value==NULL)     //判空，为空，则删除创建的节点，释放内存，避免内存泄漏
    {
        cJSON_Delete(root);         
        return NULL;
    }
    cJSON_AddNumberToObject(OpenLog_value, "Unlockways",Unlock_ways);
    cJSON_AddNumberToObject(OpenLog_value, "Time",Current_unlock_time);


    return root;
}