#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

#include "icm20948.h"

/* DeviceTree node for the IMU on SPI1 */
#define ICM_NODE DT_NODELABEL(icm20948)

BUILD_ASSERT(DT_NODE_HAS_STATUS(ICM_NODE, okay),
             "ICM20948 node not okay in DT");

static const struct spi_dt_spec icm = SPI_DT_SPEC_GET(
    ICM_NODE,
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, /* mode 0 */
    0
);

/* Simple helpers for SPI read/write */

static int icm_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(reg & 0x7F), val }; /* bit7=0 for write */

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
    uint8_t tx = (uint8_t)(reg | 0x80); /* bit7=1 for read */

    struct spi_buf txb = {
        .buf = &tx,
        .len = 1,
    };
    struct spi_buf rxb = {
        .buf = val,
        .len = 1,
    };

    struct spi_buf_set tx_set = {
        .buffers = &txb,
        .count = 1,
    };
    struct spi_buf_set rx_set = {
        .buffers = &rxb,
        .count = 1,
    };

    return spi_transceive_dt(&icm, &tx_set, &rx_set);
}

static int icm_read_burst(uint8_t reg, uint8_t *data, size_t len)
{
    uint8_t tx = (uint8_t)(reg | 0x80);

    struct spi_buf txb = {
        .buf = &tx,
        .len = 1,
    };
    struct spi_buf rxb = {
        .buf = data,
        .len = len,
    };

    struct spi_buf_set tx_set = {
        .buffers = &txb,
        .count = 1,
    };
    struct spi_buf_set rx_set = {
        .buffers = &rxb,
        .count = 1,
    };

    return spi_transceive_dt(&icm, &tx_set, &rx_set);
}

/* Public API */

int icm20948_init(struct icm20948_data *dev)
{
    int err;
    uint8_t who = 0;

    if (!device_is_ready(icm.bus)) {
        printk("ICM20948: SPI bus not ready\n");
        return -ENODEV;
    }

    /* store pointer in case you want it later */
    if (dev) {
        dev->spi = &icm;
    }

    /* Wake up the device (bank 0) */
    err = icm_write_reg(0x06, 0x01); /* PWR_MGMT_1: clk auto, not sleep */
    if (err) {
        printk("ICM20948: PWR_MGMT_1 write err %d\n", err);
        return err;
    }

    err = icm_write_reg(0x07, 0x00); /* PWR_MGMT_2: all sensors on */
    if (err) {
        printk("ICM20948: PWR_MGMT_2 write err %d\n", err);
        return err;
    }

    k_msleep(50);

    /* WHO_AM_I (bank 0, 0x00) should be 0xEA */
    err = icm_read_reg(0x00, &who);
    if (err) {
        printk("ICM20948: WHO_AM_I read err %d\n", err);
        return err;
    }

    printk("ICM20948: WHO_AM_I = 0x%02X (expect 0xEA)\n", who);
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

/* Thread entry: print to serial forever */

void icm20948_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("ICM20948 thread starting!\n");

    struct icm20948_data dev;
    int err = icm20948_init(&dev);
    if (err) {
        printk("ICM20948: init failed (%d)\n", err);
        return;
    }

    while (1) {
        int16_t ax, ay, az, gx, gy, gz;
        err = icm20948_read_raw(&dev, &ax, &ay, &az, &gx, &gy, &gz);
        if (!err) {
            printk("ICM A:%6d %6d %6d  G:%6d %6d %6d\n",
                   ax, ay, az, gx, gy, gz);
        } else {
            printk("ICM read err %d\n", err);
        }
        k_msleep(200);
    }
}

/* Define and start the IMU thread automatically at boot */

K_THREAD_DEFINE(imu_tid,
                2048,
                icm20948_task,
                NULL, NULL, NULL,
                5,
                0,
                0);

