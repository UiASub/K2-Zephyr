#pragma once

#include <zephyr/device.h>
#include <stddef.h>
#include <stdint.h>

void vesc_uart_send(const struct device *uart,
                    const uint8_t *buf,
                    size_t len);

/* Receive bytes with timeout (returns bytes received) */
size_t vesc_uart_recv(const struct device *uart,
                      uint8_t *buf,
                      size_t max_len,
                      int timeout_ms);
