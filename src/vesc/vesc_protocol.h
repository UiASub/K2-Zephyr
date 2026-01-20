#pragma once

#include <stdint.h>
#include <stddef.h>

/* Build a SET_DUTY packet (-1.0 to 1.0 = -100% to +100%) */
size_t vesc_build_set_duty(uint8_t *buf, float duty);

/* Build a SET_CURRENT packet (local VESC) */
size_t vesc_build_set_current(uint8_t *buf, float current);

/* Build a GET_VALUES request (VESC will respond with telemetry) */
size_t vesc_build_get_values(uint8_t *buf);

/* Build a CAN-forwarded SET_CURRENT packet */
size_t vesc_build_can_set_current(uint8_t *buf,
                                  uint8_t can_id,
                                  float current);