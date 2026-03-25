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

/* Public functions */
void rov_control_init(void);
void rov_control_start(void);
void rov_send_command(uint32_t sequence, uint64_t payload);
