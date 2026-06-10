#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(esc_debug, LOG_LEVEL_DBG);

#define VESC_UART_NODE DT_ALIAS(vesc_uart)

BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(VESC_UART_NODE),
	     "vesc_uart alias must point to an enabled UART in the board overlay");

#define ESC_BAUDRATE 115200
#define QUERY_INTERVAL_MS 1000
#define RX_IDLE_FLUSH_MS 50
#define RX_BUF_SIZE 256

#define VESC_START_BYTE 0x02
#define VESC_STOP_BYTE 0x03
#define COMM_GET_VALUES 4
#define COMM_SET_DUTY 5

static const struct device *const esc_uart = DEVICE_DT_GET(VESC_UART_NODE);

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
	uint16_t crc = 0;

	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (int bit = 0; bit < 8; bit++) {
			if ((crc & 0x8000) != 0U) {
				crc = (crc << 1) ^ 0x1021;
			} else {
				crc <<= 1;
			}
		}
	}

	return crc;
}

static void append_i32(uint8_t *buf, int32_t value, size_t *idx)
{
	buf[(*idx)++] = (uint8_t)((value >> 24) & 0xff);
	buf[(*idx)++] = (uint8_t)((value >> 16) & 0xff);
	buf[(*idx)++] = (uint8_t)((value >> 8) & 0xff);
	buf[(*idx)++] = (uint8_t)(value & 0xff);
}

static size_t wrap_packet(uint8_t *packet, const uint8_t *payload, size_t payload_len)
{
	size_t idx = 0;
	uint16_t crc = crc16_ccitt(payload, payload_len);

	packet[idx++] = VESC_START_BYTE;
	packet[idx++] = (uint8_t)payload_len;
	for (size_t i = 0; i < payload_len; i++) {
		packet[idx++] = payload[i];
	}
	packet[idx++] = (uint8_t)(crc >> 8);
	packet[idx++] = (uint8_t)crc;
	packet[idx++] = VESC_STOP_BYTE;

	return idx;
}

static size_t build_get_values(uint8_t *packet)
{
	const uint8_t payload[] = { COMM_GET_VALUES };

	return wrap_packet(packet, payload, sizeof(payload));
}

static size_t build_zero_duty(uint8_t *packet)
{
	uint8_t payload[5];
	size_t idx = 0;

	payload[idx++] = COMM_SET_DUTY;
	append_i32(payload, 0, &idx);

	return wrap_packet(packet, payload, idx);
}

static int configure_esc_uart(void)
{
	if (!device_is_ready(esc_uart)) {
		LOG_ERR("ESC UART device is not ready");
		return -ENODEV;
	}

	const struct uart_config cfg = {
		.baudrate = ESC_BAUDRATE,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	int ret = uart_configure(esc_uart, &cfg);

	if (ret < 0) {
		LOG_ERR("Failed to configure ESC UART %s at %d baud: %d",
			esc_uart->name, ESC_BAUDRATE, ret);
		return ret;
	}

	LOG_INF("ESC UART ready: %s, %d 8N1", esc_uart->name, ESC_BAUDRATE);
	return 0;
}

static void send_packet(const char *label, const uint8_t *packet, size_t len)
{
	LOG_INF("TX %s (%u bytes)", label, (unsigned int)len);
	LOG_HEXDUMP_INF(packet, len, "tx");

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(esc_uart, packet[i]);
	}
}

static void poll_rx(uint8_t *rx, size_t *rx_len, int64_t *last_rx_ms)
{
	uint8_t byte;

	while (uart_poll_in(esc_uart, &byte) == 0) {
		if (*rx_len >= RX_BUF_SIZE) {
			LOG_WRN("RX buffer full; flushing partial frame");
			LOG_HEXDUMP_INF(rx, *rx_len, "rx");
			*rx_len = 0;
		}

		rx[(*rx_len)++] = byte;
		*last_rx_ms = k_uptime_get();
	}

	if (*rx_len > 0 && (k_uptime_get() - *last_rx_ms) >= RX_IDLE_FLUSH_MS) {
		LOG_INF("RX idle flush (%u bytes)", (unsigned int)*rx_len);
		LOG_HEXDUMP_INF(rx, *rx_len, "rx");
		*rx_len = 0;
	}
}

int main(void)
{
	uint8_t packet[16];
	uint8_t rx[RX_BUF_SIZE];
	size_t rx_len = 0;
	int64_t last_rx_ms = 0;
	int64_t next_query_ms;
	uint32_t query_count = 0;

	LOG_INF("minimal ESC UART debug starting on %s", CONFIG_BOARD);

	if (configure_esc_uart() < 0) {
		return 0;
	}

	next_query_ms = k_uptime_get();

	while (true) {
		int64_t now = k_uptime_get();

		poll_rx(rx, &rx_len, &last_rx_ms);

		if (now >= next_query_ms) {
			size_t len = build_get_values(packet);

			send_packet("COMM_GET_VALUES", packet, len);
			query_count++;

			if ((query_count % 5U) == 0U) {
				len = build_zero_duty(packet);
				send_packet("COMM_SET_DUTY zero", packet, len);
			}

			next_query_ms = now + QUERY_INTERVAL_MS;
		}

		k_sleep(K_MSEC(10));
	}
}
