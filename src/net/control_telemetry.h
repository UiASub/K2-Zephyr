#pragma once

#include <stdint.h>

/* Binary packet sent to topside at 10 Hz on CONTROL_TELEM_PORT.
 * Floats are native byte order (little-endian on both STM32 and x86). */
typedef struct {
    uint32_t sequence;            /* network byte order */
    uint32_t uptime_ms;           /* network byte order */
    uint32_t last_command_age_ms; /* network byte order */
    uint8_t flags;
    uint8_t override_mask;
    uint8_t pid_active_mask;
    int8_t pilot[6];              /* raw latest pilot command */
    uint8_t light;
    int8_t manipulator_command;
    float setpoint[6];            /* surge, sway, heave, roll, pitch, yaw */
    float measurement[6];         /* value PID is measuring */
    float output[6];              /* PID output [-1,+1] or passthrough */
    float error[6];               /* setpoint - measurement (0 when passthrough) */
    float gains[18];              /* kp,ki,kd for each axis */
    float manipulator_deg;  /* applied manipulator setpoint */
    uint16_t manipulator_pulse_us;
    uint32_t crc32;         /* IEEE 802.3, network byte order */
} __attribute__((packed)) control_telem_packet_t;

/* Start the control telemetry sender thread */
void control_telemetry_start(void);
