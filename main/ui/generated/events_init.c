/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "events_init.h"
#include <stdio.h>
#include "lvgl.h"
#include "custom.h"

#if LV_USE_GUIDER_SIMULATOR && LV_USE_FREEMASTER
#include "freemaster_client.h"
#endif


static void screen_keyboard_event_handler (lv_event_t *e)
{
    //获取事件类型
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:      //事件类型:点击
    {
        //获取触发事件的对象（就是按钮矩阵）
        lv_obj_t * obj = lv_event_get_target(e);
        //获取按钮矩阵选中的按钮ID
        uint32_t id = lv_buttonmatrix_get_selected_button(obj);
        //把按钮矩阵对象和按钮ID传递给按钮输入处理函数
        button_input_handle(obj, id);
        break;
    }
    default:
        break;
    }
}

void events_init_screen (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->screen_keyboard, screen_keyboard_event_handler, LV_EVENT_ALL, ui);
}


void events_init(lv_ui *ui)
{

}
