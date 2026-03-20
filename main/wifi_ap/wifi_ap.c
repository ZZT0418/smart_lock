#include "wifi_ap.h"
#include "onenet_mqtt.h"
#include  <string.h>
#include <arpa/inet.h>              // 添加网络地址转换函数的头文件


#define TAG "WIFI_AP"
#define WIFI_AP_SSID "ESP32_AP"
#define WIFI_AP_PASSWORD "12345678"

/*======================================网页配网的全局变量======================================== */
static httpd_handle_t httpd_handle =NULL;                   //HTTP服务句柄(标识服务器)
static esp_err_t get_handler(httpd_req_t *r);               // 获取网页处理函数
static esp_err_t post_handler(httpd_req_t *r);              // POST请求处理函数
static esp_err_t favicon_handler(httpd_req_t *req);         // 处理favicon.ico请求

// 添加WiFi连接状态管理
static char target_ssid[32] = {0};      // 目标WiFi SSID
static char target_password[64] = {0};  // 目标WiFi密码
static bool wifi_connecting = false;    // WiFi连接状态标志

// 添加初始化状态标志
static bool wifi_ap_initialized = false;

/*============================================WiFi连接任务==============================================
 * @brief: 独立任务处理WiFi连接，避免阻塞HTTP响应(之前esp_wifi_connect()直接在post_handler里执行，浏览器会显示 “超时”)
 * @param: pvParameters 任务参数
 * @return: 无
 * ==================================================================================================*/
static void wifi_connect_task(void *pvParameters)
{
    ESP_LOGI(TAG, "开始连接WiFi: %s", target_ssid);
    
    // 配置STA参数
    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, target_ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char*)sta_config.sta.password, target_password, sizeof(sta_config.sta.password) - 1);
    
    // 设置为STA模式并应用配置
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    
    // 启动连接
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi连接启动失败: %s", esp_err_to_name(ret));
        wifi_connecting = false;
        return;
    }
    
    ESP_LOGI(TAG, "WiFi连接请求已发送,等待连接结果...");
    wifi_connecting = false;
    
    vTaskDelete(NULL);  // 任务完成后自我删除
}

/*===============================WiFi事件处理函数====================================== 
 * @brief: WIFI事件处理函数
 * @param: arg，回调函数参数；event_base，事件类别；event_id，事件ID；event_data，事件数据
 * @return: 无
 * ===================================================================================*/
void wifi_event_handler(void* event_handler_arg, esp_event_base_t event_base,
                        int32_t event_id,void* event_data)
{
    //事件1：STA设备接入AP
    if(event_id==WIFI_EVENT_AP_STACONNECTED)
    {
        ESP_LOGI(TAG, "有设备连接了%s",WIFI_AP_SSID);
        
    }
    //事件2：STA设备断开AP
    if(event_id==WIFI_EVENT_AP_STADISCONNECTED)
    {
        ESP_LOGI(TAG, "有设备断开了%s",WIFI_AP_SSID);
        
    }
    //事件3：STA连接成功
    if(event_id == WIFI_EVENT_STA_CONNECTED)
    {
        ESP_LOGI(TAG, "WiFi连接成功!");
    }
    //事件4：STA连接断开
    if(event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*)event_data;
        ESP_LOGW(TAG, "WiFi连接断开,原因码: %d", disconnected->reason);
        
        // 根据断开原因提供更详细的错误信息
        switch(disconnected->reason) {
            case WIFI_REASON_AUTH_EXPIRE:
            case WIFI_REASON_AUTH_FAIL:
                ESP_LOGW(TAG, "认证失败，请检查密码是否正确");
                break;
            case WIFI_REASON_NO_AP_FOUND:
                ESP_LOGW(TAG, "未找到指定的WiFi网络");
                break;
            case WIFI_REASON_ASSOC_LEAVE:
                ESP_LOGW(TAG, "主动断开连接");
                break;
            default:
                ESP_LOGW(TAG, "连接断开，原因码: %d", disconnected->reason);
                break;
        }
        wifi_ap_initialized = false;
        ESP_LOGI(TAG, "已重置 WiFi AP 初始化标志，可重新进入配网模式");
    }
}

