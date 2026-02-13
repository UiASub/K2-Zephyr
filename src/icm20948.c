#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

#include "icm20948.h"

LOG_MODULE_REGISTER(icm20948, LOG_LEVEL_INF);

#define ICM_NODE DT_NODELABEL(icm20948)

BUILD_ASSERT(DT_NODE_HAS_STATUS(ICM_NODE, okay),
             "ICM20948 node not okay in DT");

static const struct spi_dt_spec icm = SPI_DT_SPEC_GET(
    ICM_NODE,
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    0
);

static int icm_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(reg & 0x7F), val };

    struct spi_buf buf = {
        .buf = tx,
        .len = sizeof(tx),
    };
    struct spi_buf_set tx_set = {
        .buffers = &buf,
        .count = 1,
    };

    return spi_write_dt(&icm, &tx_set);
}

static int icm_read_reg(uint8_t reg, uint8_t *val)
{
    uint8_t tx[2] = {
        (uint8_t)(reg | 0x80),
        0xFF,
    };
    uint8_t rx[2] = {0};

    struct spi_buf txb = {
        .buf = tx,
        .len = sizeof(tx),   // 2 bytes
    };
    struct spi_buf rxb = {
        .buf = rx,
        .len = sizeof(rx),   // 2 bytes
    };

    struct spi_buf_set tx_set = {
        .buffers = &txb,
        .count   = 1,
    };
    struct spi_buf_set rx_set = {
        .buffers = &rxb,
        .count   = 1,
    };

    int err = spi_transceive_dt(&icm, &tx_set, &rx_set);
    if (err) {
        return err;
    }

    *val = rx[1];
    return 0;
}


static int icm_read_burst(uint8_t reg, uint8_t *data, size_t len)
{
    /* We need 1 byte for the address + len data bytes */
    uint8_t tx_buf[1 + 12];   /* 12 is enough for your 6 accel + 6 gyro bytes */
    uint8_t rx_buf[1 + 12];

    if (len > 12) {
        return -EINVAL;
    }

    tx_buf[0] = (uint8_t)(reg | 0x80);  /* bit7=1 for read, start address */
    for (size_t i = 1; i < 1 + len; i++) {
        tx_buf[i] = 0xFF;              /* dummy bytes to clock out data */
    }

    struct spi_buf txb = {
        .buf = tx_buf,
        .len = 1 + len,
    };
    struct spi_buf rxb = {
        .buf = rx_buf,
        .len = 1 + len,
    };

    struct spi_buf_set tx_set = {
        .buffers = &txb,
        .count   = 1,
    };
    struct spi_buf_set rx_set = {
        .buffers = &rxb,
        .count   = 1,
    };

    int err = spi_transceive_dt(&icm, &tx_set, &rx_set);
    if (err) {
        return err;
    }

    /* Skip the first dummy byte, copy only the data bytes */
    memcpy(data, &rx_buf[1], len);
    return 0;
}


/* Public API */

