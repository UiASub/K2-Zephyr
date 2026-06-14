#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>

/* Message structure for communication between threads */
typedef struct {
    uint32_t sequence;
    int8_t surge;        /* Forward/backward (-128 to +127) */
    int8_t sway;         /* Left/right (-128 to +127) */
    int8_t heave;        /* Up/down (-128 to +127) */
    int8_t roll;         /* Roll rotation (-128 to +127) */
    int8_t pitch;        /* Pitch rotation (-128 to +127) */
    int8_t yaw;          /* Yaw rotation (-128 to +127) */
    uint8_t light;       /* Light brightness (0-255) */
    int8_t manipulator;  /* Manipulator setpoint (-128 to +127) */
} rov_command_t;

/* Control telemetry flags. */
#define CONTROL_TELEM_FLAG_TIMEOUT  (1U << 0)
#define CONTROL_TELEM_FLAG_OVERRIDE (1U << 1)
#define CONTROL_TELEM_FLAG_PID      (1U << 2)

/* Snapshot of control loop state for topside telemetry.
 * Axis order: [surge, sway, heave, roll, pitch, yaw] */
typedef struct {
    uint32_t uptime_ms;
    uint32_t last_command_age_ms;
    uint8_t flags;
    uint8_t override_mask;
    uint8_t pid_active_mask;
    int8_t pilot[6];    /* Latest raw pilot command per axis (-128..127) */
    uint8_t light;
    int8_t manipulator_command;
    float setpoint[6];  /* Target value per axis */
    float measurement[6]; /* Measurement used by PID */
    float output[6];    /* PID output [-1,+1] or passthrough */
    float error[6];     /* setpoint - measurement (0 when passthrough) */
    float gains[18];    /* kp,ki,kd for each axis */
    float manipulator_deg;
    uint16_t manipulator_pulse_us;
} control_telemetry_t;

/* Public functions */
void rov_control_init(void);
void rov_control_start(void);
void rov_send_command(uint32_t sequence, uint64_t payload);

/* Copy the latest control telemetry snapshot (thread-safe) */
void control_get_telemetry(control_telemetry_t *out);

/* Manual setpoint override from topside (for testing/debugging).
 * axis_mask: bitmask of axes to override (bit 0=surge … bit 5=yaw).
 * setpoints: target value per axis (only bits set in mask are used).
 * Units: surge/sway m/s, heave m, roll/pitch/yaw degrees. */
void control_set_override(uint8_t axis_mask, const float setpoints[6]);

/* Clear all overrides — return to normal stick control */
void control_clear_override(void);
