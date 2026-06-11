#include "depth_sensor_ms5837.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(depth_sensor, LOG_LEVEL_INF);

#define DEPTH_I2C_GPIO_NODE DT_NODELABEL(gpiob)

BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DEPTH_I2C_GPIO_NODE),
             "GPIOB is not okay in devicetree");

#define MS5837_ADDR_PRIMARY 0x76
#define MS5837_ADDR_SECONDARY 0x77

#define BITBANG_I2C_HZ 10000
#define I2C_HALF_PERIOD_US (1000000 / (BITBANG_I2C_HZ * 2))

#define RESET_DELAY_MS 50
#define PROM_WORD_DELAY_MS 10
#define ADDRESS_RETRY_DELAY_MS 250
#define DETECT_RETRY_DELAY_MS 1000
#define READ_TURNAROUND_US 2000

#define MS5837_CMD_RESET 0x1e
#define MS5837_CMD_ADC_READ 0x00
#define MS5837_CMD_CONVERT_D1_OSR8192 0x4a
#define MS5837_CMD_CONVERT_D2_OSR8192 0x5a
#define MS5837_CMD_PROM_READ_BASE 0xa0

#define CONVERSION_DELAY_MS 20
#define SAMPLE_PERIOD_MS 100
#define SAMPLE_STALE_MS 1000
#define WATER_DENSITY_KG_M3 1029
#define GRAVITY_UM_S2 9806650LL
#define PROM_READ_WORDS 7
#define SAMPLE_READ_RETRIES 4
#define SAMPLE_RETRY_DELAY_MS 20

#define SCL_PIN 8
#define SDA_PIN 9
#define I2C_GPIO_FLAGS (GPIO_OPEN_DRAIN | GPIO_PULL_UP)

struct ms5837_raw_sample {
    uint32_t raw_pressure;
    uint32_t raw_temperature;
    int32_t temperature_centi_c;
    int32_t pressure_deci_mbar;
};

static const struct device *const i2c_gpio =
    DEVICE_DT_GET(DEPTH_I2C_GPIO_NODE);

static uint8_t sensor_addr;
static uint16_t prom[8];
static int32_t surface_pressure_deci_mbar;
static const char *last_i2c_error_stage = "none";

static depth_sensor_sample_t latest_sample;
static bool latest_valid;
K_MUTEX_DEFINE(depth_sample_mutex);

K_THREAD_STACK_DEFINE(depth_sensor_stack, 3072);
static struct k_thread depth_sensor_thread_data;
static bool depth_sensor_thread_started;

static void i2c_delay(void)
{
    k_busy_wait(I2C_HALF_PERIOD_US);
}

static void scl_set(int value)
{
    gpio_pin_set(i2c_gpio, SCL_PIN, value);
    i2c_delay();
}

static void sda_set(int value)
{
    gpio_pin_set(i2c_gpio, SDA_PIN, value);
    i2c_delay();
}

static int sda_read(void)
{
    return gpio_pin_get(i2c_gpio, SDA_PIN);
}

static void bb_stop(void);

static void bb_recover_bus(void)
{
    sda_set(1);
    scl_set(1);

    for (uint8_t i = 0; i < 9 && sda_read() == 0; i++) {
        scl_set(0);
        scl_set(1);
    }

    bb_stop();
}

static void bb_start(void)
{
    sda_set(1);
    scl_set(1);
    sda_set(0);
    scl_set(0);
}

static void bb_stop(void)
{
    sda_set(0);
    scl_set(1);
    sda_set(1);
}

static void bb_write_bit(bool bit)
{
    sda_set(bit ? 1 : 0);
    scl_set(1);
    scl_set(0);
}

static bool bb_read_bit(void)
{
    bool bit;

    sda_set(1);
    scl_set(1);
    bit = sda_read() > 0;
    scl_set(0);

    return bit;
}

static int bb_write_byte(uint8_t byte)
{
    for (int bit = 7; bit >= 0; bit--) {
        bb_write_bit((byte >> bit) & 1U);
    }

    return bb_read_bit() ? -EIO : 0;
}

static uint8_t bb_read_byte(bool ack)
{
    uint8_t byte = 0;

    for (int bit = 7; bit >= 0; bit--) {
        if (bb_read_bit()) {
            byte |= 1U << bit;
        }
    }

    bb_write_bit(!ack);
    return byte;
}

static int bb_addr(uint8_t addr, bool read)
{
    return bb_write_byte((addr << 1) | (read ? 1U : 0U));
}

