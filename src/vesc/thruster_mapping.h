#pragma once

#include <stdint.h>

/* CAN IDs for your 8 thrusters
 * T/B = Top/Bottom, L/R = Left/Right, F/B = Front/Back */
#define THRUSTER_TLF  0   // Top-Left-Front (UART local) CCW
#define THRUSTER_TLB  1   // Top-Left-Back (CAN) CW
#define THRUSTER_BLB  2   // Bottom-Left-Back (CAN) CCW
#define THRUSTER_BLF  3   // Bottom-Left-Front (CAN) CW
#define THRUSTER_BRF  4   // Bottom-Right-Front (CAN) CCW (reversed soldering)
#define THRUSTER_BRB  5   // Bottom-Right-Back (CAN) CW
#define THRUSTER_TRB  6   // Top-Right-Back (CAN) CCW
#define THRUSTER_TRF  7   // Top-Right-Front (CAN) CW

/* Thruster output structure */
typedef struct {
    float thruster[8];  // Duty cycles for all 8 thrusters (-1.0 to +1.0)
} thruster_output_t;

/**
 * @brief Calculate thruster outputs from 6DOF inputs
 * @param inputs Array of 6 floats in range -1.0 to +1.0
 *               [surge, sway, heave, roll, pitch, yaw]
 * @param output Pointer to thruster output structure
 */
void thruster_calculate_6dof(const float inputs[6], thruster_output_t *output);

/**
 * @brief Send thruster outputs to all VESCs
 * @param output Thruster output structure
 */
void thruster_send_outputs(const thruster_output_t *output);
