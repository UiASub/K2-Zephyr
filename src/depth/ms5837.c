#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#include "ms5837.h"

LOG_MODULE_REGISTER(ms5837, LOG_LEVEL_INF);

#define MS5837_GPIO_NODE DT_NODELABEL(gpiob)

BUILD_ASSERT(DT_NODE_HAS_STATUS(MS5837_GPIO_NODE, okay),
             "GPIOB node not okay in DT");

#define MS5837_SCL_PIN          8  /* D15 / PB8 */
#define MS5837_SDA_PIN          9  /* D14 / PB9 */
#define MS5837_I2C_HALF_PERIOD_US 50
#define MS5837_I2C_BUS_FREE_US    1000
#define MS5837_I2C_CLOCK_TIMEOUT_US 1000

#define MS5837_ADDR              0x76
#define MS5837_CMD_RESET         0x1E
#define MS5837_CMD_ADC_READ      0x00
#define MS5837_CMD_PROM_READ     0xA0
#define MS5837_CMD_CONVERT_D1    0x40
#define MS5837_CMD_CONVERT_D2    0x50
#define MS5837_OSR_8192          0x0A

#if defined(CONFIG_SOC_SERIES_STM32F7X)
#define MS5837_STARTUP_DELAY_MS  1000
#define MS5837_INIT_RETRY_MS     500
#else
#define MS5837_STARTUP_DELAY_MS  4000
#define MS5837_INIT_RETRY_MS     10000
#endif
#define MS5837_SAMPLE_PERIOD_MS  500
#define MS5837_CONV_DELAY_MS     20

#define WATER_DENSITY_KG_M3      1029.0f
#define GRAVITY_M_S2             9.80665f
#define MBAR_TO_PA               100.0f
#define MS5837_FW_DEPTH_REV      202605141u
#define MS5837_TRANSPORT_GPIO_BITBANG 1u
#define MS5837_TRANSPORT_MODE MS5837_TRANSPORT_GPIO_BITBANG
#define MS5837_PROM_REQUIRED_WORDS 7u

static uint8_t ms5837_addr = MS5837_ADDR;
static uint16_t prom[8];
static float surface_pressure_mbar;
static bool surface_pressure_set;
static bool bus_ready;
static uint32_t init_attempts;
static uint32_t read_errors;
static int scl_idle_level = -1;
static int sda_idle_level = -1;
static uint32_t transport_error_detail;

K_MUTEX_DEFINE(sample_mutex);
static ms5837_sample_t latest_sample;
static int64_t last_sample_time;
static const struct device *const gpio_dev = DEVICE_DT_GET(MS5837_GPIO_NODE);

static void i2c_delay(void)
{
    k_busy_wait(MS5837_I2C_HALF_PERIOD_US);
}

static void i2c_bus_free_delay(void)
{
    k_busy_wait(MS5837_I2C_BUS_FREE_US);
}

static int get_scl(void)
{
    return gpio_pin_get(gpio_dev, MS5837_SCL_PIN);
}

static int get_sda(void)
{
    return gpio_pin_get(gpio_dev, MS5837_SDA_PIN);
}

static int drive_scl_low(void)
{
    return gpio_pin_configure(gpio_dev, MS5837_SCL_PIN,
                              GPIO_OUTPUT_LOW | GPIO_OPEN_DRAIN | GPIO_PULL_UP);
}

static int drive_sda_low(void)
{
    return gpio_pin_configure(gpio_dev, MS5837_SDA_PIN,
                              GPIO_OUTPUT_LOW | GPIO_OPEN_DRAIN | GPIO_PULL_UP);
}

static int release_scl(void)
{
    int err = gpio_pin_configure(gpio_dev, MS5837_SCL_PIN,
                                 GPIO_INPUT | GPIO_PULL_UP);
    if (err) {
        return err;
    }

    for (uint32_t waited_us = 0; waited_us < MS5837_I2C_CLOCK_TIMEOUT_US;
         waited_us += 10) {
        if (get_scl() > 0) {
            return 0;
        }
        k_busy_wait(10);
    }

    return -ETIMEDOUT;
}

