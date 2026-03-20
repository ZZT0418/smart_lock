/**
 * @file ble_unlock.c
 * @brief BLE 蓝牙远程开锁功能实现文件
 * 
 * 该文件实现了基于 ESP32 的 BLE GATT 服务，
 * 允许手机通过蓝牙写入数据来触发开锁操作
 */

#include <stdint.h>                      // 包含标准整数类型定义
#include "ble_unlock.h"                  // 包含本模块的头文件
#include "freertos/FreeRTOS.h"           // 包含 FreeRTOS 实时操作系统头文件
#include "freertos/task.h"               // 包含 FreeRTOS 任务管理相关函数
#include "freertos/queue.h"              // 包含 FreeRTOS 队列管理相关函数
#include "esp_log.h"                     // 包含 ESP32 日志输出功能
#include "esp_bt.h"                      // 包含 ESP32 蓝牙控制器相关函数
#include "esp_bt_main.h"                 // 包含 ESP32 蓝牙主控制器初始化函数
#include "esp_bt_defs.h"                 // 包含 ESP32 蓝牙定义和数据结构
#include "esp_bt_device.h"               // 包含 ESP32 蓝牙设备相关函数
#include "esp_gap_ble_api.h"             // 包含 ESP32 BLE GAP(通用访问配置文件)API
#include "esp_gatts_api.h"               // 包含 ESP32 BLE GATT 服务器 API
#include "esp_gatt_common_api.h"         // 包含 ESP32 BLE GATT 通用 API
#include "servo.h"                       // 包含舵机控制模块头文件
#include "onenet_dm.h"

#define TAG             "ble_unlock"     // 定义日志标签，用于标识日志来源
#define DEVICE_NAME     "smart_lock"     // 定义蓝牙设备名称，手机搜索时显示

extern Unlockways_t Unlock_ways;

// 定义自定义服务的 UUID (16 位)
static const uint16_t GATTS_SERVICE_UUID_TEST = 0x1111;   // 自定义服务 UUID

// 定义特征的 UUID (16 位)
static const uint16_t GATTS_CHAR_UUID_CH1 = 0x2222;       // 特征值 UUID，用于手机读写数据

// 定义特征权限（Permission）：可读可写
#define GATTS_CHAR_PERM (ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE)  // 特征权限配置，允许读取和写入

// 定义特征属性（Property）：声明特征"支持哪些功能"，可读、可写、无响应写
#define GATTS_CHAR_PROPERTY (ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR)

// 保存 GATT 接口号，用于后续 GATT 操作
static esp_gatt_if_t gatts_if = ESP_GATT_IF_NONE;  // 初始化为无效值，注册成功后会更新

// 当前连接 ID，仅支持单连接模式
static uint16_t conn_id = 0;  // 保存当前蓝牙连接的 ID，断开连接后清零

// 定义开锁命令队列句柄
QueueHandle_t unlock_queue = NULL;  // 用于在蓝牙任务和开锁任务之间传递开锁命令

// 定义广播参数结构体
static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,                          // 广播最小间隔，单位 0.625ms，0x20=20ms
    .adv_int_max        = 0x40,                          // 广播最大间隔，单位 0.625ms，0x40=40ms
    .adv_type           = ADV_TYPE_IND,                  // 广播类型为可连接、可扫描的通用广播
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,          // 使用公共蓝牙地址（设备出厂时烧录的地址）
    .peer_addr          = {0},                           // 对端设备地址，设为 0 表示对所有设备可见
    .peer_addr_type     = BLE_ADDR_TYPE_PUBLIC,          // 对端地址类型为公共地址
    .channel_map        = ADV_CHNL_ALL,                  // 在所有广播信道（37/38/39）上发送广播
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,  // 允许所有设备进行扫描和连接请求
};

/**
 * @brief 处理写事件的辅助函数
 * 
 * 当手机向特征写入数据时，如果需要响应（need_rsp），则调用此函数发送响应
 * 
 * @param gatts_if GATT 接口号
 * @param param GATT 事件参数
 */
static void example_write_event_env(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    esp_gatt_status_t status = ESP_GATT_OK;  // 定义状态变量，ESP_GATT_OK 表示操作成功
    
    // 检查是否需要发送响应
    if (param->write.need_rsp) {
        // 发送 GATT 响应给手机
        // 参数依次为：GATT 接口、连接 ID、传输 ID、状态、响应数据（NULL 表示空响应）
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, NULL);
    }
}

