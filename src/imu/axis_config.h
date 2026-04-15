#pragma once

#include <zephyr/kernel.h>

/* Axis source indices for YPR */
enum axis_src {
    AXIS_SRC_YAW   = 0,
    AXIS_SRC_PITCH = 1,
    AXIS_SRC_ROLL  = 2,
};

/* Axis source indices for accelerometer */
enum accel_src {
    ACCEL_SRC_X = 0,
    ACCEL_SRC_Y = 1,
    ACCEL_SRC_Z = 2,
};

/* Mapping for a single axis: which sensor axis it reads and its sign */
typedef struct {
    uint8_t src;   /* source index */
    int8_t  sign;  /* +1 or -1 */
} axis_map_t;

/* IMU offset from center of mass (mm) */
typedef struct {
    float x;
    float y;
    float z;
} imu_offset_t;

/* Start the axis config UDP listener thread */
void axis_config_start(void);

/*
 * Apply the current axis remapping to raw sensor yaw/pitch/roll.
 * Thread-safe.
 */
void axis_config_remap_ypr(float raw_yaw, float raw_pitch, float raw_roll,
                           float *out_yaw, float *out_pitch, float *out_roll);

/*
 * Apply the current axis remapping to raw accelerometer x/y/z.
 * Thread-safe.
 */
void axis_config_remap_accel(float raw_ax, float raw_ay, float raw_az,
                             float *out_ax, float *out_ay, float *out_az);

/*
 * Get the current IMU offset from center of mass (mm).
 * Thread-safe.
 */
imu_offset_t axis_config_get_offset(void);
