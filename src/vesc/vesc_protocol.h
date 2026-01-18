#pragma once

#include <stdint.h>
#include <stddef.h>

/* Build a SET_CURRENT packet (local VESC) */
size_t vesc_build_set_current(uint8_t *buf, float current);

/* Build a CAN-forwarded SET_CURRENT packet */
size_t vesc_build_can_set_current(uint8_t *buf,
                                  uint8_t can_id,
                                  float current);