/*===============================IP事件处理函数====================================== 
 * @brief: IP事件处理函数
 * @param: arg，回调函数参数；event_base，事件类别；event_id，事件ID；event_data，事件数据
 * @return: 无
 * ===================================================================================*/
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "成功获取 IP 地址:" IPSTR, IP2STR(&event->ip_info.ip));
        
        // 等待网络稳定（根据项目规范）
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        //启动 OneNet
         // 初始化 OneNet 云平台
        ESP_LOGI(TAG, "正在初始化 OneNet 云平台");
        esp_err_t ret = onenet_mqtt_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OneNet 云平台初始化失败");
        } else {
            ESP_LOGI(TAG, "OneNet 云平台初始化成功");
        }
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "失去 IP 地址连接");
    }
}

/*===============================HTTP服务启动函数====================================== 
 * @brief: 用于HTTP服务配置与启动,注册配网相关路由
 * @param: 无
 * @return: httpd_handle_t 服务器句柄（成功）/NULL（失败）
 * ===================================================================================*/
httpd_handle_t start_http_server(void)
{
    //配置HTTP服务器，使用默认配置
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();

    //启动HTTP服务器
    esp_err_t ret =httpd_start(&httpd_handle, &http_config);
    if(ret!=ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP服务器启动失败");
        return NULL;
    }
    else
    {
        ESP_LOGI(TAG, "HTTP服务器启动成功");
    }
    
    //注册配网相关路由1：访问根路径"/" → 返回配网网页（GET请求）
    httpd_register_uri_handler(httpd_handle, &(httpd_uri_t) {
        .uri = "/",                      // 根路径
        .method = HTTP_GET,              // 请求方式：GET（获取网页）
        .handler = get_handler,          // 处理函数    
        .user_ctx = NULL                 // 自定义参数（无需传参）
    });

    //注册配网相关路由2：提交配网参数"/save_config" → 接收参数（POST请求）
    httpd_register_uri_handler(httpd_handle, &(httpd_uri_t) {
        .uri = "/save_config",                // 提交路径
        .method = HTTP_POST,             // 请求方式：POST（提交数据）
        .handler = post_handler,         // 处理函数    
        .user_ctx = NULL                 // 自定义参数（无需传参）
    });
    
    //注册favicon.ico路由，避免浏览器自动请求导致404错误
    httpd_register_uri_handler(httpd_handle, &(httpd_uri_t) {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,         // 使用函数指针替代lambda表达式
        .user_ctx = NULL
    });

    return httpd_handle;

}

/*=============================favicon图标处理函数================================  
 * @brief: 处理favicon.ico请求，返回空的ICO响应
 * @param: req，HTTP请求结构体
 * @return: esp_err_t 函数执行结果
*/
static esp_err_t favicon_handler(httpd_req_t *req)
{
    // 返回空的ICO响应
    httpd_resp_set_type(req, "image/x-icon");
    return httpd_resp_send(req, NULL, 0);
}

/*=============================HTTP事件处理函数：GET请求===========================  
 * @brief: 用于处理HTTP GET请求：返回配网网页（用户输入路由器SSID/密码）
 * @param: r，HTTP请求结构体：包含请求信息
 * @return: esp_err_t 函数执行结果
*/
static esp_err_t get_handler(httpd_req_t *r)
{
    //检查连接是否仍然有效
    if(r == NULL) {
        ESP_LOGW(TAG, "HTTP请求指针为空");
        return ESP_ERR_INVALID_ARG;
    }

    //网页HTML代码
    const char* config_html = 
        "<!DOCTYPE html>"
        "<html lang='zh-CN'>"          
        "<head>"
        "   <meta charset='UTF-8'>"    
        "   <meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "   <title>ESP32配网</title>"
        "   <style>"
        "       body { font-family: Arial, sans-serif; text-align: center; margin: 50px; }"
        "       input[type=text], input[type=password] { padding: 10px; margin: 10px; width: 200px; }"
        "       input[type=submit] { padding: 12px 24px; background-color: #4CAF50; color: white; border: none; cursor: pointer; }"
        "       input[type=submit]:hover { background-color: #45a049; }"
        "   </style>"
        "</head>"
        "<body>"
        "   <h1>ESP32 WiFi配网</h1>"
        "   <p>请输入要连接的WiFi信息</p>"
        "   <!-- 表单:POST方式提交到/save_config路径 -->"
        "   <form method='POST' action='/save_config'>"
        "       路由器SSID:<br><input type='text' name='ssid' maxlength='31' required><br><br>"
        "       路由器密码：<br><input type='password' name='pwd' maxlength='63' required><br><br>"
        "       <input type='submit' value='进行配网'>"
        "   </form>"
        "</body>"
        "</html>";

    //设置响应头
    httpd_resp_set_type(r, "text/html");
    httpd_resp_set_status(r, "200 OK");
    
    //发送配网网页给客户端
    esp_err_t ret = httpd_resp_send(r, config_html, HTTPD_RESP_USE_STRLEN);
    
    if(ret != ESP_OK) {
        ESP_LOGW(TAG, "发送HTTP响应失败: %s", esp_err_to_name(ret));
    }
    
    return ret;

}