static int release_sda(void)
{
    return gpio_pin_configure(gpio_dev, MS5837_SDA_PIN,
                              GPIO_INPUT | GPIO_PULL_UP);
}

static int i2c_stop_condition(void);

static int i2c_bus_clear(void)
{
    int err = release_sda();
    if (err) {
        return err;
    }
    err = release_scl();
    if (err) {
        return err;
    }

    for (int i = 0; i < 9 && get_sda() == 0; i++) {
        err = drive_scl_low();
        if (err) {
            return err;
        }
        i2c_delay();
        err = release_scl();
        if (err) {
            return err;
        }
        i2c_delay();
    }

    if (get_sda() == 0) {
        return -EBUSY;
    }

    return i2c_stop_condition();
}

static int transport_init(void)
{
    if (bus_ready) {
        return 0;
    }

    if (!device_is_ready(gpio_dev)) {
        LOG_ERR("GPIOB not ready for MS5837 bitbang I2C");
        return -ENODEV;
    }

    int err = release_scl();
    if (err) {
        LOG_ERR("MS5837 SCL GPIO configure failed: %d", err);
        return err;
    }

    err = release_sda();
    if (err) {
        LOG_ERR("MS5837 SDA GPIO configure failed: %d", err);
        return err;
    }

    i2c_delay();
    scl_idle_level = get_scl();
    sda_idle_level = get_sda();

    err = i2c_bus_clear();
    if (err) {
        LOG_ERR("MS5837 bus clear failed: %d (SCL=%d SDA=%d)",
                err, get_scl(), get_sda());
        return err;
    }

    bus_ready = true;
    LOG_INF("MS5837 bitbang I2C ready on PB8/PB9 at about 10 kHz");
    return 0;
}

static int i2c_start_condition(void)
{
    int err = release_sda();
    if (err) {
        return err;
    }
    err = release_scl();
    if (err) {
        return err;
    }
    i2c_bus_free_delay();
    i2c_delay();

    err = drive_sda_low();
    if (err) {
        return err;
    }
    i2c_delay();

    err = drive_scl_low();
    if (err) {
        return err;
    }
    i2c_delay();
    return 0;
}

static int i2c_stop_condition(void)
{
    int err = drive_sda_low();
    if (err) {
        return err;
    }
    i2c_delay();

    err = release_scl();
    if (err) {
        return err;
    }
    i2c_delay();

    err = release_sda();
    if (err) {
        return err;
    }
    i2c_delay();
    i2c_bus_free_delay();
    return 0;
}

static int i2c_write_byte(uint8_t value)
{
    for (int bit = 7; bit >= 0; bit--) {
        int err = (value & BIT(bit)) ? release_sda() : drive_sda_low();
        if (err) {
            return err;
        }
        i2c_delay();

        err = release_scl();
        if (err) {
            return err;
        }
        i2c_delay();

        err = drive_scl_low();
        if (err) {
            return err;
        }
        i2c_delay();
    }

    int err = release_sda();
    if (err) {
        return err;
    }
    i2c_delay();

    err = release_scl();
    if (err) {
        return err;
    }
    i2c_delay();

    int ack = get_sda();
    err = drive_scl_low();
    if (err) {
        return err;
    }
    i2c_delay();

    return (ack == 0) ? 0 : -EIO;
}

static int i2c_read_byte(uint8_t *value, bool ack)
{
    uint8_t rx = 0;
    int err = release_sda();
    if (err) {
        return err;
    }

    for (int bit = 7; bit >= 0; bit--) {
        i2c_delay();
        err = release_scl();
        if (err) {
            return err;
        }
        i2c_delay();

        if (get_sda() > 0) {
            rx |= BIT(bit);
        }

        err = drive_scl_low();
        if (err) {
            return err;
        }
        i2c_delay();
    }

    err = ack ? drive_sda_low() : release_sda();
    if (err) {
        return err;
    }
    i2c_delay();

    err = release_scl();
    if (err) {
        return err;
    }
    i2c_delay();

    err = drive_scl_low();
    if (err) {
        return err;
    }
    err = release_sda();
    if (err) {
        return err;
    }
    i2c_delay();

    *value = rx;
    return 0;
}

