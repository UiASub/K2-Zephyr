#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(k2_led, LOG_LEVEL_INF);

const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

void led_init(void)
{
    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("LED GPIO device not ready");
        return;
    }
    
    int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure LED GPIO: %d", ret);
    } else {
        gpio_pin_set_dt(&led, 0);
        LOG_INF("LED initialized successfully on pin PA5");
    }
}
