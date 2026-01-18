#include "vesc_uart_zephyr.h"
#include <zephyr/drivers/uart.h>

void vesc_uart_send(const struct device *uart,
                    const uint8_t *buf,
                    size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart, buf[i]);
    }
}
