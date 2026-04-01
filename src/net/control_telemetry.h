#pragma once

#include <stdint.h>

/* Binary packet sent to topside at 10 Hz on CONTROL_TELEM_PORT.
 * Floats are native byte order (little-endian on both STM32 and x86). */
typedef struct {
    uint32_t sequence;      /* network byte order */
    float setpoint[6];      /* surge, sway, heave, roll, pitch, yaw */
    float output[6];        /* PID output [-1,+1] or passthrough */
    float error[6];         /* setpoint - measurement (0 when passthrough) */
    uint32_t crc32;         /* IEEE 802.3, network byte order */
} __attribute__((packed)) control_telem_packet_t;

/* Start the control telemetry sender thread */
void control_telemetry_start(void);