/**
 * @brief GATT 事件处理函数（回调函数）
 * 
 * 该函数由 BLE 协议栈调用，处理各种 GATT 相关事件
 * 
 * @param event GATT 事件类型
 * @param gatts_if_param GATT 接口号
 * @param param GATT 事件参数
 */
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if_param, esp_ble_gatts_cb_param_t *param) 
{
    // 根据事件类型进行不同的处理
    switch (event) {
        
        //  case 1: GATT 应用程序注册完成事件
        case ESP_GATTS_REG_EVT:
            // 打印注册事件信息，包括 app_id 和状态
            ESP_LOGI(TAG, "GATTS_REG_EVT, app_id %d, status %d", param->reg.app_id, param->reg.status);
            
            // 检查注册是否成功
            if (param->reg.status == ESP_GATT_OK) {
                gatts_if = gatts_if_param;  // 保存 GATT 接口号，后续操作需要使用
                
                // 定义服务 ID 结构体
                esp_gatt_srvc_id_t service_id = {
                    .id = {
                        .uuid = {
                            .len = ESP_UUID_LEN_16,              // UUID 长度为 16 位
                            .uuid = {.uuid16 = GATTS_SERVICE_UUID_TEST}  // 设置服务 UUID
                        },
                        .inst_id = 0  // 实例 ID为 0，表示主服务
                    },
                    .is_primary = true  // 标记为主服务（相对于从服务）
                };
                
                // 创建 GATT 服务
                // 参数：GATT 接口、服务 ID、属性数量（10 个）
                esp_ble_gatts_create_service(gatts_if, &service_id, 10);
            }
            break;

        // case 2: 服务创建完成事件
        case ESP_GATTS_CREATE_EVT:
            // 打印服务创建事件信息
            ESP_LOGI(TAG, "CREATE_SERVICE_EVT, service_handle %d", param->create.service_handle);
            
            // 检查服务创建是否成功
            if (param->create.status == ESP_GATT_OK) {
                // 启动 GATT 服务
                // 参数：服务句柄
                esp_ble_gatts_start_service(param->create.service_handle);
                
                // 添加特征到服务中
                esp_err_t ret = esp_ble_gatts_add_char(
                    param->create.service_handle,                              // 服务句柄
                    &(esp_bt_uuid_t){.len = ESP_UUID_LEN_16, .uuid.uuid16 = GATTS_CHAR_UUID_CH1},  // 特征UUID
                    GATTS_CHAR_PERM,                                           // 特征权限
                    GATTS_CHAR_PROPERTY,                                       // 特征属性
                    NULL,                                                      // 特征值（NULL 表示使用默认值）
                    NULL                                                       // 控制属性（NULL 表示不使用）
                );
                
                // 检查添加特征是否成功
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "add char failed, error code = %x", ret);  // 打印错误信息
                }
            }
            break;

        // case 3: 特征添加完成事件
        case ESP_GATTS_ADD_CHAR_EVT:
            // 打印特征添加事件信息
            ESP_LOGI(TAG, "ADD_CHAR_EVT, char_handle %d", param->add_char.attr_handle);
            
            // 添加特征描述符（CCCD - Client Characteristic Configuration Descriptor）
            // 用于配置通知和指示功能
            esp_ble_gatts_add_char_descr(
                param->add_char.service_handle,                              // 服务句柄
                &(esp_bt_uuid_t){.len = ESP_UUID_LEN_16, .uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG},  // CCCD UUID (0x2902)
                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,                    // 描述符权限：可读可写
                NULL,                                                        // 描述符值（NULL 表示使用默认值）
                NULL                                                         // 控制属性
            );
            break;

        // case 4: 特征描述符添加完成事件
        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            // 打印描述符添加事件信息
            ESP_LOGI(TAG, "ADD_CHAR_DESCR_EVT, descr_handle %d", param->add_char_descr.attr_handle);
            break;

        // case 5: 服务启动完成事件
        case ESP_GATTS_START_EVT:
            // 打印服务启动事件信息
            ESP_LOGI(TAG, "SERVICE_START_EVT, status %d", param->start.status);
            break;

        // case 6: 蓝牙连接建立事件
        case ESP_GATTS_CONNECT_EVT:
            // 打印连接事件信息，包括连接 ID 和远程设备 MAC 地址
            ESP_LOGI(TAG, "CONNECT_EVT, conn_id %d, remote "ESP_BD_ADDR_STR"", 
                     param->connect.conn_id, ESP_BD_ADDR_HEX(param->connect.remote_bda));
            
            conn_id = param->connect.conn_id;  // 保存连接 ID，用于后续通信
            
            // 定义连接参数更新结构体
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));  // 复制远程设备地址
            conn_params.latency = 0;       // 连接延迟为 0
            conn_params.max_int = 0x20;    // 最大连接间隔 32*1.25ms = 40ms
            conn_params.min_int = 0x10;    // 最小连接间隔 16*1.25ms = 20ms
            conn_params.timeout = 400;     // 超时时间 400*10ms = 4s
            
            // 请求更新连接参数
            esp_ble_gap_update_conn_params(&conn_params);
            
            // 停止广播（已连接不需要继续广播）
            esp_ble_gap_stop_advertising();
            break;

        // case 7: 蓝牙断开连接事件
        case ESP_GATTS_DISCONNECT_EVT:
            // 打印断开连接事件信息
            ESP_LOGI(TAG, "DISCONNECT_EVT, conn_id %d, remote "ESP_BD_ADDR_STR", reason 0x%02x",
                     param->disconnect.conn_id, ESP_BD_ADDR_HEX(param->disconnect.remote_bda), param->disconnect.reason);
            
            conn_id = 0;  // 清空连接 ID
            
            // 重新启动广播，让其他设备可以连接
            esp_ble_gap_start_advertising(&adv_params);
            break;

        // case 8: 特征读取事件（手机读取特征值）
        case ESP_GATTS_READ_EVT:
            // 打印读取事件信息
            ESP_LOGI(TAG, "READ_EVT");
            
            // 定义 GATT 响应结构体
            esp_gatt_rsp_t rsp = {0};
            rsp.attr_value.handle = param->read.handle;  // 设置响应的特征句柄
            rsp.attr_value.len = 0;                      // 响应数据长度为 0（空响应）
            
            // 发送读响应给手机
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
            break;

        // case 9: 特征写入事件（手机写入数据）
        case ESP_GATTS_WRITE_EVT:
            // 打印写入事件信息，包括数据长度
            ESP_LOGI(TAG, "WRITE_EVT, len %d", param->write.len);
            
            // 检查写入的数据长度是否大于 0，且队列已创建
            if (param->write.len > 0 && unlock_queue != NULL) {
                int dummy = 1;  // 定义一个整数值作为开锁命令（实际应用中可以传递更复杂的数据）
                
                // 发送开锁命令到队列
                if (xQueueSend(unlock_queue, &dummy, 0) == pdPASS) {
                    ESP_LOGI(TAG, "开锁命令已发送到队列");  // 发送成功打印日志
                } else {
                    ESP_LOGE(TAG, "发送队列失败");  // 发送失败打印错误日志
                }
            }
            
            // 处理写响应（如果需要响应的话）
            example_write_event_env(gatts_if, param);
            break;

        // case 10: 确认事件（收到手机的确认）
        case ESP_GATTS_CONF_EVT:
            // 打印确认事件信息
            ESP_LOGI(TAG, "CONF_EVT, status %d", param->conf.status);
            break;

        // default: 处理其他未定义的事件
        default:
            break;  // 不做任何处理
    }
}

