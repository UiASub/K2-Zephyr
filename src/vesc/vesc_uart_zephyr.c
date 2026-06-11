#include "vesc_uart_zephyr.h"
#include "vesc_protocol.h"

#include <errno.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vesc_uart, LOG_LEVEL_INF);

#define VESC_UART_NODE DT_ALIAS(vesc_uart)

BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(VESC_UART_NODE),
             "vesc_uart alias not okay in DT");

static const struct device *vesc_uart = DEVICE_DT_GET(VESC_UART_NODE);

#define TX_BUF_SIZE 256

static uint8_t tx_buf[TX_BUF_SIZE];
static volatile uint16_t tx_head;
static volatile uint16_t tx_tail;
static struct k_sem tx_space;

static void uart_isr_callback(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    uart_irq_update(dev);

    if (!uart_irq_tx_ready(dev)) {
        return;
    }

    while (tx_tail != tx_head) {
        uint16_t head = tx_head;
        uint16_t tail = tx_tail;
        uint16_t chunk_len;

        if (head > tail) {
            chunk_len = head - tail;
        } else {
            chunk_len = TX_BUF_SIZE - tail;
        }

        int sent = uart_fifo_fill(dev, &tx_buf[tail], chunk_len);
        if (sent <= 0) {
            break;
        }

        tx_tail = (tail + sent) % TX_BUF_SIZE;

        for (int i = 0; i < sent; i++) {
            k_sem_give(&tx_space);
        }
    }

    if (tx_tail == tx_head) {
        uart_irq_tx_disable(dev);
    }
}

int vesc_uart_init(void)
{
    if (!device_is_ready(vesc_uart)) {
        LOG_ERR("VESC UART device not ready");
        return -ENODEV;
    }

    const struct uart_config uart_cfg = {
        .baudrate = 115200,
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

    tx_head = 0;
    tx_tail = 0;
    k_sem_init(&tx_space, TX_BUF_SIZE - 1, TX_BUF_SIZE - 1);

    uart_irq_callback_set(vesc_uart, uart_isr_callback);
    uart_irq_tx_disable(vesc_uart);

    LOG_INF("VESC UART initialized successfully (interrupt TX)");
    return 0;
}

void vesc_uart_send(const struct device *uart,
                    const uint8_t *buf,
                    size_t len)
{
    for (size_t i = 0; i < len; i++) {
        k_sem_take(&tx_space, K_FOREVER);

        unsigned int key = irq_lock();
        tx_buf[tx_head] = buf[i];
        tx_head = (tx_head + 1) % TX_BUF_SIZE;
        irq_unlock(key);

        uart_irq_tx_enable(uart);
    }
}

void vesc_set_duty_local(float duty)
{
    LOG_DBG("vesc_set_duty_local called with: %d/1000", (int)(duty * 1000));

    uint8_t tx[32];
    size_t len = vesc_build_set_duty(tx, duty);

    vesc_uart_send(vesc_uart, tx, len);
}

void vesc_set_duty_can(uint8_t can_id, float duty)
{
    LOG_DBG("vesc_set_duty_can[%d] called with: %d/1000",
            can_id, (int)(duty * 1000));

    uint8_t tx[32];
    size_t len = vesc_build_set_duty_can(tx, can_id, duty);

    vesc_uart_send(vesc_uart, tx, len);
}
