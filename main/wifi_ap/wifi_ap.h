#ifndef __WIFI_AP_H__
#define __WIFI_AP_H__

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_http_server.h"        // 引入 HTTP Server 头文件

void My_WIFI_Init_AP(void);
httpd_handle_t start_http_server(void);


#endif
