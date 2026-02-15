#include "vesc_uart_zephyr.h"
#include "vesc_protocol.h"
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vesc_uart, LOG_LEVEL_INF);

/* Get USART6 device for VESC communication */
static const struct device *vesc_uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

int vesc_uart_init(void)
{
    /* Check if device is ready */
    if (!device_is_ready(vesc_uart)) {
        LOG_ERR("VESC UART device not ready");
        return -ENODEV;
    }

    /* Optional: Configure UART if not done in devicetree */
    struct uart_config uart_cfg = {
        .baudrate = 115200,  /* VESC default is 115200 */
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    int ret = uart_configure(vesc_uart, &uart_cfg);
    if (ret < 0) {
        LOG_ERR("Failed to configure VESC UART: %d", ret);
        return ret;
    }

    LOG_INF("VESC UART initialized successfully");
    return 0;
}

void vesc_uart_send(const struct device *uart,
                    const uint8_t *buf,
                    size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart, buf[i]);
    }
}

void vesc_set_duty_local(float duty)
{
    LOG_DBG("vesc_set_duty_local called with: %d/1000", (int)(duty * 1000));
    uint8_t tx_buf[32];
    size_t len = vesc_build_set_duty(tx_buf, duty);
    vesc_uart_send(vesc_uart, tx_buf, len);
}

void vesc_set_duty_can(uint8_t can_id, float duty)
{
    LOG_DBG("vesc_set_duty_can[%d] called with: %d/1000", can_id, (int)(duty * 1000));
    uint8_t tx_buf[32];
    size_t len = vesc_build_set_duty_can(tx_buf, can_id, duty);
    vesc_uart_send(vesc_uart, tx_buf, len);
}