static int bb_write(uint8_t addr, const uint8_t *buf, uint8_t len)
{
    int ret;

    bb_recover_bus();
    bb_start();
    ret = bb_addr(addr, false);
    if (ret < 0) {
        bb_stop();
        return ret;
    }

    for (uint8_t i = 0; i < len; i++) {
        ret = bb_write_byte(buf[i]);
        if (ret < 0) {
            bb_stop();
            return ret;
        }
    }

    bb_stop();
    return 0;
}

static int bb_write_read(uint8_t addr,
                         const uint8_t *tx,
                         uint8_t tx_len,
                         uint8_t *rx,
                         uint8_t rx_len)
{
    int ret;

    bb_recover_bus();
    bb_start();
    ret = bb_addr(addr, false);
    if (ret < 0) {
        last_i2c_error_stage = "write address";
        bb_stop();
        return ret;
    }

    for (uint8_t i = 0; i < tx_len; i++) {
        ret = bb_write_byte(tx[i]);
        if (ret < 0) {
            last_i2c_error_stage = "write data";
            bb_stop();
            return ret;
        }
    }

    bb_stop();
    k_busy_wait(READ_TURNAROUND_US);

    bb_start();
    ret = bb_addr(addr, true);
    if (ret < 0) {
        last_i2c_error_stage = "read address";
        bb_stop();
        return ret;
    }

    for (uint8_t i = 0; i < rx_len; i++) {
        rx[i] = bb_read_byte(i + 1U < rx_len);
    }

    bb_stop();
    last_i2c_error_stage = "none";
    return 0;
}

static int write_cmd(uint8_t cmd)
{
    return bb_write(sensor_addr, &cmd, 1);
}

static int read_prom_word(uint8_t index, uint16_t *word)
{
    uint8_t cmd = MS5837_CMD_PROM_READ_BASE + (index * 2U);
    uint8_t raw[2];
    int ret = bb_write_read(sensor_addr, &cmd, 1, raw, sizeof(raw));

    if (ret < 0) {
        LOG_WRN("PROM C%u cmd 0x%02x failed during %s: %d",
                index, cmd, last_i2c_error_stage, ret);
        return ret;
    }

    *word = ((uint16_t)raw[0] << 8) | raw[1];
    return 0;
}

static int read_adc(uint32_t *value)
{
    uint8_t cmd = MS5837_CMD_ADC_READ;
    uint8_t raw[3];
    int ret = bb_write_read(sensor_addr, &cmd, 1, raw, sizeof(raw));

    if (ret < 0) {
        return ret;
    }

    *value = ((uint32_t)raw[0] << 16) | ((uint32_t)raw[1] << 8) | raw[2];
    return 0;
}

static uint8_t crc4(uint16_t coeffs[8])
{
    uint16_t n_rem = 0;
    uint16_t saved0 = coeffs[0];
    uint16_t saved7 = coeffs[7];

    coeffs[0] &= 0x0fff;
    coeffs[7] = 0;

    for (uint8_t cnt = 0; cnt < 16; cnt++) {
        if (cnt & 1U) {
            n_rem ^= coeffs[cnt >> 1] & 0x00ff;
        } else {
            n_rem ^= coeffs[cnt >> 1] >> 8;
        }

        for (uint8_t bit = 0; bit < 8; bit++) {
            if (n_rem & 0x8000) {
                n_rem = (n_rem << 1) ^ 0x3000;
            } else {
                n_rem <<= 1;
            }
        }
    }

    coeffs[0] = saved0;
    coeffs[7] = saved7;

    return (n_rem >> 12) & 0x0f;
}

static bool prom_crc_ok(void)
{
    uint16_t prom_copy[8];

    memcpy(prom_copy, prom, sizeof(prom_copy));
    return crc4(prom_copy) == ((prom[0] >> 12) & 0x0f);
}

static int reset_sensor(void)
{
    int ret = write_cmd(MS5837_CMD_RESET);

    if (ret < 0) {
        return ret;
    }

    k_sleep(K_MSEC(RESET_DELAY_MS));
    return 0;
}

static int read_prom(void)
{
    prom[7] = 0;

    for (uint8_t i = 0; i < PROM_READ_WORDS; i++) {
        int ret = read_prom_word(i, &prom[i]);

        if (ret < 0) {
            return ret;
        }

        k_sleep(K_MSEC(PROM_WORD_DELAY_MS));
    }

    return 0;
}

static int try_detect_addr(uint8_t addr)
{
    sensor_addr = addr;

    int ret = reset_sensor();
    if (ret < 0) {
        return ret;
    }

    ret = read_prom();
    if (ret < 0) {
        return ret;
    }

    if (prom[1] == 0 || prom[2] == 0 || prom[5] == 0 || prom[6] == 0) {
        return -ENODEV;
    }

    if (!prom_crc_ok()) {
        return -EBADMSG;
    }

    return 0;
}

