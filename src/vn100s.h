#ifndef VN100S_H
#define VN100S_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>

struct vn100s_data {
    const struct spi_dt_spec *spi;
};

int vn100s_init(struct vn100s_data *dev);

/* Get latest yaw/pitch/roll (degrees) */
void vn100s_get_ypr(float *yaw, float *pitch, float *roll);

/* Get latest angular rates (deg/s) */
void vn100s_get_rates(float *yaw_rate, float *pitch_rate, float *roll_rate);

/* Thread entry for the IMU task */
void vn100s_task(void *p1, void *p2, void *p3);

#endif /* VN100S_H */
