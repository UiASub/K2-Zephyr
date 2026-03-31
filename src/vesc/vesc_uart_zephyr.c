#include "vesc_uart_zephyr.h"
#include "vesc_protocol.h"
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vesc_uart, LOG_LEVEL_INF);

/* Get USART6 device for VESC communication */
static const struct device *vesc_uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

/*
 * Interrupt-driven TX ring buffer.
 *
 * 256 bytes is enough for several queued VESC packets
 * (each duty command is ~10-12 bytes on the wire).
 */
#define TX_BUF_SIZE 256

static uint8_t  tx_buf[TX_BUF_SIZE];
static volatile uint16_t tx_head;   /* next write position  */
static volatile uint16_t tx_tail;   /* next read  position  */
static struct k_sem tx_sem;         /* signalled when space available */

/*
 * UART TX ISR callback — called when the UART hardware is ready for more
 * bytes.  We feed bytes from the ring buffer until it's empty, then
 * disable the TX interrupt so it doesn't keep firing.
 */
static void uart_isr_callback(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) {
        return;
    }

    if (uart_irq_tx_ready(dev)) {
        uint16_t head = tx_head;
        uint16_t tail = tx_tail;

        if (tail == head) {
            /* Buffer empty — disable TX interrupt */
            uart_irq_tx_disable(dev);
            return;
        }

        /* Feed as many bytes as the FIFO will accept */
        uint16_t total_sent = 0;
        while (tail != head) {
            uint16_t chunk_end;
            if (head > tail) {
                chunk_end = head;
            } else {
                chunk_end = TX_BUF_SIZE;
            }

            int sent = uart_fifo_fill(dev, &tx_buf[tail], chunk_end - tail);
            if (sent <= 0) {
                break;
            }
            tail = (tail + sent) % TX_BUF_SIZE;
            total_sent += sent;
        }

        tx_tail = tail;

        /* Give back one semaphore count per byte consumed so the
         * producer's available-space tracking stays in sync. */
        for (uint16_t i = 0; i < total_sent; i++) {
            k_sem_give(&tx_sem);
        }
    }
}

int vesc_uart_init(void)
{
    /* Check if device is ready */
    if (!device_is_ready(vesc_uart)) {
        LOG_ERR("VESC UART device not ready");
        return -ENODEV;
    }

    /* Configure UART */
    struct uart_config uart_cfg = {
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

    /* Initialise ring buffer state */
    tx_head = 0;
    tx_tail = 0;
    k_sem_init(&tx_sem, TX_BUF_SIZE, TX_BUF_SIZE);

    /* Register ISR and leave TX interrupt disabled until we have data */
    uart_irq_callback_set(vesc_uart, uart_isr_callback);
    uart_irq_tx_disable(vesc_uart);

    LOG_INF("VESC UART initialized successfully (interrupt-driven TX)");
    return 0;
}

void vesc_uart_send(const struct device *uart,
                    const uint8_t *buf,
                    size_t len)
{
    for (size_t i = 0; i < len; i++) {
        /* Wait until there is space in the ring buffer */
        k_sem_take(&tx_sem, K_FOREVER);

        unsigned int key = irq_lock();
        uint16_t next = (tx_head + 1) % TX_BUF_SIZE;
        tx_buf[tx_head] = buf[i];
        tx_head = next;
        irq_unlock(key);
    }

    /* Kick the TX interrupt so the ISR starts draining */
    uart_irq_tx_enable(uart);
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
    LOG_DBG("vesc_set_duty_can[%d] called with: %d/1000", can_id, (int)(duty * 1000));
    uint8_t tx[32];
    size_t len = vesc_build_set_duty_can(tx, can_id, duty);
    vesc_uart_send(vesc_uart, tx, len);
}
