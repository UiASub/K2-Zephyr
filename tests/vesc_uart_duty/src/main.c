#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "vesc/vesc_protocol.h"

#define VESC_UART_NODE DT_ALIAS(vesc_test_uart)

BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(VESC_UART_NODE),
             "vesc-test-uart alias is not okay in devicetree");

#define BAUDRATE 115200
#define START_DELAY_MS 3000
#define COMMAND_PERIOD_MS 100
#define DUTY_CYCLE 0.20f

static const struct device *const vesc_uart = DEVICE_DT_GET(VESC_UART_NODE);

static void uart_send(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(vesc_uart, buf[i]);
    }
}

static void send_duty(float duty)
{
    uint8_t packet[32];
    const size_t len = vesc_build_set_duty(packet, duty);

    uart_send(packet, len);
}

int main(void)
{
    printk("\nK2 VESC UART duty test\n");
    printk("UART: %s, %d 8N1\n", vesc_uart->name, BAUDRATE);
    printk("PB6/D1 TX -> VESC RX, PB7/D0 RX -> VESC TX, common GND.\n");
    printk("Duty command: %.2f\n", (double)DUTY_CYCLE);

    if (!device_is_ready(vesc_uart)) {
        printk("FAIL: VESC UART device is not ready\n");
        return 0;
    }

    const struct uart_config config = {
        .baudrate = BAUDRATE,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    int ret = uart_configure(vesc_uart, &config);
    if (ret < 0) {
        printk("FAIL: uart_configure returned %d\n", ret);
        return 0;
    }

    printk("Sending zero duty for startup delay...\n");
    for (int i = 0; i < START_DELAY_MS / COMMAND_PERIOD_MS; i++) {
        send_duty(0.0f);
        k_sleep(K_MSEC(COMMAND_PERIOD_MS));
    }

    printk("Sending duty %.2f every %d ms.\n",
           (double)DUTY_CYCLE, COMMAND_PERIOD_MS);

    while (true) {
        send_duty(DUTY_CYCLE);
        k_sleep(K_MSEC(COMMAND_PERIOD_MS));
    }
}
