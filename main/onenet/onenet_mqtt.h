#ifndef _ONENET_MQTT_H_
#define _ONENET_MQTT_H_ 
#include "esp_err.h"

#define ONENET_PRODUCT_ID   "idB0K035tx"                                                //onenet产品ID
#define ONETNET_ACCESS_KEY  "iStnd6G/Bw9Fa7c7eX2nERn5B6mP2/9zV//tQX3nJZc="              //onenet产品密钥
#define ONENET_DEVICE_NAME  "smart_lock"                                                //onenet设备名称

esp_err_t onenet_mqtt_start(void);
esp_err_t onenet_mqtt_subscribe(void);

#endif