static int detect_sensor(void)
{
    int ret = try_detect_addr(MS5837_ADDR_PRIMARY);

    if (ret == 0) {
        return 0;
    }

    k_sleep(K_MSEC(ADDRESS_RETRY_DELAY_MS));
    return try_detect_addr(MS5837_ADDR_SECONDARY);
}

static int convert_and_read(uint8_t cmd, uint32_t *value)
{
    int ret = write_cmd(cmd);

    if (ret < 0) {
        return ret;
    }

    k_sleep(K_MSEC(CONVERSION_DELAY_MS));
    return read_adc(value);
}

static void calculate_sample(struct ms5837_raw_sample *sample)
{
    int64_t dT = (int64_t)sample->raw_temperature - ((int64_t)prom[5] * 256LL);
    int64_t temp = 2000LL + ((dT * (int64_t)prom[6]) / 8388608LL);
    int64_t off = ((int64_t)prom[2] * 65536LL) + (((int64_t)prom[4] * dT) / 128LL);
    int64_t sens = ((int64_t)prom[1] * 32768LL) + (((int64_t)prom[3] * dT) / 256LL);

    int64_t temp_i = 0;
    int64_t off_i = 0;
    int64_t sens_i = 0;

    if (temp < 2000LL) {
        int64_t t = temp - 2000LL;

        temp_i = (3LL * dT * dT) / 8589934592LL;
        off_i = (3LL * t * t) / 2LL;
        sens_i = (5LL * t * t) / 8LL;

        if (temp < -1500LL) {
            int64_t cold = temp + 1500LL;

            off_i += 7LL * cold * cold;
            sens_i += 4LL * cold * cold;
        }
    } else {
        int64_t t = temp - 2000LL;

        temp_i = (2LL * dT * dT) / 137438953472LL;
        off_i = (t * t) / 16LL;
    }

    temp -= temp_i;
    off -= off_i;
    sens -= sens_i;

    sample->temperature_centi_c = (int32_t)temp;
    sample->pressure_deci_mbar =
        (int32_t)(((((int64_t)sample->raw_pressure * sens) / 2097152LL) - off) / 8192LL);
}

static int read_sample(struct ms5837_raw_sample *sample)
{
    int ret = 0;

    for (uint8_t attempt = 0; attempt < SAMPLE_READ_RETRIES; attempt++) {
        ret = convert_and_read(MS5837_CMD_CONVERT_D1_OSR8192,
                               &sample->raw_pressure);

        if (ret == 0) {
            ret = convert_and_read(MS5837_CMD_CONVERT_D2_OSR8192,
                                   &sample->raw_temperature);
        }

        if (ret == 0 &&
            sample->raw_pressure >= 1000000U &&
            sample->raw_pressure <= 16000000U &&
            sample->raw_temperature >= 1000000U &&
            sample->raw_temperature <= 16000000U) {
            calculate_sample(sample);

            if (sample->temperature_centi_c >= -4000 &&
                sample->temperature_centi_c <= 10000 &&
                sample->pressure_deci_mbar >= 3000 &&
                sample->pressure_deci_mbar <= 400000) {
                return 0;
            }

            ret = -ERANGE;
        } else if (ret == 0) {
            ret = -ERANGE;
        }

        bb_recover_bus();
        k_sleep(K_MSEC(SAMPLE_RETRY_DELAY_MS));
    }

    return ret;
}

static int64_t depth_mm_from_pressure(int32_t pressure_deci_mbar)
{
    int64_t delta_deci_mbar =
        (int64_t)pressure_deci_mbar - surface_pressure_deci_mbar;

    return (delta_deci_mbar * 10000LL * 1000000LL) /
           ((int64_t)WATER_DENSITY_KG_M3 * GRAVITY_UM_S2);
}

static void publish_sample(const struct ms5837_raw_sample *raw)
{
    int64_t depth_mm = depth_mm_from_pressure(raw->pressure_deci_mbar);

    depth_sensor_sample_t sample = {
        .depth_m = (float)depth_mm / 1000.0f,
        .pressure_mbar = (float)raw->pressure_deci_mbar / 10.0f,
        .temperature_c = (float)raw->temperature_centi_c / 100.0f,
        .raw_pressure = raw->raw_pressure,
        .raw_temperature = raw->raw_temperature,
        .timestamp_ms = k_uptime_get(),
    };

    k_mutex_lock(&depth_sample_mutex, K_FOREVER);
    latest_sample = sample;
    latest_valid = true;
    k_mutex_unlock(&depth_sample_mutex);
}

