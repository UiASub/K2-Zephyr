#pragma once

#include <stdbool.h>

typedef struct {
    /* Gains */
    float kp;
    float ki;
    float kd;

    /* Output limits */
    float out_min;
    float out_max;

    /* State */
    float integral;
    float prev_measurement;
    bool  initialized;
} pid_controller_t;

/**
 * @brief Initialize / reset a PID controller
 */
void pid_init(pid_controller_t *pid, float kp, float ki, float kd,
              float out_min, float out_max);

/**
 * @brief Update gains without resetting state (for live tuning)
 */
void pid_set_gains(pid_controller_t *pid, float kp, float ki, float kd);

/**
 * @brief Reset integrator and derivative state
 */
void pid_reset(pid_controller_t *pid);

/**
 * @brief Compute one PID step
 *
 * Uses derivative-on-measurement to avoid derivative kick.
 *
 * @param pid        Controller state
 * @param setpoint   Desired value
 * @param measurement Current measured value
 * @param dt         Time step in seconds
 * @return           Control output (clamped to [out_min, out_max])
 */
float pid_compute(pid_controller_t *pid, float setpoint, float measurement, float dt);

/**
 * @brief Check if gains are all zero (controller disabled)
 */
static inline bool pid_is_disabled(const pid_controller_t *pid)
{
    return (pid->kp == 0.0f && pid->ki == 0.0f && pid->kd == 0.0f);
}
