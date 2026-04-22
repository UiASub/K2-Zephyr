/*
 * K2 Zephyr Application - LED Control
 * 
 * This application demonstrates basic embedded programming concepts:
 * 1. GPIO control (blinking LED)
 * 2. Real-time scheduling (periodic tasks)
 * 3. Logging system for debug output
 * 4. Networking (UDP server)
 * 
 * Target Hardware: ST NUCLEO-F767ZI or NUCLEO-H755ZI-Q development board
 * - UART console for debug messages (115200 baud via ST-LINK USB)
 */

// Include Zephyr RTOS headers - these provide the APIs we need
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h> // added for zsock_close
#include <zephyr/drivers/uart.h>
#include <stdint.h>
#include "net/net.h"
#include "control.h"
#include "imu/vn100s.h"
#include "net/resource_monitor.h"
#include "vesc/vesc_protocol.h"
#include "vesc/vesc_uart_zephyr.h"
#include "pid/pid_config.h"
#include "imu/axis_config.h"
#include "net/control_telemetry.h"
#include "net/setpoint_override.h"
#include "net/ota_confirm.h"

/* Defined in net/log_backend_udp.c */
void log_backend_udp_topside_start(void);

// Register this source file as a log module named "k2_app" with INFO level
// This allows us to use LOG_INF(), LOG_ERR(), etc. in our code
LOG_MODULE_REGISTER(k2_app, LOG_LEVEL_INF);

/*
 * Main Application Entry Point
 * 
 * In embedded systems, main() typically:
 * 1. Initializes hardware and subsystems
 * 2. Sets up any required callbacks or interrupts  
 * 3. Enters an infinite loop to handle the main application logic
 * 
 * Unlike desktop programs that exit when done, embedded applications
 * run continuously until power is removed.
 */
int main(void)
{
    LOG_INF("=== K2 Zephyr Application Starting ===");
    LOG_INF("Board: %s", CONFIG_BOARD);
    
    /* 
     * INITIALIZATION PHASE
     * Set up all hardware and software components before main loop
     */
    
    // Initialize ROV control system
    rov_control_init();

    // Initialize networking
    network_init();

    // Activate UDP log backend (must be after network_init)
    log_backend_udp_topside_start();

    // Start ROV control thread
    rov_control_start();

    // Start resource monitor thread
    resource_monitor_start();

    // Start UDP server thread
    udp_server_start();

    // Start PID config listener
    pid_config_start();

    // Start axis config listener
    axis_config_start();

    // Start control telemetry sender
    control_telemetry_start();

    // Start setpoint override listener
    setpoint_override_start();

    // Confirm a trial MCUboot image only after the app and network come up
    ota_confirm_init();

    /*
     * MAIN APPLICATION LOOP
     * 
     * Monitor network status and UDP server operation
     * No LED blinking - UDP server now processes structured packets silently
     */

    //uint32_t loop_count = 0; // Count main loop iterations
    
    LOG_INF("Starting main loop");
    LOG_DBG("UDP server will validate structured packets (sequence + payload + CRC32)");
    LOG_DBG("Payload will be forwarded to ROV control system");
    
    while (1) {  // Infinite loop - runs forever
        
        // Increment counter and log current state
        //loop_count++;
        
        if (network_ready) {
            LOG_DBG("Network ready, UDP server processing packets");
        } else {
            LOG_DBG("Network not ready, waiting...");
        }

        // Sleep for 10 seconds (longer interval for status updates)
        k_sleep(K_SECONDS(10));
    }
    
    // Cleanup (never reached in embedded systems)
    if (udp_sock >= 0) {
        zsock_close(udp_sock);
    }
    return 0;
}