int icm20948_init(struct icm20948_data *dev)
{
    int err;
    uint8_t who = 0;

    if (!device_is_ready(icm.bus)) {
        LOG_ERR("SPI bus not ready");
        return -ENODEV;
    }

    if (dev) {
        dev->spi = &icm;
    }

    LOG_DBG("Checking WHO_AM_I...");
    /* WHO_AM_I check */
    err = icm_read_reg(0x00, &who);
    if (err) {
        LOG_ERR("WHO_AM_I read err %d", err);
        return err;
    }

    LOG_DBG("WHO_AM_I = 0x%02X (expect 0xEA)", who);
    if (who != 0xEA) {
        LOG_WRN("Unexpected WHO_AM_I 0x%02X. Attempting reset anyway...", who);
    }

    /* --- Reset and wake (bank 0) --- */
    LOG_DBG("Resetting ICM20948...");
    err = icm_write_reg(0x06, 0x41);   /* PWR_MGMT_1: reset + CLKSEL=1 */
    if (err) {
        LOG_ERR("PWR_MGMT_1 reset err %d", err);
        return err;
    }
    k_msleep(100);

    err = icm_write_reg(0x06, 0x01);   /* PWR_MGMT_1: clk auto, not sleep */
    if (err) {
        LOG_ERR("PWR_MGMT_1 wake err %d", err);
        return err;
    }

    err = icm_write_reg(0x07, 0x00);   /* PWR_MGMT_2: all accel+gyro on */
    if (err) {
        LOG_ERR("PWR_MGMT_2 write err %d", err);
        return err;
    }

    /* Disable I2C, force SPI mode (USER_CTRL, bank 0, 0x03, bit4=I2C_IF_DIS) */
    err = icm_write_reg(0x03, 0x10);
    if (err) {
        LOG_ERR("USER_CTRL write err %d", err);
        return err;
    }

    /* Optional: configure accel/gyro full-scale in bank 2 (matches common examples)  */
    err = icm_write_reg(0x7F, 0x20);   /* REG_BANK_SEL: bank 2 */
    if (err) return err;

    err = icm_write_reg(0x14, 0x01);   /* ACCEL_CONFIG: +/-2 g */
    if (err) return err;

    err = icm_write_reg(0x15, 0x00);   /* ACCEL_CONFIG_2: no self-test */
    if (err) return err;

    err = icm_write_reg(0x01, 0x01);   /* GYRO_CONFIG: +/-250 dps */
    if (err) return err;

    err = icm_write_reg(0x02, 0x00);   /* GYRO_CONFIG_2: no self-test */
    if (err) return err;

    /* Back to bank 0 for WHO_AM_I and data */
    err = icm_write_reg(0x7F, 0x00);   /* REG_BANK_SEL: bank 0 */
    if (err) return err;

    k_msleep(50);

    return 0;
}

int icm20948_read_raw(struct icm20948_data *dev,
                      int16_t *ax, int16_t *ay, int16_t *az,
                      int16_t *gx, int16_t *gy, int16_t *gz)
{
    ARG_UNUSED(dev); /* we use static icm spec */

    uint8_t raw[12];
    int err = icm_read_burst(0x2D, raw, sizeof(raw)); /* accel+gyro start */
    if (err) {
        return err;
    }

    *ax = (int16_t)((raw[0] << 8) | raw[1]);
    *ay = (int16_t)((raw[2] << 8) | raw[3]);
    *az = (int16_t)((raw[4] << 8) | raw[5]);
    *gx = (int16_t)((raw[6] << 8) | raw[7]);
    *gy = (int16_t)((raw[8] << 8) | raw[9]);
    *gz = (int16_t)((raw[10] << 8) | raw[11]);

    return 0;
}

static int16_t last_ax, last_ay, last_az;
static int16_t last_gx, last_gy, last_gz;

void icm20948_get_latest(int16_t *accel, int16_t *gyro)
{
    accel[0] = last_ax;
    accel[1] = last_ay;
    accel[2] = last_az;
    gyro[0]  = last_gx;
    gyro[1]  = last_gy;
    gyro[2]  = last_gz;
}

/* Thread entry: print to serial forever */

void icm20948_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_DBG("ICM20948 thread starting");

    struct icm20948_data dev;
    int err = icm20948_init(&dev);
    if (err) {
        LOG_ERR("ICM20948 init failed: %d", err);
        return;
    }

    while (1) {
        int16_t ax, ay, az, gx, gy, gz;
        err = icm20948_read_raw(&dev, &ax, &ay, &az, &gx, &gy, &gz);
        if (!err) {
            /* Store for external access */
            last_ax = ax; last_ay = ay; last_az = az;
            last_gx = gx; last_gy = gy; last_gz = gz;
        } else {
            LOG_ERR("ICM read err %d", err);
        }
        k_msleep(200);
    }
}

/* Define and start the IMU thread automatically at boot */

K_THREAD_DEFINE(imu_tid, 2048, icm20948_task, NULL, NULL, NULL, 5, 0, 0);