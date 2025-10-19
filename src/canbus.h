#ifndef CANBUS_H
#define CANBUS_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <stdint.h>

/**
 * @brief Initialize the CAN bus at 500 kbps
 *
 * Sets CAN timing and starts the controller.
 * Must be called before any send_* functions.
 *
 * @return 0 on success, negative error code on failure
 */
int canbus_init(void);

/**
 * @brief Send a test CAN frame (legacy function)
 *
 * For initial verification of CAN wiring.
 *
 * @return 0 on success, negative error code on failure
 */
int send_test_frame(void);

/**
 * @brief Send an "alive" heartbeat to the VESC
 *
 * Should be sent every ~100 ms to prevent timeout.
 *
 * @param controller_id VESC CAN ID (1–255)
 * @return 0 on success, negative error code on failure
 */
int send_alive(int controller_id);

/**
 * @brief Send an RPM command over CAN
 *
 * @param controller_id VESC CAN ID (1–255)
 * @param rpm Target motor RPM
 * @return 0 on success, negative error code on failure
 */
int send_set_rpm(int controller_id, int32_t rpm);

/**
 * @brief Send a current command (torque control)
 *
 * Sends a float current (amps) to the VESC using the
 * COMM_SET_CURRENT (command 1) CAN frame.
 *
 * @param vesc_id VESC CAN ID (1–255)
 * @param amps Desired motor current (A)
 * @return 0 on success, negative error code on failure
 */
int send_set_current(uint8_t vesc_id, float amps);

/**
 * @brief Send a braking current command
 *
 * Sends a float current (amps) to brake the motor using
 * COMM_SET_CURRENT_BRAKE (command 2) CAN frame.
 *
 * @param vesc_id VESC CAN ID (1–255)
 * @param brake_amps Desired braking current (A)
 * @return 0 on success, negative error code on failure
 */
int send_set_current_brake(uint8_t vesc_id, float brake_amps);

#endif // CANBUS_H