static int configure_gpio(void)
{
    if (!device_is_ready(i2c_gpio)) {
        return -ENODEV;
    }

    int ret = gpio_pin_configure(i2c_gpio, SCL_PIN,
                                 GPIO_OUTPUT_HIGH | I2C_GPIO_FLAGS);
    if (ret < 0) {
        return ret;
    }

    ret = gpio_pin_configure(i2c_gpio, SDA_PIN,
                             GPIO_OUTPUT_HIGH | I2C_GPIO_FLAGS);
    if (ret < 0) {
        return ret;
    }

    sda_set(1);
    scl_set(1);
    return 0;
}

static void depth_sensor_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    int ret = configure_gpio();
    if (ret < 0) {
        LOG_ERR("GPIO init failed for PB8/PB9 depth I2C: %d", ret);
        return;
    }

    LOG_INF("MS5837 depth sensor bit-banged on PB8=SCL PB9=SDA at %u Hz",
            BITBANG_I2C_HZ);

    while (true) {
        ret = detect_sensor();
        if (ret == 0) {
            break;
        }

        k_mutex_lock(&depth_sample_mutex, K_FOREVER);
        latest_valid = false;
        k_mutex_unlock(&depth_sample_mutex);

        LOG_WRN("MS5837 not detected at 0x%02x/0x%02x: %d",
                MS5837_ADDR_PRIMARY, MS5837_ADDR_SECONDARY, ret);
        k_sleep(K_MSEC(DETECT_RETRY_DELAY_MS));
    }

    LOG_INF("MS5837 detected at I2C address 0x%02x", sensor_addr);

    struct ms5837_raw_sample raw;
    while (read_sample(&raw) < 0) {
        LOG_WRN("Waiting for first valid MS5837 sample");
        k_sleep(K_MSEC(DETECT_RETRY_DELAY_MS));
    }

    surface_pressure_deci_mbar = raw.pressure_deci_mbar;
    publish_sample(&raw);

    LOG_INF("MS5837 depth zero reference: %d.%01d mbar",
            surface_pressure_deci_mbar / 10,
            surface_pressure_deci_mbar < 0 ?
                -(surface_pressure_deci_mbar % 10) :
                surface_pressure_deci_mbar % 10);

    uint32_t ok_count = 0;
    uint32_t fail_count = 0;

    while (true) {
        ret = read_sample(&raw);
        if (ret == 0) {
            publish_sample(&raw);
            ok_count++;

            if ((ok_count % 50U) == 0U) {
                depth_sensor_sample_t sample;
                bool ok = depth_sensor_get_latest(&sample);
                if (ok) {
                    LOG_INF("Depth %.3f m, pressure %.1f mbar, temp %.2f C",
                            (double)sample.depth_m,
                            (double)sample.pressure_mbar,
                            (double)sample.temperature_c);
                }
            }
        } else {
            fail_count++;
            if (fail_count == 1U || (fail_count % 20U) == 0U) {
                LOG_WRN("MS5837 sample read failed: %d", ret);
            }
        }

        k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
    }
}

void depth_sensor_start(void)
{
    if (depth_sensor_thread_started) {
        return;
    }

    k_tid_t tid = k_thread_create(&depth_sensor_thread_data,
                                  depth_sensor_stack,
                                  K_THREAD_STACK_SIZEOF(depth_sensor_stack),
                                  depth_sensor_thread,
                                  NULL, NULL, NULL,
                                  8, 0, K_NO_WAIT);
    if (tid != NULL) {
        depth_sensor_thread_started = true;
        k_thread_name_set(tid, "depth_sensor");
    } else {
        LOG_ERR("Failed to create depth sensor thread");
    }
}

bool depth_sensor_get_latest(depth_sensor_sample_t *out)
{
    depth_sensor_sample_t sample;
    bool valid;

    k_mutex_lock(&depth_sample_mutex, K_FOREVER);
    sample = latest_sample;
    valid = latest_valid;
    k_mutex_unlock(&depth_sample_mutex);

    if (!valid || (k_uptime_get() - sample.timestamp_ms) > SAMPLE_STALE_MS) {
        return false;
    }

    if (out != NULL) {
        *out = sample;
    }

    return true;
}

bool depth_sensor_get_depth_m(float *depth_m)
{
    depth_sensor_sample_t sample;

    if (!depth_sensor_get_latest(&sample)) {
        return false;
    }

    if (depth_m != NULL) {
        *depth_m = sample.depth_m;
    }

    return true;
}
