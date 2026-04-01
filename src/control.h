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
    uint8_t manipulator; /* Manipulator position (0-255) */
} rov_command_t;

/* Snapshot of control loop state for topside telemetry.
 * Axis order: [surge, sway, heave, roll, pitch, yaw] */
typedef struct {
    float setpoint[6];  /* Target value per axis */
    float output[6];    /* PID output [-1,+1] or passthrough */
    float error[6];     /* setpoint - measurement (0 when passthrough) */
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
