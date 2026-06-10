#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <esp_rom_serial_output.h>
#include <esp_rom_sys.h>

#define VESC_UART_NODE DT_ALIAS(vesc_uart)

BUILD_ASSERT(DT_NODE_HAS_STATUS(VESC_UART_NODE, okay),
	     "vesc-uart alias is not okay");

static const struct device *const vesc_uart = DEVICE_DT_GET(VESC_UART_NODE);

static const uint8_t VESC_START_SHORT = 0x02;
static const uint8_t VESC_START_LONG = 0x03;
static const uint8_t VESC_STOP = 0x03;
static const uint8_t COMM_FW_VERSION = 0;
static const uint8_t COMM_SET_DUTY = 5;

static const uint32_t bauds[] = {
	115200,
	57600,
	38400,
	19200,
	9600,
	230400,
	460800,
	921600,
};

static const int32_t test_duty_per_mille = 100;
static const int32_t duty_pulse_ms = 700;
static const int32_t stop_hold_ms = 1500;

static void rom_log(const char *msg)
{
	esp_rom_printf("%s", msg);
	esp_rom_output_tx_wait_idle(CONFIG_ESP_CONSOLE_UART_NUM);
}

static uint16_t crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0;

	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (uint8_t bit = 0; bit < 8; bit++) {
			if ((crc & 0x8000) != 0U) {
				crc = (uint16_t)((crc << 1) ^ 0x1021);
			} else {
				crc = (uint16_t)(crc << 1);
			}
		}
	}

	return crc;
}

static size_t wrap_packet(uint8_t *out, size_t out_size, const uint8_t *payload,
			  size_t payload_len)
{
	if (payload_len > 255 || out_size < payload_len + 5) {
		return 0;
	}

	const uint16_t crc = crc16(payload, payload_len);
	size_t idx = 0;

	out[idx++] = VESC_START_SHORT;
	out[idx++] = (uint8_t)payload_len;
	for (size_t i = 0; i < payload_len; i++) {
		out[idx++] = payload[i];
	}
	out[idx++] = (uint8_t)(crc >> 8);
	out[idx++] = (uint8_t)(crc & 0xff);
	out[idx++] = VESC_STOP;

	return idx;
}

static void print_hex(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		printk("%02x ", data[i]);
	}
	printk("\n");
}

static void flush_rx(void)
{
	uint8_t ch;

	while (uart_poll_in(vesc_uart, &ch) == 0) {
	}
}

static void write_packet(const uint8_t *payload, size_t payload_len)
{
	uint8_t packet[16];
	const size_t len = wrap_packet(packet, sizeof(packet), payload, payload_len);

	if (len == 0) {
		printk("packet build failed\n");
		return;
	}

	printk("tx ");
	print_hex(packet, len);

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(vesc_uart, packet[i]);
	}
}

static size_t read_rx(uint8_t *rx, size_t rx_size, int64_t first_timeout_ms,
		      int64_t idle_timeout_ms, int64_t *first_byte_ms,
		      int64_t *last_byte_ms)
{
	size_t len = 0;
	const int64_t start = k_uptime_get();
	int64_t deadline = start + first_timeout_ms;

	*first_byte_ms = -1;
	*last_byte_ms = -1;

	while (k_uptime_get() < deadline && len < rx_size) {
		uint8_t ch;

		if (uart_poll_in(vesc_uart, &ch) == 0) {
			const int64_t now = k_uptime_get();

			if (len == 0) {
				*first_byte_ms = now - start;
			}
			*last_byte_ms = now - start;
			rx[len++] = ch;
			deadline = now + idle_timeout_ms;
		} else {
			k_sleep(K_MSEC(1));
		}
	}

	return len;
}