/*=============================HTTP 事件处理函数：POST 请求===========================  
 * @brief: 处理 HTTP POST 请求，接收用户输入的路由器 SSID/密码，配置连接
 * @param: r，结构体，包含要提交的信息
 * @return: esp_err_t 函数执行结果
*/

// 添加 URL 解码辅助函数
static void url_decode(char *str)
{
    char *src = str;
    char *dst = str;
    
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            // 转换两个十六进制字符为一个字节
            char hex[3] = {src[1], src[2], 0};
            *dst = (char)strtol(hex, NULL, 16);
            src += 3;
            dst++;
        } else if (*src == '+') {
            // '+' 转换为空格
            *dst = ' ';
            src++;
            dst++;
        } else {
            *dst = *src;
            src++;
            dst++;
        }
    }
    *dst = '\0';
}

static esp_err_t post_handler(httpd_req_t *r)
{
    //检查连接是否仍然有效
    if(r == NULL) {
        ESP_LOGW(TAG, "HTTP 请求指针为空");
        return ESP_ERR_INVALID_ARG;
    }

    //创建缓冲区：用于存储用户输入的路由器 SSID/密码
    char post_buf[128] = {0};

    //接收 POST 请求数据（缓冲区大小 -1，留位置给字符串结束符'\0'）
    int rec_data = httpd_req_recv(r, post_buf, sizeof(post_buf)-1);
    if (rec_data <= 0) {
        ESP_LOGW(TAG, "接收 POST 数据失败，返回值：%d", rec_data);
        const char* error_html = "<html><body><h1 style='color:red;'>数据接收失败！请重新尝试。</h1><a href='/'>返回重新配置</a></body></html>";
        httpd_resp_send(r, error_html, HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    //确保字符串正确结束
    post_buf[rec_data] = '\0';
    ESP_LOGI(TAG, "接收到 POST 数据：%s", post_buf);

    //解析数据：提取用户输入的路由器 SSID/密码
    char *ssid_ptr = strstr(post_buf, "ssid=");  //找到 SSID 的起始位置
    char *pwd_ptr = strstr(post_buf, "pwd=");    //找到密码的起始位置
    
    // 只要 SSID 或密码为空，直接返回错误
    if(ssid_ptr == NULL || pwd_ptr == NULL) {
        ESP_LOGW(TAG, "缺少 SSID 或密码参数");
        const char* error_html = "<html><body><h1 style='color:red;'>请输入完整的 SSID 和密码</h1><a href='/'>返回重新配置</a></body></html>";
        httpd_resp_send(r, error_html, HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    //提取并处理 SSID 和密码
    ssid_ptr += 5;                               // 跳过"ssid="
    char* ssid_end = strstr(ssid_ptr, "&");
    if(ssid_end) *ssid_end = '\0';               // 截断 SSID（去掉&及后面内容）
    pwd_ptr += 4;                                // 跳过"pwd="

    //验证输入长度
    if(strlen(ssid_ptr) == 0 || strlen(pwd_ptr) == 0) {
        ESP_LOGW(TAG, "SSID 或密码为空");
        const char* error_html = "<html><body><h1 style='color:red;'>SSID 和密码不能为空</h1><a href='/'>返回重新配置</a></body></html>";
        httpd_resp_send(r, error_html, HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // 对 SSID 和密码进行 URL 解码
    url_decode(ssid_ptr);
    url_decode(pwd_ptr);
    
    ESP_LOGI(TAG, "解析得到 - SSID: %s, 密码长度：%d", ssid_ptr, strlen(pwd_ptr));

    // 存储目标 WiFi 信息
    strncpy(target_ssid, ssid_ptr, sizeof(target_ssid) - 1);
    strncpy(target_password, pwd_ptr, sizeof(target_password) - 1);
    
    // 立即返回响应，不等待 WiFi 连接完成
    const char* response_html = 
        "<html><body>"
        "<h1 style='color:orange;'>配置已接收！</h1>"
        "<p>正在尝试连接 WiFi 网络：<strong>%s</strong></p>"
        "<p>请稍候查看连接状态...</p>"
        "<p><a href='/'>返回重新配置</a></p>"
        "</body></html>";
    
    char formatted_response[512];
    snprintf(formatted_response, sizeof(formatted_response), response_html, target_ssid);
    
    // 发送即时响应
    esp_err_t send_ret = httpd_resp_send(r, formatted_response, HTTPD_RESP_USE_STRLEN);
    if(send_ret != ESP_OK) {
        ESP_LOGW(TAG, "发送 HTTP 响应失败：%s", esp_err_to_name(send_ret));
    }

    // 启动异步 WiFi 连接任务
    if (!wifi_connecting) {
        wifi_connecting = true;
        BaseType_t ret = xTaskCreate(wifi_connect_task, "wifi_connect", 4096, NULL, 5, NULL);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "创建 WiFi 连接任务失败");
            wifi_connecting = false;
        } else {
            ESP_LOGI(TAG, "WiFi 连接任务已启动");
        }
    } else {
        ESP_LOGW(TAG, "WiFi 连接任务已在运行中");
    }

    return ESP_OK;
}

/*===============================WIFI初始化函数====================================== 
 * @brief: 用于WIFI相关初始化与配置
 * @param: 无
 * @return: 无
 * ===================================================================================*/
void My_WIFI_Init_AP(void)
{
    // 如果已经初始化，先停止并清理资源
    if (wifi_ap_initialized) {
        ESP_LOGI(TAG, "检测到 WiFi AP 已初始化，正在重新配置...");
        esp_wifi_stop();
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, NULL);
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_LOST_IP, NULL);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 初始化底层网络接口 (必须第一步)
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 创建默认事件循环 (WIFI 事件依赖默认事件循环)
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "事件循环已存在，继续使用");
    }
    //创建默认WIFI AP网络接口
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();
    //初始化WIFI驱动(默认配置)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    //注册WIFI事件（AP模式相关）
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, 
                                                        ESP_EVENT_ANY_ID, 
                                                        &wifi_event_handler, 
                                                        NULL, 
                                                        NULL));
    
    // 注册STA模式相关事件处理器（满足诊断规范要求）
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        WIFI_EVENT_STA_CONNECTED,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        WIFI_EVENT_STA_DISCONNECTED,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &ip_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_LOST_IP,
                                                        &ip_event_handler,
                                                        NULL,
                                                        NULL));

    //配置WIFI
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,                 //热点名称
            .ssid_len = strlen(WIFI_AP_SSID),     //热点名称长度
            .password = WIFI_AP_PASSWORD,         //热点密码
            .max_connection = 4,                  //最大连接数
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,   //认证模式
        },
    };

    if (strlen(WIFI_AP_SSID) == 0)   //如果没有设置密码，则将认证设置为开放模式
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    //设置WIFI模式为softAP模式(纯AP模式)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    //应用配置到WIFI AP接口
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    //启动WIFI驱动
    ESP_ERROR_CHECK(esp_wifi_start());

    //获取AP接口的IP信息并打印
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info); //WIFI_AP_DEF：默认WIFI AP接口
    // 将32位IP地址转为字符串（如192.168.4.1）
    char ip_addr[16];                           //IP 地址的最大长度是 xxx.xxx.xxx.xxx（比如255.255.255.255）
    inet_ntoa_r(ip_info.ip.addr, ip_addr, sizeof(ip_addr));    //internet(网络)_network to ascii(网络→ASCII)_reentrant(可重入/线程安全)
    ESP_LOGI(TAG, "获取AP的IP为:%s",ip_addr);

    ESP_LOGI(TAG, "WIFI_Init_AP 初始化完成");

    wifi_ap_initialized = true;

}

