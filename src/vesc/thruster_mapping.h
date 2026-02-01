#pragma once

#include <stdint.h>

/* CAN IDs for your 8 thrusters */
#define THRUSTER_FL_TOP     0   // Front-Left Top (UART local)
#define THRUSTER_FR_TOP     1   // Front-Right Top (CAN)
#define THRUSTER_BL_TOP     2 // Back-Left Top (CAN)
#define THRUSTER_BR_TOP     3 // Back-Right Top (CAN)
#define THRUSTER_FL_BOTTOM  4 // Front-Left Bottom (CAN)
#define THRUSTER_FR_BOTTOM  5 // Front-Right Bottom (CAN)
#define THRUSTER_BL_BOTTOM  6 // Back-Left Bottom (CAN)
#define THRUSTER_BR_BOTTOM  7 // Back-Right Bottom (CAN)

/* Thruster output structure */
typedef struct {
    float thruster[8];  // Duty cycles for all 8 thrusters (-1.0 to +1.0)
} thruster_output_t;

/**
 * @brief Calculate thruster outputs from 6DOF inputs
 * @param surge Forward/backward (-128 to +127)
 * @param sway Left/right (-128 to +127)
 * @param heave Up/down (-128 to +127)
 * @param roll Roll rotation (-128 to +127)
 * @param pitch Pitch rotation (-128 to +127)
 * @param yaw Yaw rotation (-128 to +127)
 * @param output Pointer to thruster output structure
 */
void thruster_calculate_6dof(int8_t surge, int8_t sway, int8_t heave,
                             int8_t roll, int8_t pitch, int8_t yaw,
                             thruster_output_t *output);

/**
 * @brief Send thruster outputs to all VESCs
 * @param output Thruster output structure
 */
void thruster_send_outputs(const thruster_output_t *output);
