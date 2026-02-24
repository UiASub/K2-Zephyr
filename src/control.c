#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include "control.h"
#include "led.h"
#include "vesc/thruster_mapping.h"
#include "vesc/vesc_uart_zephyr.h"

LOG_MODULE_REGISTER(rov_control, LOG_LEVEL_INF);

// Thread stack and data
K_THREAD_STACK_DEFINE(rov_control_stack, 2048);
static struct k_thread rov_control_thread_data;

// Message queue for receiving commands from network thread
K_MSGQ_DEFINE(rov_command_queue, sizeof(rov_command_t), 10, 4);

/* Current setpoints - updated by commands, sent periodically */
static struct {
    int8_t surge;
    int8_t sway;
    int8_t heave;
    int8_t roll;
    int8_t pitch;
    int8_t yaw;
    uint8_t light;
    uint8_t manipulator;
} current_setpoint = {0};

/* Mutex to protect setpoint access */
K_MUTEX_DEFINE(setpoint_mutex);

void rov_6dof_control(int8_t surge, int8_t sway, int8_t heave, 
                     int8_t roll, int8_t pitch, int8_t yaw)
{
    LOG_DBG("6DOF S:%+4d W:%+4d H:%+4d R:%+4d P:%+4d Y:%+4d",
            surge, sway, heave, roll, pitch, yaw);
    
    /* Calculate thruster outputs using mixing matrix and send to VESCs */
    thruster_output_t output;
    thruster_calculate_6dof(surge, sway, heave, roll, pitch, yaw, &output);
    
    /* Send outputs to all 8 VESCs (1 UART + 7 CAN) */
    thruster_send_outputs(&output);
}

static void rov_set_light(uint8_t brightness)
{
    if (brightness > 0) {
        LOG_DBG("Light: %d%% (%d/255)", (brightness * 100) / 255, brightness);
        // TODO: Control light PWM
    }
}

static void rov_set_manipulator(uint8_t position)
{
    if (position > 0) {
        LOG_DBG("Manipulator: %d", position);
        // TODO: Control manipulator servo
    }
}

/**
 * ROV control thread - sends commands at fixed 50Hz rate
 */
static void rov_control_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    
    rov_command_t command;
    int64_t next_send_time;
    
    LOG_DBG("ROV Control thread started");
    LOG_DBG("Waiting for 6DOF commands...");
    
    /* Calculate 50Hz period (20ms) */
    const int32_t period_ms = 20;  // 50Hz = 20ms period
    
    next_send_time = k_uptime_get();
    
    while (1) {
        /* Check for new commands (non-blocking) */
        while (k_msgq_get(&rov_command_queue, &command, K_NO_WAIT) == 0) {
            LOG_DBG("Processing ROV command #%u", command.sequence);
            LOG_DBG("CMD #%u: surge=%d sway=%d heave=%d", command.sequence, command.surge, command.sway, command.heave);
            
            LOG_DBG("Processing ROV command #%u", command.sequence);
            /* Update setpoints atomically */
            k_mutex_lock(&setpoint_mutex, K_FOREVER);
            current_setpoint.surge = command.surge;
            current_setpoint.sway = command.sway;
            current_setpoint.heave = command.heave;
            current_setpoint.roll = command.roll;
            current_setpoint.pitch = command.pitch;
            current_setpoint.yaw = command.yaw;
            current_setpoint.light = command.light;
            current_setpoint.manipulator = command.manipulator;
            k_mutex_unlock(&setpoint_mutex);
            
            gpio_pin_toggle_dt(&led);
        }
        
        /* Send current setpoints to VESCs at 20Hz */
        k_mutex_lock(&setpoint_mutex, K_FOREVER);
        rov_6dof_control(current_setpoint.surge, 
                        current_setpoint.sway, 
                        current_setpoint.heave,
                        current_setpoint.roll, 
                        current_setpoint.pitch, 
                        current_setpoint.yaw);
        
        if (current_setpoint.light > 0) {
            rov_set_light(current_setpoint.light);
        }
        
        if (current_setpoint.manipulator > 0) {
            rov_set_manipulator(current_setpoint.manipulator);
        }
        k_mutex_unlock(&setpoint_mutex);
        
        /* Sleep until next period */
        next_send_time += period_ms;
        k_sleep(K_TIMEOUT_ABS_MS(next_send_time));
    }
}

void rov_control_init(void)
{
    LOG_INF("ROV control init");
    LOG_DBG("Command queue capacity: 10 commands");
    LOG_INF("Initializing ROV 6DOF control system...");
    
    /* Initialize VESC UART */
    int ret = vesc_uart_init();
    if (ret < 0) {
        LOG_ERR("Failed to initialize VESC UART: %d", ret);
        return;
    }
    
    LOG_INF("ROV control system initialized (20Hz update rate)");
}

void rov_control_start(void)
{
    k_tid_t thread_id;
    
    thread_id = k_thread_create(&rov_control_thread_data,
                               rov_control_stack,
                               K_THREAD_STACK_SIZEOF(rov_control_stack),
                               rov_control_thread,
                               NULL, NULL, NULL,
                               K_PRIO_COOP(8),
                               0,
                               K_NO_WAIT);
    
    if (thread_id != NULL) {
        LOG_DBG("ROV control thread started");
    } else {
        LOG_ERR("Failed to start ROV control thread");
    }
}

void rov_send_command(uint32_t sequence, uint64_t payload)
{
    rov_command_t command;
    
    /* Debug: print raw payload bytes */
    LOG_INF("Raw payload: 0x%08X%08X", (uint32_t)(payload >> 32), (uint32_t)payload);
    
    command.sequence = sequence;
    command.surge = (int8_t)((payload >> 0) & 0xFF) - 128;
    command.sway = (int8_t)((payload >> 8) & 0xFF) - 128;
    command.heave = (int8_t)((payload >> 16) & 0xFF) - 128;
    command.roll = (int8_t)((payload >> 24) & 0xFF) - 128;
    command.pitch = (int8_t)((payload >> 32) & 0xFF) - 128;
    command.yaw = (int8_t)((payload >> 40) & 0xFF) - 128;
    command.light = (uint8_t)((payload >> 48) & 0xFF);
    command.manipulator = (uint8_t)((payload >> 56) & 0xFF);
    
    LOG_INF("Parsed: surge=%d sway=%d heave=%d yaw=%d", 
            command.surge, command.sway, command.heave, command.yaw);
    
    if (k_msgq_put(&rov_command_queue, &command, K_NO_WAIT) != 0) {
        LOG_WRN("ROV command queue full! Command #%u dropped", sequence);
    }
}