static bool validate_packet(const uint8_t *rx, size_t len)
{
	size_t payload_offset;
	size_t payload_len;
	size_t expected_len;

	if (len < 5) {
		printk("rx too short\n");
		return false;
	}

	if (rx[0] == VESC_START_SHORT) {
		payload_offset = 2;
		payload_len = rx[1];
		expected_len = payload_len + 5;
	} else if (rx[0] == VESC_START_LONG) {
		if (len < 6) {
			printk("long rx too short\n");
			return false;
		}
		payload_offset = 3;
		payload_len = ((size_t)rx[1] << 8) | rx[2];
		expected_len = payload_len + 6;
	} else {
		printk("bad start 0x%02x\n", rx[0]);
		return false;
	}

	if (len < expected_len) {
		printk("incomplete packet, expected %zu\n", expected_len);
		return false;
	}

	const uint16_t calc = crc16(&rx[payload_offset], payload_len);
	const uint16_t recv = ((uint16_t)rx[payload_offset + payload_len] << 8) |
			      rx[payload_offset + payload_len + 1];
	const uint8_t stop = rx[payload_offset + payload_len + 2];

	if (stop != VESC_STOP) {
		printk("bad stop 0x%02x\n", stop);
		return false;
	}
	if (calc != recv) {
		printk("crc mismatch calc=0x%04x recv=0x%04x\n", calc, recv);
		return false;
	}

	printk("valid VESC packet cmd=%u payload_len=%zu\n",
	       rx[payload_offset], payload_len);
	return true;
}

static bool query_firmware(void)
{
	const uint8_t payload[] = { COMM_FW_VERSION };
	uint8_t rx[128];
	int64_t first_byte_ms;
	int64_t last_byte_ms;

	flush_rx();
	write_packet(payload, sizeof(payload));

	const size_t len = read_rx(rx, sizeof(rx), 700, 150,
				   &first_byte_ms, &last_byte_ms);
	if (len == 0) {
		printk("no response\n");
		return false;
	}

	printk("rx %zu byte(s), first=%lldms last=%lldms: ",
	       len, (long long)first_byte_ms, (long long)last_byte_ms);
	print_hex(rx, len);
	return validate_packet(rx, len);
}

static bool configure_baud(uint32_t baud)
{
	const struct uart_config config = {
		.baudrate = baud,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	const int ret = uart_configure(vesc_uart, &config);

	if (ret != 0) {
		printk("uart_configure(%u) failed: %d\n", baud, ret);
		return false;
	}

	k_sleep(K_MSEC(100));
	return true;
}

static bool probe_bauds(uint32_t *active_baud)
{
	for (size_t i = 0; i < ARRAY_SIZE(bauds); i++) {
		printk("probe baud %u\n", bauds[i]);
		if (!configure_baud(bauds[i])) {
			continue;
		}

		if (query_firmware()) {
			*active_baud = bauds[i];
			printk("using baud %u\n", *active_baud);
			return true;
		}
	}

	*active_baud = 115200;
	(void)configure_baud(*active_baud);
	printk("no valid VESC response; restored %u and will not send duty pulses\n",
	       *active_baud);
	return false;
}

static void append_i32(uint8_t *buf, size_t *idx, int32_t value)
{
	buf[(*idx)++] = (uint8_t)((uint32_t)value >> 24);
	buf[(*idx)++] = (uint8_t)((uint32_t)value >> 16);
	buf[(*idx)++] = (uint8_t)((uint32_t)value >> 8);
	buf[(*idx)++] = (uint8_t)value;
}

static void send_duty_per_mille(int32_t duty_per_mille)
{
	uint8_t payload[5];
	size_t idx = 0;
	const int32_t duty_value = duty_per_mille * 100;

	payload[idx++] = COMM_SET_DUTY;
	append_i32(payload, &idx, duty_value);
	write_packet(payload, idx);
}

int main(void)
{
	uint32_t active_baud;

	rom_log("\nESP32-S3 VESC probe main()\n");
	printk("\nESP32-S3 VESC probe\n");
	printk("uart=%s tx=GPIO17/IO17 rx=GPIO18/IO18 duty=%d/1000\n",
	       vesc_uart->name, test_duty_per_mille);

	if (!device_is_ready(vesc_uart)) {
		printk("VESC UART device is not ready\n");
		return 1;
	}

	k_sleep(K_MSEC(1000));

	if (!probe_bauds(&active_baud)) {
		rom_log("no VESC ACK; duty disabled\n");
		printk("probe complete: no VESC ACK; not sending duty commands\n");
		return 0;
	}

	printk("firmware ACK seen; starting %d/1000 duty pulse test\n",
	       test_duty_per_mille);
	send_duty_per_mille(0);
	k_sleep(K_MSEC(2000));

	while (true) {
		printk("duty %d/1000 for %d ms\n", test_duty_per_mille,
		       duty_pulse_ms);
		send_duty_per_mille(test_duty_per_mille);
		k_sleep(K_MSEC(duty_pulse_ms));

		printk("stop\n");
		send_duty_per_mille(0);
		k_sleep(K_MSEC(stop_hold_ms));
	}

	return 0;
}
