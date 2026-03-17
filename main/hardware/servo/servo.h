#ifndef _SERVO_H_
#define _SERVO_H_
#include "stdint.h"

void servo_init(void);
void servo_set_angle(uint8_t angle);
void set_pwm_duty(uint32_t duty);
void servo_set_angle(uint8_t angle);
void servo_unlock(void);


#endif