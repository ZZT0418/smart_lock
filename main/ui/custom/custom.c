/*
* Copyright 2024 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/


/*********************
 *      INCLUDES
 *********************/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "custom.h"
#include "servo.h"
#include "esp_log.h"

/*********************
 *      DEFINES
 *********************/
#define TAG "custom"

#define PASSWORD_MAX_LENGTH     8                // 密码最大长度
#define USER_PASSWORD           "123456*#"       //密码（由纯数字或数字+特殊字符组成）

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

 //创建一个字符数组，大小为 MAX_PASSWORD_LENGTH + 1（多 1 位用于存放字符串结束符'\0'）
static char password_buffer[PASSWORD_MAX_LENGTH+1] = {0};           //用于存储用户输入的密码
//定义一个密码索引
static uint8_t password_index = 0;                                  //记录当前输入到第几位了
//保存按钮矩阵对象的指针，方便在其他函数中使用
static lv_obj_t *btn = NULL;


/**
 * Create a demo application
 */


void custom_init(lv_ui *ui)
{
    //保存按钮矩阵的指针
    btn = ui->screen_keyboard;
    //初始化时，清空密码缓冲区
    memset(password_buffer, 0, sizeof(password_buffer));
    //同时将密码索引置为0
    password_index = 0;
    ESP_LOGI(TAG,"初始化完成，请输入密码");
}

void check_password(char *password)
{ 
    ESP_LOGI(TAG,"正在验证密码......");
    if(strcmp(password,USER_PASSWORD)==0)
    {
        ESP_LOGI(TAG,"密码正确，正在开锁");
        servo_unlock();
        //重置密码缓冲区，等待下次输入
        memset(password_buffer, 0, sizeof(password_buffer));
        password_index = 0;
    }
    else
    {
        ESP_LOGI(TAG,"密码错误，请重新输入");
        //重置密码缓冲区，等待下次输入
        memset(password_buffer, 0, sizeof(password_buffer));
        password_index = 0;
    }
}

void button_input_handle(lv_obj_t * button, uint32_t button_id)
{
    //获取被按下按钮的文本内容(如"1","2","确认"等)
   const char *button_text=lv_buttonmatrix_get_button_text(button,button_id);
    //安全检查：如果按钮文本为空，直接返回
   if(button_text==NULL)
   {
    // ESP_LOGI(TAG,"按钮文本为空，请检查按钮矩阵配置");
    return;
   }
   else     //否则，打印按钮文本
   {
        ESP_LOGI(TAG,"输入了%s",button_text);
   }

    //情况1：如果按钮文本是"确认"，则说明用户输入完成，开始验证密码
   if(strcmp(button_text,"确认")==0)
   {
        //只有当输入了至少一位密码时才验证
        if(password_index>0)
        {
            password_buffer[password_index] = '\0';        //将密码缓冲区中的最后一个字符设置为字符串结束符'\0'
            ESP_LOGI(TAG,"用户输入完成，开始验证密码");
            check_password(password_buffer);
        }
        else
        {
            ESP_LOGI(TAG,"没有输入密码，确认无效");
        }
   }
   //情况2：如果按钮文本是"删除"，则说明用户想删除最后一个字符
   else if(strcmp(button_text,"删除")==0)
   {
        if(password_index>0)
        {
            //如果密码索引大于0，则说明用户已经输入了密码，可以删除最后一个字符
            password_index--;
            password_buffer[password_index] = '\0';        //将该位置的字符设置为空字符
            ESP_LOGI(TAG,"删除了最后一位密码");
        }
        else
        {
            ESP_LOGI(TAG,"没有输入密码，删除无效");
        }
    }
    //情况3：如果按钮文本是响铃，喇叭提示音
    else if(strcmp(button_text,"响铃")==0)
    {
        ESP_LOGI(TAG,"响铃，铃铃铃......");
        //播放提示音
    }
    // 情况4：按钮文本是数字或特殊字符* #，则说明用户正在输入密码
    else
    {
        if(password_index<PASSWORD_MAX_LENGTH)
        {
            //如果密码索引小于最大长度，则说明用户还没有输入满8位密码，可以继续输入
            password_buffer[password_index] = button_text[0];      //将按钮文本的字符复制到密码缓冲区中
            password_index++;                                     //密码索引加1 
        }
    }
}
