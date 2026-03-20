#ifndef _ONENET_DM_H_
#define _ONENET_DM_H_ 

#include "cJSON.h"                              // 引入cJSON库（C语言处理JSON数据的工具）

//门锁状态属性枚举
typedef enum
{
    LOCK_STATE_POEN = 0,        //门锁打开
    LOCK_STATE_CLOSE = 1        //门锁关闭
} LockState_t;
//开锁方式属性枚举
typedef enum
{
    UNLOCK_WAYS_PASSWORD = 0,           //密码方式
    UNLOCK_WAYS_FINGERPRINT = 1,        //指纹方式
    UNLOCK_WAYS_CARD = 2,               //卡片方式
    UNLOCK_WAYS_BT = 3,                 //远程方式
    UNLOCK_WAYS_WAITING = 4             //开锁方式验证中
} Unlockways_t;
//门锁报警属性枚举
typedef enum
{
    LOCK_ALARM_OFF = 0,          //未报警
    LOCK_ALARM_ON = 1           //启动报警
} LockAlarm_t;



// void onenet_dm_init(void);                                  // 初始化(device model)
void onenet_property_handle(cJSON *platform_data);          // 属性处理，处理平台下发的物模型数据（下行）
cJSON *onenet_property_upload(void);                        // 属性上传，返回物模型数据（上行）; 返回：包含属性值的cJSON对象（需手动释放内存）
cJSON *onenet_event_upload(void);                           // 事件上传，返回物模型数据（上行）; 返回：包含事件值的cJSON对象（需手动释放内存）


#endif