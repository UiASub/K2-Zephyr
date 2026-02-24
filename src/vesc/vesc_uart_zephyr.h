#pragma once

#include <zephyr/device.h>
#include <stddef.h>
#include <stdint.h>
#include "vesc_protocol.h"

int vesc_uart_init(void);

void vesc_uart_send(const struct device *uart,
                    const uint8_t *buf,
                    size_t len);

/**
 * @brief Set duty cycle for local VESC (connected via UART)
 * @param duty Duty cycle (-1.0 to +1.0)
 */
void vesc_set_duty_local(float duty);

/**
 * @brief Set duty cycle for remote VESC (connected via CAN)
 * @param can_id CAN ID of the VESC (0-253)
 * @param duty Duty cycle (-1.0 to +1.0)
 */
void vesc_set_duty_can(uint8_t can_id, float duty);