/**
 * @brief GAP 事件处理函数（回调函数）
 * 
 * 该函数由 BLE 协议栈调用，处理各种 GAP 相关事件
 * 
 * @param event GAP 事件类型
 * @param param GAP 事件参数
 */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    // 根据事件类型进行不同的处理
    switch (event) {
        
        // case 1: 广播数据配置完成事件
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            // 打印广播数据配置完成事件信息
            ESP_LOGI(TAG, "ADV_DATA_SET_COMPLETE, status %d", param->adv_data_cmpl.status);
            
            // 检查配置是否成功
            if (param->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                // 启动广播
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;

        // case 2: 广播启动完成事件
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            // 检查广播启动是否成功
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Advertising start successfully");  // 成功则打印成功信息
            } else {
                ESP_LOGE(TAG, "Advertising start failed, status %d", param->adv_start_cmpl.status);  // 失败则打印错误信息
            }
            break;

        // case 3: 广播停止完成事件
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            // 打印广播停止事件信息
            ESP_LOGI(TAG, "Advertising stopped");
            break;

        // case 4: 连接参数更新完成事件
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            // 打印连接参数更新事件信息
            ESP_LOGI(TAG, "Connection params update, status %d, conn_int %d, latency %d, timeout %d",
                     param->update_conn_params.status,           // 更新状态
                     param->update_conn_params.conn_int,         // 连接间隔
                     param->update_conn_params.latency,          // 延迟
                     param->update_conn_params.timeout);         // 超时时间
            break;

        // default: 处理其他未定义的事件
        default:
            break;  // 不做任何处理
    }
}

/**
 * @brief 开锁任务函数
 * 
 * 该任务等待队列中的开锁命令，收到命令后执行开锁操作
 * 
 * @param pvParameters 任务参数（未使用）
 */
static void unlock_task(void *pvParameters) 
{
    int dummy;  // 定义一个整型变量用于接收队列数据
    
    // 无限循环，持续等待开锁命令
    while (1) {
        // 从队列接收开锁命令（无限阻塞等待）
        // 参数：队列句柄、接收缓冲区、等待时间（portMAX_DELAY 表示无限等待）
        if (xQueueReceive(unlock_queue, &dummy, portMAX_DELAY) == pdTRUE) {
            // 收到开锁命令
            Unlock_ways=UNLOCK_WAYS_BT;
            ESP_LOGI(TAG, "收到开锁命令，执行开锁");
            
            // 调用舵机开锁函数
            // 注意：servo_unlock() 函数内部应该包含舵机转动和复位逻辑
            servo_unlock();
        }
    }
}

