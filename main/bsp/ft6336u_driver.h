#ifndef _FT6336U_DRIVER_H_
#define _FT6336U_DRIVER_H_ 
#include "driver/gpio.h"
#include "esp_err.h"


esp_err_t ft6336u_driver_init(gpio_num_t sda,gpio_num_t scl);
void ft6336u_read(int16_t *x,int16_t *y,int *state);
void ft6336u_interrupt_info(bool flag);

#endif
