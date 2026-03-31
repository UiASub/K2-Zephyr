#include "pid_controller.h"

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void pid_init(pid_controller_t *pid, float kp, float ki, float kd,
              float out_min, float out_max)
{
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->out_min = out_min;
    pid->out_max = out_max;
    pid->integral = 0.0f;
    pid->prev_measurement = 0.0f;
    pid->initialized = false;
}

void pid_set_gains(pid_controller_t *pid, float kp, float ki, float kd)
{
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void pid_reset(pid_controller_t *pid)
{
    pid->integral = 0.0f;
    pid->prev_measurement = 0.0f;
    pid->initialized = false;
}

float pid_compute(pid_controller_t *pid, float setpoint, float measurement, float dt)
{
    if (dt <= 0.0f) {
        return 0.0f;
    }

    float error = setpoint - measurement;

    /* Proportional */
    float p_term = pid->kp * error;

    /* Integral with clamping anti-windup */
    pid->integral += error * dt;
    pid->integral = clampf(pid->integral, pid->out_min / (pid->ki != 0.0f ? pid->ki : 1.0f),
                                          pid->out_max / (pid->ki != 0.0f ? pid->ki : 1.0f));
    float i_term = pid->ki * pid->integral;

    /* Derivative on measurement (avoids derivative kick on setpoint change) */
    float d_term = 0.0f;
    if (pid->initialized) {
        float d_measurement = (measurement - pid->prev_measurement) / dt;
        d_term = -pid->kd * d_measurement;
    }
    pid->prev_measurement = measurement;
    pid->initialized = true;

    /* Sum and clamp output */
    float output = p_term + i_term + d_term;
    return clampf(output, pid->out_min, pid->out_max);
}
