#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include "control.h"
#include "led.h"

LOG_MODULE_REGISTER(rov_control, LOG_LEVEL_INF);

// Thread stack and data
K_THREAD_STACK_DEFINE(rov_control_stack, 2048);
static struct k_thread rov_control_thread_data;

// Message queue for receiving commands from network thread
K_MSGQ_DEFINE(rov_command_queue, sizeof(rov_command_t), 10, 4);

/**
 * 6DOF ROV control function - perfect for matrix calculations
 * @param surge: Forward/backward movement (-128 to +127)
 * @param sway: Left/right movement (-128 to +127) 
 * @param heave: Up/down movement (-128 to +127)
 * @param roll: Roll rotation (-128 to +127)
 * @param pitch: Pitch rotation (-128 to +127)
 * @param yaw: Yaw rotation (-128 to +127)
 */
void rov_6dof_control(int8_t surge, int8_t sway, int8_t heave, 
                     int8_t roll, int8_t pitch, int8_t yaw)
{
    LOG_DBG("6DOF S:%+4d W:%+4d H:%+4d R:%+4d P:%+4d Y:%+4d",
            surge, sway, heave, roll, pitch, yaw);
    
    // TODO: Apply your matrix calculations here
    // Example: thruster_output = thruster_matrix * [surge, sway, heave, roll, pitch, yaw]
}

/**
 * Control light brightness
 */
static void rov_set_light(uint8_t brightness)
{
    if (brightness > 0) {
        LOG_DBG("Light: %d%% (%d/255)", (brightness * 100) / 255, brightness);
        // TODO: Control light PWM
    }
}

/**
 * Control manipulator position
 */
static void rov_set_manipulator(uint8_t position)
{
    if (position > 0) {
        LOG_DBG("Manipulator: %d", position);
        // TODO: Control manipulator servo
    }
}

/**
 * ROV control thread - processes incoming commands from queue
 */
static void rov_control_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    
    rov_command_t command;
    
    LOG_DBG("ROV Control thread started");
    LOG_DBG("Waiting for 6DOF commands...");
    
    while (1) {
        // Wait for a command from the network thread
        if (k_msgq_get(&rov_command_queue, &command, K_FOREVER) == 0) {
            
            LOG_DBG("Processing ROV command #%u", command.sequence);
            
            // Call 6DOF function with parsed values
            rov_6dof_control(command.surge, command.sway, command.heave,
                           command.roll, command.pitch, command.yaw);
            
            // Handle auxiliary controls
            if (command.light > 0) {
                rov_set_light(command.light);
            }
            
            if (command.manipulator > 0) {
                rov_set_manipulator(command.manipulator);
            }
            
            // Visual feedback
            gpio_pin_toggle_dt(&led);
            
            // Small delay to prevent overwhelming the system
            k_sleep(K_MSEC(10));
        }
    }
}

/**
 * Initialize ROV control system
 */
void rov_control_init(void)
{
    LOG_INF("ROV control init");
    LOG_DBG("Command queue capacity: 10 commands");
    
    // TODO: Initialize hardware components here
    // Examples:
    // - PWM controllers for thrusters
    // - Servo controllers for manipulator
    // - LED PWM for lights
    
    LOG_INF("ROV control system initialized");
}

/**
 * Start ROV control thread
 */
void rov_control_start(void)
{
    k_tid_t thread_id;
    
    thread_id = k_thread_create(&rov_control_thread_data,
                               rov_control_stack,
                               K_THREAD_STACK_SIZEOF(rov_control_stack),
                               rov_control_thread,
                               NULL, NULL, NULL,
                               K_PRIO_COOP(8),  // Lower priority than network
                               0,
                               K_NO_WAIT);
    
    if (thread_id != NULL) {
        LOG_DBG("ROV control thread started");
    } else {
        LOG_ERR("Failed to start ROV control thread");
    }
}

/**
 * Send a command to the ROV control thread
 * @param sequence: Command sequence number
 * @param payload: 64-bit payload containing control data
 */
void rov_send_command(uint32_t sequence, uint64_t payload)
{
    rov_command_t command;
    
    // Parse the payload and convert to signed ranges
    command.sequence = sequence;
    command.surge = (int8_t)((payload >> 0) & 0xFF) - 128;   // Bits 0-7
    command.sway = (int8_t)((payload >> 8) & 0xFF) - 128;    // Bits 8-15
    command.heave = (int8_t)((payload >> 16) & 0xFF) - 128;  // Bits 16-23
    command.roll = (int8_t)((payload >> 24) & 0xFF) - 128;   // Bits 24-31
    command.pitch = (int8_t)((payload >> 32) & 0xFF) - 128;  // Bits 32-39
    command.yaw = (int8_t)((payload >> 40) & 0xFF) - 128;    // Bits 40-47
    command.light = (uint8_t)((payload >> 48) & 0xFF);       // Bits 48-55
    command.manipulator = (uint8_t)((payload >> 56) & 0xFF); // Bits 56-63
    
    // Send command to ROV control thread
    if (k_msgq_put(&rov_command_queue, &command, K_NO_WAIT) != 0) {
        LOG_WRN("ROV command queue full! Command #%u dropped", sequence);
    } else {
        LOG_DBG("6DOF command #%u queued", sequence);
    }
}