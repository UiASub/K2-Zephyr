#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>

/* Telemetry packet sent to topside.
 *
 * "ram" fields reflect aggregate thread-stack watermarks, NOT heap.
 * Zephyr has no single system-wide heap query, so this is the best
 * proxy for overall memory pressure.
 */
typedef struct {
    uint32_t sequence;
    uint32_t uptime_ms;
    uint8_t  cpu_usage_percent;  /* 0-100 */
    uint8_t  ram_used_percent;   /* 0-100  (stack watermarks / SRAM) */
    uint16_t ram_free_kb;        /* SRAM − stack watermarks */
    uint16_t ram_total_kb;       /* CONFIG_SRAM_SIZE */
    uint8_t  thread_count;
    uint8_t  reserved;
    uint32_t udp_rx_count;
    uint32_t udp_rx_errors;
    uint32_t crc32;
} __attribute__((packed)) telemetry_packet_t;

/* Start the resource monitor thread */
void resource_monitor_start(void);

/* Called from net.c on valid packet / error */
void resource_monitor_inc_udp_rx(void);
void resource_monitor_inc_udp_errors(void);
