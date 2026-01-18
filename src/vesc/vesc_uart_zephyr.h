#pragma once

#include <zephyr/device.h>
#include <stddef.h>
#include <stdint.h>

void vesc_uart_send(const struct device *uart,
                    const uint8_t *buf,
                    size_t len);