static int transport_write(uint8_t addr, const uint8_t *tx, size_t len)
{
    transport_error_detail = 0;

    int err = i2c_start_condition();
    if (err) {
        return err;
    }

    err = i2c_write_byte((addr << 1) | 0);
    if (err) {
        transport_error_detail = (uint32_t)-err;
        (void)i2c_stop_condition();
        return err;
    }

    for (size_t i = 0; i < len; i++) {
        err = i2c_write_byte(tx[i]);
        if (err) {
            transport_error_detail = (uint32_t)-err;
            (void)i2c_stop_condition();
            return err;
        }
    }

    return i2c_stop_condition();
}

static int transport_read(uint8_t addr, uint8_t *rx, size_t len)
{
    transport_error_detail = 0;

    int err = i2c_start_condition();
    if (err) {
        return err;
    }

    err = i2c_write_byte((addr << 1) | 1);
    if (err) {
        transport_error_detail = (uint32_t)-err;
        (void)i2c_stop_condition();
        return err;
    }

    for (size_t i = 0; i < len; i++) {
        err = i2c_read_byte(&rx[i], i < (len - 1));
        if (err) {
            transport_error_detail = (uint32_t)-err;
            (void)i2c_stop_condition();
            return err;
        }
    }

    return i2c_stop_condition();
}

static int write_cmd(uint8_t cmd)
{
    return transport_write(ms5837_addr, &cmd, sizeof(cmd));
}

static int read_prom_word(uint8_t index, uint16_t *value)
{
    uint8_t cmd = MS5837_CMD_PROM_READ + (index * 2);
    uint8_t rx[2];
    int err = write_cmd(cmd);
    if (err) {
        return err;
    }

    err = transport_read(ms5837_addr, rx, sizeof(rx));
    if (err) {
        return err;
    }

    *value = ((uint16_t)rx[0] << 8) | rx[1];
    return 0;
}

static int read_adc(uint8_t convert_cmd, uint32_t *value)
{
    uint8_t rx[3];
    int err = write_cmd(convert_cmd | MS5837_OSR_8192);
    if (err) {
        return err;
    }

    k_msleep(MS5837_CONV_DELAY_MS);

    uint8_t cmd = MS5837_CMD_ADC_READ;
    err = write_cmd(cmd);
    if (err) {
        return err;
    }

    err = transport_read(ms5837_addr, rx, sizeof(rx));
    if (err) {
        return err;
    }

    *value = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];
    return 0;
}

static uint8_t crc4(uint16_t n_prom[8])
{
    uint16_t n_rem = 0;

    n_prom[0] &= 0x0FFF;
    n_prom[7] = 0;

    for (int cnt = 0; cnt < 16; cnt++) {
        if (cnt & 1) {
            n_rem ^= n_prom[cnt >> 1] & 0x00FF;
        } else {
            n_rem ^= n_prom[cnt >> 1] >> 8;
        }

        for (int bit = 8; bit > 0; bit--) {
            if (n_rem & 0x8000) {
                n_rem = (n_rem << 1) ^ 0x3000;
            } else {
                n_rem <<= 1;
            }
        }
    }

    return (n_rem >> 12) & 0x0F;
}

static bool prom_has_coefficients(void)
{
    bool any_nonzero = false;
    bool any_not_erased = false;

    for (uint8_t i = 1; i <= 6; i++) {
        any_nonzero |= prom[i] != 0x0000;
        any_not_erased |= prom[i] != 0xFFFF;
    }

    return any_nonzero && any_not_erased;
}

