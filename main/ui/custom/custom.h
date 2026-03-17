/*
* Copyright 2024 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#ifndef __CUSTOM_H_
#define __CUSTOM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "gui_guider.h"
#include "stdint.h"

void custom_init(lv_ui *ui);

/*============================声明按钮输入处理函数=======================
 *说明：此函数用于处理按钮输入事件，根据按钮编号进行相应的处理。
 *参数：button - 按钮矩阵对象，button_id - 被按下的按钮编号
 *返回：无
*/
void button_input_handle(lv_obj_t *button, uint32_t button_id);

#ifdef __cplusplus
}
#endif
#endif /* EVENT_CB_H_ */
