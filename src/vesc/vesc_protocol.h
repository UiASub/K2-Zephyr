#pragma once

#include <stdint.h>
#include <stddef.h>

/* Build a SET_DUTY packet (-1.0 to 1.0 = -100% to +100%) */
size_t vesc_build_set_duty(uint8_t *buf, float duty);

/* Build a CAN forwarded duty command (COMM_FORWARD_CAN) */
size_t vesc_build_set_duty_can(uint8_t *buf, uint8_t can_id, float duty);