#include "onenet_mqtt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "onenet_token.h"
#include "onenet_dm.h"              //引入物模型相关头文件

#define TAG     "onenet_mqtt"

// 调整任务堆栈大小
#define UPLOAD_TASK_STACK_SIZE    4096
// 调整任务优先级
#define UPLOAD_TASK_PRIORITY      5

//连接成功标志位
static bool onenet_connected_flg = false;

// mqtt 句柄
static esp_mqtt_client_handle_t mqtt_client_handle = NULL;

//重连延迟时间（毫秒）
static uint32_t reconnect_delay_ms = 1000;

// 数据上传任务句柄
static TaskHandle_t s_upload_task_handle = NULL;

// 静态任务堆栈和 TCB 缓冲区（用于静态创建任务）
static StackType_t s_upload_task_stack[UPLOAD_TASK_STACK_SIZE];
static StaticTask_t s_upload_task_tcb;

static void onenet_data_upload_task(void *pvParameters);

/*==========================MQTT事件处理函数===========================
 *功能：处理onenet平台mqtt服务事件
 *参数：无
 *返回值：无
  ====================================================================*/
static void onenet_mqtt_event_handler(void* event_handler_arg,
                                        esp_event_base_t event_base,
                                        int32_t event_id,
                                        void* event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:  //连接成功
            ESP_LOGI(TAG, "Onenet mqtt connected");
            onenet_connected_flg = true;                        //将标志位设置为true
            onenet_mqtt_subscribe();                            //订阅相关主题
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // 修复1：增加NULL指针检查，避免cJSON操作崩溃
            cJSON *property_data = onenet_property_upload();
            if (property_data != NULL) {
                char *json_str = cJSON_PrintUnformatted(property_data);
                if (json_str != NULL && mqtt_client_handle != NULL) {
                    esp_mqtt_client_publish(mqtt_client_handle, "$sys/idB0K035tx/smart_lock/thing/property/post", json_str, 0, 1, 0);
                    free(json_str);
                }
                cJSON_Delete(property_data);
            }
            
            cJSON *event_data = onenet_event_upload();
            if (event_data != NULL) {
                char *json_str = cJSON_PrintUnformatted(event_data);
                if (json_str != NULL && mqtt_client_handle != NULL) {
                    esp_mqtt_client_publish(mqtt_client_handle, "$sys/idB0K035tx/smart_lock/thing/event/post", json_str, 0, 1, 0);
                    free(json_str);
                }
                cJSON_Delete(event_data);
            }
            
            // 使用静态方式创建数据上传任务，避免动态内存分配失败
            if (s_upload_task_handle == NULL) {
                s_upload_task_handle = xTaskCreateStatic(
                    onenet_data_upload_task,          // 任务函数指针
                    "onenet_upload",                  // 任务名称
                    UPLOAD_TASK_STACK_SIZE,           // 堆栈大小
                    NULL,                             // 任务参数
                    UPLOAD_TASK_PRIORITY,             // 任务优先级
                    s_upload_task_stack,              // 静态堆栈数组
                    &s_upload_task_tcb                // 静态 TCB 缓冲区
                );
                
                if (s_upload_task_handle != NULL) {
                    ESP_LOGI(TAG, "数据上传任务静态创建成功，任务句柄：%p，堆栈大小：%d", 
                             s_upload_task_handle, UPLOAD_TASK_STACK_SIZE);
                } else {
                    ESP_LOGE(TAG, "数据上传任务静态创建失败！");
                    ESP_LOGI(TAG, "系统剩余堆内存：%d bytes", esp_get_free_heap_size());
                }
            }
            break;
        case MQTT_EVENT_DISCONNECTED:   //连接断开
            ESP_LOGI(TAG, "Onenet mqtt disconnected");
            onenet_connected_flg = false;                   //将标志位设置为false
            // 删除数据上传任务
            if (s_upload_task_handle != NULL) {
                vTaskDelete(s_upload_task_handle);
                s_upload_task_handle = NULL;
                ESP_LOGI(TAG, "数据上传任务已删除");
            }

            //指数退避重连机制
            vTaskDelay(pdMS_TO_TICKS(reconnect_delay_ms));
            reconnect_delay_ms *= 2;
            if (reconnect_delay_ms > 60000) reconnect_delay_ms = 60000; //最大延迟60秒
            
            if (mqtt_client_handle != NULL) {
                esp_mqtt_client_reconnect(mqtt_client_handle);
            }
            break;

        case MQTT_EVENT_SUBSCRIBED:     //收到订阅消息ACK
            ESP_LOGI(TAG, "Onenet mqtt subscribed ack, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:   //收到解订阅消息ACK
            ESP_LOGI(TAG, "Onenet mqtt unsubscribed ack, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:      //收到发布消息ACK
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            // 判断是否是物模型下行命令（主题包含"/thing/property/set"）
            if (strstr(event->topic, "/thing/property/set") != NULL) {
                ESP_LOGI("ONENET", "收到平台命令：%.*s", event->data_len, event->data);

                // 创建临时缓冲区并复制数据，确保字符串结尾
                char *json_data = malloc(event->data_len + 1);
                if (json_data == NULL) {
                    ESP_LOGE("ONENET", "内存分配失败");
                    break;
                }
                
                memcpy(json_data, event->data, event->data_len);
                json_data[event->data_len] = '\0';  // 确保字符串结尾
                
                ESP_LOGI("ONENET", "解析数据: %s", json_data);

                // 1. 解析平台发来的JSON字符串
                cJSON *platform_data = cJSON_Parse(json_data);
                free(json_data);  // 释放临时缓冲区
                
                if (platform_data == NULL) {
                    ESP_LOGE("ONENET", "JSON解析失败");
                    break;
                }

                // 2. 处理命令（控制硬件，如打开LED）
                onenet_property_handle(platform_data);

                // 3. 回复ACK（告诉平台"命令已执行"）
                char ack_topic[128];
                snprintf(ack_topic, sizeof(ack_topic),
                         "$sys/%s/%s/thing/property/set_reply",
                         ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
                
                cJSON *ack_json = cJSON_CreateObject();
                // 修复3：增加id字段的NULL检查，避免崩溃
                cJSON *id_item = cJSON_GetObjectItem(platform_data, "id");
                if (id_item != NULL && id_item->type == cJSON_String) {
                    cJSON_AddStringToObject(ack_json, "id", id_item->valuestring);
                } else {
                    cJSON_AddStringToObject(ack_json, "id", "unknown");
                }
                cJSON_AddNumberToObject(ack_json, "code", 200); // 200=成功
                cJSON_AddStringToObject(ack_json, "msg", "Success");

                char *ack_str = cJSON_Print(ack_json);
                if (ack_str != NULL && mqtt_client_handle != NULL) {
                    esp_mqtt_client_publish(mqtt_client_handle, ack_topic, ack_str, 0, 0, 0);
                    free(ack_str); // 修复4：及时释放内存，避免内存泄漏
                }

                // 4. 释放内存
                cJSON_Delete(platform_data);
                cJSON_Delete(ack_json);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
}

/*===========================MQTT启动函数===========================
 *功能：用于启动onenet平台mqtt服务
 *参数：无
 *返回值：esp_err_t
 ===================================================================*/
esp_err_t onenet_mqtt_start(void)
{
    //初始化并配置mqtt
    esp_mqtt_client_config_t mqtt_client = {0};
    // 设置MQTT服务器信息
    mqtt_client.broker.address.uri = "mqtt://mqtts.heclouds.com";    //onenet平台连接的mqtt地址
    mqtt_client.broker.address.port = 1883;                          //onenet平台连接的mqtt端口号
    mqtt_client.credentials.client_id=ONENET_DEVICE_NAME;            //设备名称
    mqtt_client.credentials.username=ONENET_PRODUCT_ID;              //产品ID

    static char token[256];
    int result = dev_token_generate( token,                         //生成token
                                    SIG_METHOD_SHA256,              //签名方法
                                    1924833600,                     //token的过期时间       
                                    ONENET_PRODUCT_ID,              //产品ID    
                                    ONENET_DEVICE_NAME,             //设备名称
                                    ONETNET_ACCESS_KEY);            //访问密钥
    
    if (result != 0) {
        ESP_LOGE(TAG, "Failed to generate token, error code: %d", result);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Generated token: %s", token);
    ESP_LOGI(TAG, "Product ID: %s", ONENET_PRODUCT_ID);
    ESP_LOGI(TAG, "Device Name: %s", ONENET_DEVICE_NAME);
    //password:onenet平台使用token来作为密码，token直接使用dev_token_generate计算
    mqtt_client.credentials.authentication.password= token;            
    //启用心跳机制
    mqtt_client.session.keepalive = 30;  //心跳间隔30秒

    //应用mqtt配置，返回mqtt句柄
    mqtt_client_handle = esp_mqtt_client_init(&mqtt_client);
    if (mqtt_client_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    //注册mqtt事件
    esp_mqtt_client_register_event(mqtt_client_handle, ESP_EVENT_ANY_ID, onenet_mqtt_event_handler, NULL);
    
    //启动mqtt
    esp_err_t ret = esp_mqtt_client_start(mqtt_client_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;

}

/*===========================数据上传任务=========================== 
*功能：周期性向平台上报数据或事件
*参数：无 
*返回值：无 
===================================================================*/
static void onenet_data_upload_task(void *pvParameters)
{
    // 修复6：使用宏定义/变量拼接主题，避免硬编码
    char property_topic[128];
    char event_topic[128];
    snprintf(property_topic, sizeof(property_topic), "$sys/%s/%s/thing/property/post", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    snprintf(event_topic, sizeof(event_topic), "$sys/%s/%s/thing/event/post", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    
    while (1) {
        if (!onenet_connected_flg || mqtt_client_handle == NULL) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        cJSON *property_data = onenet_property_upload();
        if (property_data != NULL) {
            char *json_str = cJSON_PrintUnformatted(property_data);
            if (json_str != NULL) {
                esp_mqtt_client_publish(mqtt_client_handle, property_topic, json_str, 0, 1, 0);
                free(json_str);
            }
            cJSON_Delete(property_data);
        }

        cJSON *event_data = onenet_event_upload();
        if (event_data != NULL) {
            char *json_str = cJSON_PrintUnformatted(event_data);
            if (json_str != NULL) {
                esp_mqtt_client_publish(mqtt_client_handle, event_topic, json_str, 0, 1, 0);
                free(json_str);
            }
            cJSON_Delete(event_data);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/*===========================MQTT订阅函数=========================== 
*功能：用于onenet平台订阅相关主题，有要订阅的主题可以放在这个函数 
*参数：无 
*返回值：esp_err_t 
===================================================================*/
esp_err_t onenet_mqtt_subscribe(void)
{
    //判断 mqtt 是否已经连接
    if (!onenet_connected_flg || mqtt_client_handle == NULL)
        return ESP_FAIL;
    
    char topic[128];
    
    //订阅上报属性响应主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/post/reply",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(mqtt_client_handle,topic,1);
    ESP_LOGI(TAG, "订阅主题：%s", topic);
    
    //订阅下行设置属性主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/set",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(mqtt_client_handle,topic,1);
    ESP_LOGI(TAG, "订阅主题：%s", topic);
    
    //订阅设备事件上报响应主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/event/post/reply",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(mqtt_client_handle,topic,1);
    ESP_LOGI(TAG, "订阅主题：%s", topic);
    
    return ESP_OK;
}