static void update_debug_fields(void)
{
    latest_sample.addr = ms5837_addr;
    latest_sample.scl_idle = scl_idle_level;
    latest_sample.sda_idle = sda_idle_level;
    latest_sample.transport_error = transport_error_detail;
    latest_sample.transport_mode = MS5837_TRANSPORT_MODE;
    latest_sample.fw_depth_rev = MS5837_FW_DEPTH_REV;
    latest_sample.init_attempts = init_attempts;
    latest_sample.read_errors = read_errors;
}

static void publish_status(int err, bool valid)
{
    k_mutex_lock(&sample_mutex, K_FOREVER);
    update_debug_fields();
    latest_sample.valid = valid;
    latest_sample.last_error = err;
    k_mutex_unlock(&sample_mutex);
}

int ms5837_init(void)
{
    int err = transport_init();
    if (err) {
        publish_status(err, false);
        return err;
    }

    init_attempts++;
    ms5837_addr = MS5837_ADDR;
    memset(prom, 0, sizeof(prom));
    publish_status(-ENODEV, false);

    err = write_cmd(MS5837_CMD_RESET);
    if (err) {
        LOG_WRN("MS5837 reset failed at 0x%02X: %d", ms5837_addr, err);
        publish_status(err, false);
        (void)i2c_bus_clear();
        return err;
    }
    k_msleep(10);

    for (uint8_t i = 0; i < MS5837_PROM_REQUIRED_WORDS; i++) {
        err = read_prom_word(i, &prom[i]);
        if (err) {
            LOG_WRN("MS5837 PROM read %u failed at 0x%02X: %d",
                    i, ms5837_addr, err);
            publish_status(err, false);
            return err;
        }
        publish_status(0, false);
    }

    err = read_prom_word(7, &prom[7]);
    if (err) {
        LOG_WRN("MS5837 optional PROM read 7 failed at 0x%02X: %d; continuing",
                ms5837_addr, err);
        prom[7] = 0;
        transport_error_detail = 0;
    }

    if (!prom_has_coefficients()) {
        LOG_WRN("MS5837 PROM coefficients blank at 0x%02X", ms5837_addr);
        publish_status(-EIO, false);
        return -EIO;
    }

    uint16_t crc_prom[8];
    memcpy(crc_prom, prom, sizeof(crc_prom));
    uint8_t prom_crc_expected = prom[0] >> 12;
    uint8_t prom_crc_calculated = crc4(crc_prom);
    publish_status(0, false);
    if (prom_crc_calculated != prom_crc_expected) {
        LOG_WRN("MS5837 PROM CRC mismatch at 0x%02X: calc=%u expected=%u",
                ms5837_addr, prom_crc_calculated, prom_crc_expected);
        publish_status(-EILSEQ, false);
        return -EILSEQ;
    }

    LOG_INF("MS5837-30BA ready on D14/D15 addr 0x%02X", ms5837_addr);
    publish_status(0, false);
    return 0;
}

