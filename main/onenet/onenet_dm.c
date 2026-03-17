#include "onenet_dm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "string.h"
#include "esp_log.h"


#define TAG "onenet_dm"



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
            //获取属性名称
            //略
           
        }
    }

}      




/*==========================属性上传函数============================
 * @brief:物模型属性上传
 * @param:无
 * @return:上传的属性数据
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
    //略
    
    
    return root;
}