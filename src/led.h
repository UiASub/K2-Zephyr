#pragma once

#include <zephyr/drivers/gpio.h>

extern const struct gpio_dt_spec led;
void led_init(void);