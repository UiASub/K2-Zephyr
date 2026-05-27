#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "../imu/vn100s.h"
#include "../net/net.h"
#include "oled.h"

LOG_MODULE_REGISTER(oled, LOG_LEVEL_INF);

#define OLED_THREAD_STACK_SIZE 2048
#define OLED_THREAD_PRIORITY 6
#define OLED_WIDTH_PIXELS 128
#define OLED_REFRESH_MS 200
#define OLED_LINE_MAX 32
#define OLED_MAX_FAULTS 4
#define OLED_FAULT_SCROLL_MS 1500
#define OLED_BUTTON_DEBOUNCE_MS 250
#define OLED_IMU_STALE_MS 1000

#define OLED_NODE DT_CHOSEN(zephyr_display)
#define OLED_BUTTON_NODE DT_ALIAS(sw0)

BUILD_ASSERT(DT_NODE_HAS_STATUS(OLED_NODE, okay),
	     "zephyr,display must point to an enabled display");
BUILD_ASSERT(DT_NODE_HAS_STATUS(OLED_BUTTON_NODE, okay),
	     "sw0 alias must point to an enabled user button");

enum oled_mode {
	OLED_MODE_IMU,
	OLED_MODE_NET,
	OLED_MODE_FAULTS,
	OLED_MODE_BMS,
	OLED_MODE_COUNT,
};

struct fault_line {
	const char *text;
};

static const struct device *const oled_dev = DEVICE_DT_GET(OLED_NODE);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(OLED_BUTTON_NODE, gpios);
static K_THREAD_STACK_DEFINE(oled_thread_stack, OLED_THREAD_STACK_SIZE);
static struct k_thread oled_thread_data;
static struct gpio_callback button_cb;
static K_MUTEX_DEFINE(oled_lock);
static atomic_t oled_mode = ATOMIC_INIT(OLED_MODE_IMU);

static bool oled_ready;
static bool mode_button_ready;
static uint8_t chars_per_line = 16;
static uint8_t line_height = 16;

static void button_pressed(const struct device *dev,
			   struct gpio_callback *cb,
			   uint32_t pins)
{
	static int64_t last_press_ms;
	int64_t now_ms = k_uptime_get();
	atomic_val_t mode;

	ARG_UNUSED(dev);
	ARG_UNUSED(cb);

	if ((pins & BIT(button.pin)) == 0) {
		return;
	}

	if (now_ms - last_press_ms < OLED_BUTTON_DEBOUNCE_MS) {
		return;
	}

	last_press_ms = now_ms;
	mode = atomic_get(&oled_mode);
	atomic_set(&oled_mode, (mode + 1) % OLED_MODE_COUNT);
}

static int button_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("OLED mode button is not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure OLED mode button: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure OLED mode button interrupt: %d", ret);
		return ret;
	}

	gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
	ret = gpio_add_callback(button.port, &button_cb);
	if (ret < 0) {
		LOG_ERR("Failed to add OLED mode button callback: %d", ret);
		return ret;
	}

	return 0;
}

static int select_font(void)
{
	uint8_t font_width;
	uint8_t font_height;
	int font_count = cfb_get_numof_fonts(oled_dev);

	for (int idx = 0; idx < font_count; idx++) {
		if (cfb_get_font_size(oled_dev, idx, &font_width, &font_height) < 0) {
			continue;
		}

		if (font_height <= 16) {
			int ret = cfb_framebuffer_set_font(oled_dev, idx);

			if (ret < 0) {
				return ret;
			}

			line_height = MAX(font_height, 8);
			if (font_width > 0) {
				chars_per_line = MIN(MAX(OLED_WIDTH_PIXELS / font_width, 1),
						     OLED_LINE_MAX - 1);
			}
			return 0;
		}
	}

	return -ENOENT;
}

static size_t fault_text_len(const char *text)
{
	return text ? strlen(text) : 0;
}

static void copy_fault_chunk(char *dst, size_t dst_len, const char *text,
			     size_t offset)
{
	size_t len;

	if (!text || dst_len == 0) {
		return;
	}

	len = MIN(fault_text_len(text + offset), MIN((size_t)chars_per_line, dst_len - 1));
	memcpy(dst, text + offset, len);
	dst[len] = '\0';
}

static size_t fault_line_count(const char *text)
{
	size_t len = fault_text_len(text);

	if (len == 0) {
		return 1;
	}

	return (len + chars_per_line - 1) / chars_per_line;
}

static size_t collect_faults(struct fault_line *faults, size_t max_faults)
{
	size_t count = 0;

	if (!vn100s_has_recent_sample(OLED_IMU_STALE_MS) && count < max_faults) {
		faults[count++].text = "IMU stale/no valid sample";
	}

	if (!network_ready && count < max_faults) {
		faults[count++].text = "Network not ready";
	}

	if (!mode_button_ready && count < max_faults) {
		faults[count++].text = "Mode button disabled";
	}

	return count;
}

static size_t total_fault_lines(const struct fault_line *faults, size_t fault_count)
{
	size_t count = 0;

	for (size_t i = 0; i < fault_count; i++) {
		count += fault_line_count(faults[i].text);
	}

	return count;
}