static int read_compensated(float *pressure_mbar, float *temperature_c, float *depth_m)
{
    uint32_t d1_pressure;
    uint32_t d2_temperature;
    int err = read_adc(MS5837_CMD_CONVERT_D1, &d1_pressure);
    if (err) {
        return err;
    }

    err = read_adc(MS5837_CMD_CONVERT_D2, &d2_temperature);
    if (err) {
        return err;
    }

    int32_t dT = (int32_t)d2_temperature - ((int32_t)prom[5] * 256);
    int32_t temp = 2000 + (int64_t)dT * prom[6] / 8388608LL;
    int64_t off = (int64_t)prom[2] * 65536LL + ((int64_t)prom[4] * dT) / 128LL;
    int64_t sens = (int64_t)prom[1] * 32768LL + ((int64_t)prom[3] * dT) / 256LL;

    int64_t ti;
    int64_t offi;
    int64_t sensi;

    if (temp < 2000) {
        int64_t temp_delta = temp - 2000;
        ti = (3LL * (int64_t)dT * dT) / 8589934592LL;
        offi = (3LL * temp_delta * temp_delta) / 2LL;
        sensi = (5LL * temp_delta * temp_delta) / 8LL;

        if (temp < -1500) {
            int64_t very_low_delta = temp + 1500;
            offi += 7LL * very_low_delta * very_low_delta;
            sensi += 4LL * very_low_delta * very_low_delta;
        }
    } else {
        int64_t temp_delta = temp - 2000;
        ti = (2LL * (int64_t)dT * dT) / 137438953472LL;
        offi = (temp_delta * temp_delta) / 16LL;
        sensi = 0;
    }

    temp -= (int32_t)ti;
    off -= offi;
    sens -= sensi;

    int32_t pressure_x10_mbar =
        (((int64_t)d1_pressure * sens / 2097152LL) - off) / 8192LL;

    *pressure_mbar = (float)pressure_x10_mbar / 10.0f;
    *temperature_c = (float)temp / 100.0f;

    if (!surface_pressure_set) {
        surface_pressure_mbar = *pressure_mbar;
        surface_pressure_set = true;
        LOG_INF("MS5837 surface pressure set to %d.%01d mbar",
                (int)surface_pressure_mbar,
                (int)fabsf(surface_pressure_mbar * 10.0f) % 10);
    }

    float gauge_pa = (*pressure_mbar - surface_pressure_mbar) * MBAR_TO_PA;
    *depth_m = gauge_pa / (WATER_DENSITY_KG_M3 * GRAVITY_M_S2);
    if (*depth_m < 0.0f) {
        *depth_m = 0.0f;
    }

    return 0;
}

void ms5837_get_sample(ms5837_sample_t *sample)
{
    if (!sample) {
        return;
    }

    k_mutex_lock(&sample_mutex, K_FOREVER);
    *sample = latest_sample;
    if (last_sample_time != 0) {
        sample->age_ms = k_uptime_get() - last_sample_time;
    } else {
        sample->age_ms = -1;
    }
    k_mutex_unlock(&sample_mutex);
}

float ms5837_get_depth_m(void)
{
    ms5837_sample_t sample;
    ms5837_get_sample(&sample);
    return sample.valid ? sample.depth_m : 0.0f;
}

void ms5837_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    bool initialized = false;
    int err;

    k_msleep(MS5837_STARTUP_DELAY_MS);

    while (1) {
        if (!initialized) {
            err = ms5837_init();
            if (err) {
                k_mutex_lock(&sample_mutex, K_FOREVER);
                update_debug_fields();
                latest_sample.valid = false;
                latest_sample.age_ms = -1;
                latest_sample.last_error = err;
                k_mutex_unlock(&sample_mutex);

                LOG_ERR("MS5837 init failed: %d; retrying in %d s",
                        err, MS5837_INIT_RETRY_MS / 1000);
                k_msleep(MS5837_INIT_RETRY_MS);
                continue;
            }
            initialized = true;
        }

        float pressure_mbar;
        float temperature_c;
        float depth_m;
        err = read_compensated(&pressure_mbar, &temperature_c, &depth_m);
        if (!err && isfinite(pressure_mbar) && isfinite(temperature_c) && isfinite(depth_m)) {
            k_mutex_lock(&sample_mutex, K_FOREVER);
            latest_sample.depth_m = depth_m;
            latest_sample.pressure_mbar = pressure_mbar;
            latest_sample.temperature_c = temperature_c;
            latest_sample.valid = true;
            latest_sample.last_error = 0;
            update_debug_fields();
            last_sample_time = k_uptime_get();
            latest_sample.age_ms = 0;
            k_mutex_unlock(&sample_mutex);
        } else {
            read_errors++;
            LOG_ERR("MS5837 read failed: %d", err);
            k_mutex_lock(&sample_mutex, K_FOREVER);
            update_debug_fields();
            latest_sample.valid = false;
            latest_sample.last_error = err;
            k_mutex_unlock(&sample_mutex);
            initialized = false;
        }

        k_msleep(MS5837_SAMPLE_PERIOD_MS);
    }
}

K_THREAD_DEFINE(ms5837_tid, 2048, ms5837_task, NULL, NULL, NULL, 6, 0, 0);
