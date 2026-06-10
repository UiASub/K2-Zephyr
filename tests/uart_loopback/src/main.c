#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define LOOPBACK_UART_NODE DT_ALIAS(loopback_uart)

BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(LOOPBACK_UART_NODE),
             "loopback-uart alias is not okay in devicetree");

#define BAUDRATE 115200
#define RX_TIMEOUT_MS 250
#define TEST_PERIOD_MS 1000
#define PACKET_LEN 16

static const struct device *const loopback_uart = DEVICE_DT_GET(LOOPBACK_UART_NODE);

static void drain_rx(void)
{
    uint8_t byte;

    while (uart_poll_in(loopback_uart, &byte) == 0) {
    }
}

static int wait_for_byte(uint8_t *byte, int32_t timeout_ms)
{
    const int64_t deadline = k_uptime_get() + timeout_ms;

    do {
        if (uart_poll_in(loopback_uart, byte) == 0) {
            return 0;
        }

        k_sleep(K_MSEC(1));
    } while (k_uptime_get() < deadline);

    return -ETIMEDOUT;
}

static uint8_t checksum8(const uint8_t *buf, size_t len)
{
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum ^= buf[i];
    }

    return sum;
}

static void make_packet(uint8_t *packet, uint32_t sequence)
{
    packet[0] = 0xa5;
    packet[1] = 0x5a;
    packet[2] = (uint8_t)(sequence >> 24);
    packet[3] = (uint8_t)(sequence >> 16);
    packet[4] = (uint8_t)(sequence >> 8);
    packet[5] = (uint8_t)sequence;
    packet[6] = 0x00;
    packet[7] = 0x11;
    packet[8] = 0x22;
    packet[9] = 0x7e;
    packet[10] = 0x80;
    packet[11] = 0xff;
    packet[12] = (uint8_t)(sequence * 3U);
    packet[13] = (uint8_t)(sequence * 7U);
    packet[14] = 0xc3;
    packet[15] = checksum8(packet, PACKET_LEN - 1);
}

static int run_loopback_test(uint32_t sequence)
{
    uint8_t tx[PACKET_LEN];
    uint8_t rx[PACKET_LEN];

    make_packet(tx, sequence);
    memset(rx, 0, sizeof(rx));

    drain_rx();

    for (size_t i = 0; i < sizeof(tx); i++) {
        uart_poll_out(loopback_uart, tx[i]);

        int ret = wait_for_byte(&rx[i], RX_TIMEOUT_MS);
        if (ret < 0) {
            int err = uart_err_check(loopback_uart);

            printk("FAIL: timeout at byte %u/%u, uart_err=0x%x\n",
                   (unsigned int)i, (unsigned int)sizeof(tx),
                   (unsigned int)err);
            return ret;
        }

        if (rx[i] != tx[i]) {
            printk("FAIL: byte %u mismatch, tx=0x%02x rx=0x%02x\n",
                   (unsigned int)i, tx[i], rx[i]);
            return -EIO;
        }
    }

    printk("PASS: packet %u looped back (%u bytes)\n",
           (unsigned int)sequence, (unsigned int)sizeof(tx));
    return 0;
}

int main(void)
{
    uint32_t sequence = 0;

    printk("\nK2 UART loopback test\n");
    printk("UART: %s, %d 8N1\n", loopback_uart->name, BAUDRATE);
    printk("Wire PB6 LPUART1_TX directly to PB7 LPUART1_RX.\n\n");

    if (!device_is_ready(loopback_uart)) {
        printk("FAIL: UART device is not ready\n");
        return 0;
    }

    const struct uart_config config = {
        .baudrate = BAUDRATE,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    int ret = uart_configure(loopback_uart, &config);
    if (ret < 0) {
        printk("FAIL: uart_configure returned %d\n", ret);
        return 0;
    }

    while (true) {
        (void)run_loopback_test(sequence++);
        k_sleep(K_MSEC(TEST_PERIOD_MS));
    }
}
