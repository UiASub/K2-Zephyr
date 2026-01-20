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
#include <zephyr/drivers/uart.h>
#include <stdint.h>
#include "led.h"
#include "net.h"
#include "control.h"
#include "icm20948.h"
#include "vesc/vesc_protocol.h"
#include "vesc/vesc_uart_zephyr.h"

// Register this source file as a log module named "k2_app" with INFO level
// This allows us to use LOG_INF(), LOG_ERR(), etc. in our code
LOG_MODULE_REGISTER(k2_app, LOG_LEVEL_INF);

/* ============================================================
 * UART TEST MODE
 * Set to 1 to run UART test instead of normal operation
 * Set to 0 for normal ROV operation
 * ============================================================ */
#define UART_TEST_MODE 1

#if UART_TEST_MODE
/* Get USART6 device for VESC communication */
static const struct device *vesc_uart = DEVICE_DT_GET(DT_NODELABEL(usart6));

static void print_hex(const char *label, const uint8_t *buf, size_t len)
{
    printk("%s: ", label);
    for (size_t i = 0; i < len; i++) {
        printk("%02X ", buf[i]);
    }
    printk("\n");
}

static void uart_test(void)
{
    LOG_INF("=== UART TEST MODE (DUTY CYCLE) ===");
    
    if (!device_is_ready(vesc_uart)) {
        LOG_ERR("VESC UART device not ready!");
        return;
    }
    LOG_INF("VESC UART (usart6) initialized OK");
    LOG_INF("Using SET_DUTY at 50Hz");
    
    uint8_t tx_buf[32];
    uint8_t rx_buf[80];
    size_t tx_len, rx_len;
    uint32_t cycle = 0;
    float duty = 0.20f;  /* 20% duty cycle */
    
    while (1) {
        cycle++;
        
        /* ON phase: Run at duty% for 3 seconds (50Hz = 150 packets) */
        LOG_INF("=== Cycle %u: Motor ON at %.0f%% duty ===", cycle, duty * 100);
        for (int i = 0; i < 150; i++) {
            tx_len = vesc_build_set_duty(tx_buf, duty);
            vesc_uart_send(vesc_uart, tx_buf, tx_len);
            
            /* Print TX on first packet of cycle */      //0205050000c3503aa503
            if (i == 0) {
                print_hex("TX DUTY", tx_buf, tx_len);
            }
            
            /* Every 50th packet (~1 sec), request telemetry */
            if (i % 50 == 0) {
                k_sleep(K_MSEC(5));
                tx_len = vesc_build_get_values(tx_buf);
                vesc_uart_send(vesc_uart, tx_buf, tx_len);
                print_hex("TX GET_VALUES", tx_buf, tx_len);
                
                /* Try to read response */
                rx_len = vesc_uart_recv(vesc_uart, rx_buf, sizeof(rx_buf), 50);
                if (rx_len > 0) {
                    LOG_INF("VESC response: %d bytes", rx_len);
                    print_hex("RX", rx_buf, rx_len);
                } else {
                    LOG_INF("No response from VESC");
                }
            }
            k_sleep(K_MSEC(20));
        }
        
        /* OFF phase: 0% duty for 2 seconds */
        LOG_INF("=== Motor OFF (0%% duty) ===");
        for (int i = 0; i < 100; i++) {
            tx_len = vesc_build_set_duty(tx_buf, 0.0f);
            vesc_uart_send(vesc_uart, tx_buf, tx_len);
            if (i == 0) {
                print_hex("TX DUTY", tx_buf, tx_len);
            }
            k_sleep(K_MSEC(20));
        }
        
        LOG_INF("Cycle %u complete\n", cycle);
    }
}
#endif /* UART_TEST_MODE */

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
    
#if UART_TEST_MODE
    /* ============================================================
     * UART TEST MODE - Comment out when done testing!
     * ============================================================ */
    LOG_INF("*** RUNNING IN UART TEST MODE ***");
    led_init();
    uart_test();  // Never returns
    return 0;
    
#else
    /* 
     * INITIALIZATION PHASE
     * Set up all hardware and software components before main loop
     */
    
    // Initialize the LED GPIO pin
    led_init();

    // Initialize ROV control system
    rov_control_init();
    
    // Initialize networking
    network_init();

    // Start ROV control thread
    rov_control_start();
    
    // Start UDP server thread
    udp_server_start();

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
#endif /* UART_TEST_MODE */
}