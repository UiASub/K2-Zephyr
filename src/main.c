/*
 * K2 Zephyr Application - LED Control
 * 
 * This application demonstrates basic embedded programming concepts:
 * 1. GPIO control (blinking LED)
 * 2. Real-time scheduling (periodic tasks)
 * 3. Logging system for debug output
 * 4. Networking (UDP server)
 * 
 * Target Hardware: ST NUCLEO-F767ZI development board
 * - Green LED on pin PA5 (controlled via GPIO)
 * - UART console for debug messages (115200 baud via ST-LINK USB)
 */

// Include Zephyr RTOS headers - these provide the APIs we need
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h> // added for zsock_close
#include <stdint.h>
#include "led.h"
#include "net.h"
#include "control.h"
#include "bitmask.h"

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

    led_init();
    rov_control_init();
    network_init();
    rov_control_start();
    udp_server_start();

    // --- Mock data: ramp values to show movement ---
    uint64_t bm = 0;
    uint8_t t = 0;

    LOG_INF("Starting main loop (mock bitmask generator)");

    while (1) {
        // simple pattern
        bm = 0;
        bm = bm_set_field(bm, BM_FREMBAK,   t);
        bm = bm_set_field(bm, BM_OPPNED,    255 - t);
        bm = bm_set_field(bm, BM_SIDESIDE,  (t + 32));
        bm = bm_set_field(bm, BM_PITCH,     (t >> 1));
        bm = bm_set_field(bm, BM_YAW,       (t << 1));
        bm = bm_set_field(bm, BM_ROLL,      (t ^ 0xAA));
        bm = bm_set_field(bm, BM_LYS,       (t & 1) ? 1 : 0);
        bm = bm_set_field(bm, BM_MANIP,     (t | 0x10));

        bm_set_current(bm); 

        t++;

        // status log every 10s like before, or comment out to keep logs quiet
        if (!network_ready) {
            LOG_INF("Network not ready, waiting...");
        }

        k_sleep(K_MSEC(500));
    }

    // never reached
    return 0;
}