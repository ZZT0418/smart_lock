#ifndef _SERVO_H_
#define _SERVO_H_
#include "stdint.h"
#include <stdbool.h>
#include "onenet_dm.h"

extern LockState_t Lock_state;

//状态机枚举，定义门锁的不同状态，便于在程序中进行状态管理和控制
typedef enum {
    LOCK_STATE_IDLE = 0,            // 空闲状态，等待开锁请求
    LOCK_STATE_BUSY = 1             // 忙碌状态，正在执行开锁动作
} LockControlState_t;

void servo_init(void);
void servo_set_angle(uint8_t angle);
void set_pwm_duty(uint32_t duty);
void servo_set_angle(uint8_t angle);
void servo_unlock(void);
void servo_mutex_lock(void);
bool lock_manager_request_unlock(Unlockways_t way);

#endif