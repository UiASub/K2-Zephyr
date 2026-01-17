#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>

/* Telemetry packet sent to topside */
typedef struct {
    uint32_t sequence;          /* Packet sequence number */
    uint32_t uptime_ms;         /* System uptime in milliseconds */
    uint8_t  cpu_usage_percent; /* CPU usage 0-100% */
    uint8_t  heap_used_percent; /* Heap memory used 0-100% */
    uint16_t heap_free_kb;      /* Free heap in KB */
    uint16_t heap_total_kb;     /* Total heap in KB */
    uint8_t  thread_count;      /* Number of active threads */
    uint8_t  reserved;          /* Padding for alignment */
    uint32_t udp_rx_count;      /* UDP packets received */
    uint32_t udp_rx_errors;     /* UDP receive errors */
    uint32_t crc32;             /* CRC32 checksum */
} __attribute__((packed)) telemetry_packet_t;

/* Thread info for detailed monitoring (optional extended packet) */
typedef struct {
    char     name[16];          /* Thread name */
    uint32_t stack_size;        /* Total stack size */
    uint32_t stack_used;        /* Stack used (high water mark) */
    uint8_t  stack_percent;     /* Stack usage percentage */
    uint8_t  priority;          /* Thread priority */
    uint8_t  state;             /* Thread state */
    uint8_t  reserved;          /* Padding */
} __attribute__((packed)) thread_info_t;

/* Telemetry port (different from command port) */
#define TELEMETRY_UDP_PORT 12346

/* Initialize the resource monitor */
void resource_monitor_init(void);

/* Start the resource monitor thread */
void resource_monitor_start(void);

/* Set the topside address for telemetry */
void resource_monitor_set_topside(const char *ip_addr, uint16_t port);

/* Get current telemetry data (for local use) */
void resource_monitor_get_telemetry(telemetry_packet_t *telemetry);

/* Increment UDP counters (called from net.c) */
void resource_monitor_inc_udp_rx(void);
void resource_monitor_inc_udp_errors(void);
