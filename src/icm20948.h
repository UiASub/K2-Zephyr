#ifndef ICM20948_H
#define ICM20948_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>

struct icm20948_data {
    const struct spi_dt_spec *spi;
};

int icm20948_init(struct icm20948_data *dev);
int icm20948_read_raw(struct icm20948_data *dev,
                      int16_t *ax, int16_t *ay, int16_t *az,
                      int16_t *gx, int16_t *gy, int16_t *gz);

/* Thread entry for the IMU task */
void icm20948_task(void *p1, void *p2, void *p3);

#endif /* ICM20948_H */
