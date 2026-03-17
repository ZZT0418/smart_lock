#ifndef _BLE_UNLOCK_H_
#define _BLE_UNLOCK_H_

#include "esp_err.h"

/**
 * @brief 初始化BLE远程开锁功能
 * 
 * 该函数负责：
 * 1. 初始化蓝牙控制器和Bluedroid栈
 * 2. 注册GATT和GAP回调
 * 3. 创建GATT服务表
 * 4. 启动广播
 * 5. 创建开锁任务等待队列
 * 
 * @return ESP_OK 成功，其他失败
 */
esp_err_t ble_unlock_init(void);

#endif