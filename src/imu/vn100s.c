#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <math.h>

#include "vn100s.h"

LOG_MODULE_REGISTER(vn100s, LOG_LEVEL_INF);

#define VN_NODE DT_NODELABEL(vn100s)

BUILD_ASSERT(DT_NODE_HAS_STATUS(VN_NODE, okay),
             "VN-100S node not okay in DT");

static const struct spi_dt_spec vn_spi = SPI_DT_SPEC_GET(
    VN_NODE,
    SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    0
);

/* VN-100S SPI binary protocol commands */
#define VN_CMD_READ  0x01

/* Register IDs */
#define VN_REG_MODEL       1    /* Model string (24 bytes ASCII) */
#define VN_REG_YPR_RATE_AC 239  /* YPR + rates + linear accel body (9x float32 = 36 bytes) */

/*
 * VN-100S SPI transaction:
 *   Request phase:  CS low -> [cmd, reg_id, 0x00, 0x00] -> CS high
 *   Wait:           >= 50 us for sensor to prepare response
 *   Response phase: CS low -> clock out (4 + payload_len) bytes -> CS high
 *   Response format: [0x00, cmd, reg_id, error_byte, ...payload...]
 */

static int vn_spi_read_reg(uint8_t reg_id, uint8_t *payload, size_t payload_len)
{
    int err;

    /* --- Request phase --- */
    uint8_t req[4] = { VN_CMD_READ, reg_id, 0x00, 0x00 };

    struct spi_buf req_buf = { .buf = req, .len = sizeof(req) };
    struct spi_buf_set req_set = { .buffers = &req_buf, .count = 1 };

    err = spi_write_dt(&vn_spi, &req_set);
    if (err) {
        LOG_ERR("VN SPI request write err %d", err);
        return err;
    }

    k_busy_wait(500);

    /* --- Response phase --- */
    size_t resp_len = 4 + payload_len;
    uint8_t tx_dummy[4 + 48];
    uint8_t rx[4 + 48];

    if (payload_len > 48) {
        return -EINVAL;
    }

    memset(tx_dummy, 0x00, resp_len);
    memset(rx, 0x00, resp_len);

    struct spi_buf tx_buf = { .buf = tx_dummy, .len = resp_len };
    struct spi_buf rx_buf = { .buf = rx, .len = resp_len };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    err = spi_transceive_dt(&vn_spi, &tx_set, &rx_set);
    if (err) {
        LOG_ERR("VN SPI response read err %d", err);
        return err;
    }

    uint8_t resp_err = rx[3];

    LOG_DBG("VN resp: [%02X %02X %02X %02X] for reg %d",
            rx[0], rx[1], rx[2], rx[3], reg_id);

    if (resp_err != 0x00) {
        LOG_ERR("VN sensor error 0x%02X for reg %d", resp_err, reg_id);
        return -EIO;
    }

    memcpy(payload, &rx[4], payload_len);
    return 0;
}

/* Public API */

int vn100s_init(struct vn100s_data *dev)
{
    if (!device_is_ready(vn_spi.bus)) {
        LOG_ERR("SPI bus not ready");
        return -ENODEV;
    }

    if (dev) {
        dev->spi = &vn_spi;
    }

    uint8_t model[24];
    memset(model, 0, sizeof(model));

    int err = vn_spi_read_reg(VN_REG_MODEL, model, sizeof(model));
    if (err) {
        LOG_ERR("Failed to read VN-100S model register: %d", err);
        return err;
    }

    LOG_INF("VN-100S model: %.24s", model);

    k_msleep(100);
    return 0;
}

/*
 * Register 239 payload (36 bytes, little-endian floats):
 *   [0..3]   yaw   (deg)
 *   [4..7]   pitch (deg)
 *   [8..11]  roll  (deg)
 *   [12..15] yaw rate   (deg/s)
 *   [16..19] pitch rate (deg/s)
 *   [20..23] roll rate  (deg/s)
 *   [24..27] linear accel X (m/s^2, gravity compensated)
 *   [28..31] linear accel Y (m/s^2, gravity compensated)
 *   [32..35] linear accel Z (m/s^2, gravity compensated)
 */
static int vn100s_read_all(float *yaw, float *pitch, float *roll,
                            float *yr, float *pr, float *rr,
                            float *ax, float *ay, float *az)
{
    uint8_t raw[36]; /* 9x float32 */
    int err = vn_spi_read_reg(VN_REG_YPR_RATE_AC, raw, sizeof(raw));
    if (err) {
        return err;
    }

    memcpy(yaw,   &raw[0],  sizeof(float));
    memcpy(pitch, &raw[4],  sizeof(float));
    memcpy(roll,  &raw[8],  sizeof(float));
    memcpy(yr,    &raw[12], sizeof(float));
    memcpy(pr,    &raw[16], sizeof(float));
    memcpy(rr,    &raw[20], sizeof(float));
    memcpy(ax,    &raw[24], sizeof(float));
    memcpy(ay,    &raw[28], sizeof(float));
    memcpy(az,    &raw[32], sizeof(float));

    return 0;
}

/* Reject NaN/Inf from SPI corruption — range checks are redundant
 * with the VN-100S onboard Kalman filter. */
static bool vn_sane(float yaw, float pitch, float roll,
                    float yr, float pr, float rr,
                    float ax, float ay, float az)
{
    return isfinite(yaw) && isfinite(pitch) && isfinite(roll) &&
           isfinite(yr)  && isfinite(pr)    && isfinite(rr)   &&
           isfinite(ax)  && isfinite(ay)    && isfinite(az);
}

static float last_yaw, last_pitch, last_roll;
static float last_yr, last_pr, last_rr;
static float last_ax, last_ay, last_az;

void vn100s_get_ypr(float *yaw, float *pitch, float *roll)
{
    *yaw   = last_yaw;
    *pitch = last_pitch;
    *roll  = last_roll;
}

void vn100s_get_rates(float *yaw_rate, float *pitch_rate, float *roll_rate)
{
    *yaw_rate   = last_yr;
    *pitch_rate = last_pr;
    *roll_rate  = last_rr;
}

void vn100s_get_accel(float *ax, float *ay, float *az)
{
    *ax = last_ax;
    *ay = last_ay;
    *az = last_az;
}

/* Thread entry */

void vn100s_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_DBG("VN-100S thread starting");

    struct vn100s_data dev;
    int err = vn100s_init(&dev);
    if (err) {
        LOG_ERR("VN-100S init failed: %d", err);
        return;
    }

    while (1) {
        float yaw, pitch, roll, yr, pr, rr, ax, ay, az;
        err = vn100s_read_all(&yaw, &pitch, &roll, &yr, &pr, &rr, &ax, &ay, &az);
        if (!err) {
            if (vn_sane(yaw, pitch, roll, yr, pr, rr, ax, ay, az)) {
                last_yaw   = yaw;
                last_pitch = pitch;
                last_roll  = roll;
                last_yr    = yr;
                last_pr    = pr;
                last_rr    = rr;
                last_ax    = ax;
                last_ay    = ay;
                last_az    = az;
            } else {
                LOG_WRN("VN-100S: corrupt sample "
                        "(y=%d p=%d r=%d)",
                        (int)yaw, (int)pitch, (int)roll);
            }
        } else {
            LOG_ERR("VN-100S read err %d", err);
        }
        k_msleep(50);
    }
}

K_THREAD_DEFINE(imu_tid, 2048, vn100s_task, NULL, NULL, NULL, 5, 0, 0);