static void get_fault_display_line(const struct fault_line *faults,
				   size_t fault_count,
				   size_t display_line,
				   char *dst,
				   size_t dst_len)
{
	if (dst_len == 0) {
		return;
	}

	dst[0] = '\0';

	for (size_t fault_idx = 0; fault_idx < fault_count; fault_idx++) {
		size_t lines = fault_line_count(faults[fault_idx].text);

		if (display_line < lines) {
			copy_fault_chunk(dst, dst_len, faults[fault_idx].text,
					 display_line * chars_per_line);
			return;
		}

		display_line -= lines;
	}
}

static void format_fault_page(char *line1, size_t line1_len,
			      char *line2, size_t line2_len)
{
	struct fault_line faults[OLED_MAX_FAULTS];
	size_t fault_count = collect_faults(faults, ARRAY_SIZE(faults));
	size_t line_count;
	size_t scroll_line;

	if (fault_count == 0) {
		snprintf(line1, line1_len, "Faults");
		snprintf(line2, line2_len, "none");
		return;
	}

	line_count = total_fault_lines(faults, fault_count);
	scroll_line = 0;
	if (line_count > 2) {
		scroll_line = (k_uptime_get() / OLED_FAULT_SCROLL_MS) % line_count;
	}

	get_fault_display_line(faults, fault_count, scroll_line, line1, line1_len);
	if (line_count > 1) {
		get_fault_display_line(faults, fault_count,
				       (scroll_line + 1) % line_count,
				       line2, line2_len);
	} else if (line2_len > 0) {
		line2[0] = '\0';
	}
}

static int oled_init(void)
{
	int ret;

	if (!device_is_ready(oled_dev)) {
		LOG_ERR("OLED display %s is not ready", oled_dev->name);
		return -ENODEV;
	}

	ret = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO10);
	if (ret < 0) {
		ret = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO01);
		if (ret < 0) {
			LOG_ERR("Failed to set OLED pixel format: %d", ret);
			return ret;
		}
	}

	ret = cfb_framebuffer_init(oled_dev);
	if (ret < 0) {
		LOG_ERR("Failed to initialize OLED framebuffer: %d", ret);
		return ret;
	}

	ret = select_font();
	if (ret < 0) {
		LOG_ERR("Failed to select OLED font: %d", ret);
		return ret;
	}

	ret = cfb_framebuffer_clear(oled_dev, true);
	if (ret < 0) {
		LOG_ERR("Failed to clear OLED framebuffer: %d", ret);
		return ret;
	}

	ret = display_blanking_off(oled_dev);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("Failed to unblank OLED display: %d", ret);
		return ret;
	}

	oled_ready = true;
	return display_print("K2 OLED", "starting");
}

int display_print(const char *line1, const char *line2)
{
	int ret;
	uint16_t second_line_y = MAX(16, line_height);

	if (!oled_ready) {
		return -ENODEV;
	}

	k_mutex_lock(&oled_lock, K_FOREVER);

	ret = cfb_framebuffer_clear(oled_dev, false);
	if (ret < 0) {
		goto out;
	}

	ret = cfb_print(oled_dev, line1 ? line1 : "", 0, 0);
	if (ret < 0) {
		goto out;
	}

	ret = cfb_print(oled_dev, line2 ? line2 : "", 0, second_line_y);
	if (ret < 0) {
		goto out;
	}

	ret = cfb_framebuffer_finalize(oled_dev);

out:
	k_mutex_unlock(&oled_lock);
	return ret;
}

static void oled_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	int ret = oled_init();
	if (ret < 0) {
		LOG_ERR("OLED disabled: %d", ret);
		return;
	}

	ret = button_init();
	if (ret < 0) {
		LOG_WRN("OLED mode button disabled: %d", ret);
		mode_button_ready = false;
	} else {
		mode_button_ready = true;
	}

	while (1) {
		float yaw;
		float pitch;
		float roll;
		char line1[OLED_LINE_MAX];
		char line2[OLED_LINE_MAX];

		switch (atomic_get(&oled_mode)) {
		case OLED_MODE_NET:
			snprintf(line1, sizeof(line1), "IP %s", STATIC_DEVICE_IP);
			snprintf(line2, sizeof(line2), "Top %s", TOPSIDE_IP);
			break;
		case OLED_MODE_FAULTS:
			format_fault_page(line1, sizeof(line1), line2, sizeof(line2));
			break;
		case OLED_MODE_BMS:
			snprintf(line1, sizeof(line1), "BMS");
			snprintf(line2, sizeof(line2), "pending");
			break;
		case OLED_MODE_IMU:
		default:
			vn100s_get_ypr(&yaw, &pitch, &roll);
			snprintf(line1, sizeof(line1), "Yaw %6.1f", (double)yaw);
			snprintf(line2, sizeof(line2), "Pit %5.1f Rol %5.1f",
				 (double)pitch, (double)roll);
			break;
		}

		ret = display_print(line1, line2);
		if (ret < 0) {
			LOG_WRN("OLED update failed: %d", ret);
		}

		k_msleep(OLED_REFRESH_MS);
	}
}

void display_start(void)
{
	k_tid_t thread_id = k_thread_create(&oled_thread_data,
					    oled_thread_stack,
					    K_THREAD_STACK_SIZEOF(oled_thread_stack),
					    oled_thread,
					    NULL, NULL, NULL,
					    OLED_THREAD_PRIORITY,
					    0,
					    K_NO_WAIT);

	if (thread_id) {
		k_thread_name_set(thread_id, "oled");
	} else {
		LOG_ERR("Failed to create OLED thread");
	}
}