/**
 * @brief 初始化 BLE 远程开锁功能
 * 
 * 该函数负责初始化蓝牙控制器、协议栈、GATT 服务、广播等
 * 
 * @return ESP_OK 初始化成功，其他错误码表示初始化失败
 */
esp_err_t ble_unlock_init(void)
{
    esp_err_t ret;  // 定义返回值变量
    
    // ========== 步骤 1: 初始化蓝牙控制器 ==========
    
    // 释放经典蓝牙内存，只保留 BLE 蓝牙内存
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    
    // 定义蓝牙控制器配置结构体（使用默认配置）
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    // 初始化蓝牙控制器
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "蓝牙控制器初始化失败：%s", esp_err_to_name(ret));  // 打印错误信息
        return ret;  // 返回错误码
    }

    // 启用 BLE 蓝牙控制器
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启用 BLE 控制器失败：%s", esp_err_to_name(ret));
        return ret;
    }

    // ========== 步骤 2: 初始化 Bluedroid 协议栈 ==========
    
    // 初始化 Bluedroid 协议栈（ESP32 使用的蓝牙协议栈）
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化 Bluedroid 失败：%s", esp_err_to_name(ret));
        return ret;
    }

    // 启用 Bluedroid 协议栈
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启用 Bluedroid 失败：%s", esp_err_to_name(ret));
        return ret;
    }

    // ========== 步骤 3: 注册 GATT 和 GAP 回调函数 ==========
    
    // 注册 GATT 服务器回调函数
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册 GATT 回调失败：%s", esp_err_to_name(ret));
        return ret;
    }
    
    // 注册 GAP 回调函数
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册 GAP 回调失败：%s", esp_err_to_name(ret));
        return ret;
    }

    // ========== 步骤 4: 创建 GATT 应用程序 ==========
    
    // 注册 GATT 应用程序（app_id 可以任意，这里用 0）
    ret = esp_ble_gatts_app_register(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATT 应用注册失败：%s", esp_err_to_name(ret));
        return ret;
    }

    // ========== 步骤 5: 设置设备名称 ==========
    
    // 设置蓝牙设备名称（手机搜索时显示的名称）
    ret = esp_ble_gap_set_device_name(DEVICE_NAME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置设备名失败：%s", esp_err_to_name(ret));
        return ret;
    }

    // ========== 步骤 6: 配置广播数据 ==========
    
    // 定义广播数据结构体
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,              // 不是扫描响应数据，是广播数据本身
        .include_name = true,               // 广播中包含设备名称
        .include_txpower = false,           // 不包含发射功率信息
        .min_interval = 0x0006,             // 最小连接间隔建议值
        .max_interval = 0x0010,             // 最大连接间隔建议值
        .appearance = 0x00,                 // 外观值为 0，表示未知设备类型
        .manufacturer_len = 0,              // 厂商数据长度为 0
        .p_manufacturer_data = NULL,        // 厂商数据指针为 NULL
        .service_data_len = 0,              // 服务数据长度为 0
        .p_service_data = NULL,             // 服务数据指针为 NULL
        .service_uuid_len = 0,              // 服务 UUID 长度为 0（不在广播中暴露服务 UUID）
        .p_service_uuid = NULL,             // 服务 UUID 指针为 NULL
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),  // 广播标志：可发现、不支持经典蓝牙
    };
    
    // 配置广播数据
    ret = esp_ble_gap_config_adv_data(&adv_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置广播数据失败：%s", esp_err_to_name(ret));
        return ret;
    }

    // ========== 步骤 7: 创建开锁队列和任务 ==========
    
    // 创建 FreeRTOS 队列，用于传递开锁命令
    // 参数：队列深度 5（最多缓存 5 个命令）、每个元素大小为 sizeof(int)
    unlock_queue = xQueueCreate(5, sizeof(int));
    if (unlock_queue == NULL) {
        ESP_LOGE(TAG, "创建队列失败");
        return ESP_ERR_NO_MEM;  // 返回内存不足错误
    }

    // 创建开锁任务
    // 参数：任务函数、任务名称、堆栈大小、任务参数、优先级、任务句柄（NULL 表示不需要）
    BaseType_t task_created = xTaskCreate(unlock_task, "ble_unlock", 4096, NULL, 5, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "创建任务失败");
        return ESP_ERR_NO_MEM;  // 返回内存不足错误
    }

    // ========== 初始化完成 ==========
    
    // 打印成功信息
    ESP_LOGI(TAG, "蓝牙远程开锁初始化成功，设备名：%s", DEVICE_NAME);
    
    return ESP_OK;  // 返回成功
}