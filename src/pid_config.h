#pragma once

#include <zephyr/kernel.h>

/* PID gains for a single axis */
typedef struct {
    float kp;
    float ki;
    float kd;
} pid_gains_t;

/* Indices into the gains array — matches the 6 DOF of the ROV */
enum pid_axis {
    PID_SURGE = 0,
    PID_SWAY,
    PID_HEAVE,
    PID_ROLL,
    PID_PITCH,
    PID_YAW,
    PID_AXIS_COUNT  /* always last — equals 6 */
};

/* Start the PID config UDP listener thread */
void pid_config_start(void);

/* Get a snapshot of the gains for one axis (thread-safe) */
pid_gains_t pid_config_get_gains(enum pid_axis axis);
