#include "vesc_uart_zephyr.h"
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

void vesc_uart_send(const struct device *uart,
                    const uint8_t *buf,
                    size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart, buf[i]);
    }
}

size_t vesc_uart_recv(const struct device *uart,
                      uint8_t *buf,
                      size_t max_len,
                      int timeout_ms)
{
    size_t idx = 0;
    int64_t start = k_uptime_get();
    
    while (idx < max_len) {
        uint8_t c;
        if (uart_poll_in(uart, &c) == 0) {
            buf[idx++] = c;
            /* If we got end byte and have a complete packet, done */
            if (c == 0x03 && idx >= 6) {
                break;
            }
        } else {
            if ((k_uptime_get() - start) >= timeout_ms) {
                break;
            }
            k_usleep(100);
        }
    }
    return idx;
}
