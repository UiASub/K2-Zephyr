#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>

/* Telemetry packet sent to topside */
typedef struct {
    uint32_t sequence;
    uint32_t uptime_ms;
    uint8_t  cpu_usage_percent;
    uint8_t  heap_used_percent;
    uint16_t heap_free_kb;
    uint16_t heap_total_kb;
    uint8_t  thread_count;
    uint8_t  reserved;
    uint32_t udp_rx_count;
    uint32_t udp_rx_errors;
    uint32_t crc32;
} __attribute__((packed)) telemetry_packet_t;

#define TELEMETRY_UDP_PORT 12346

void resource_monitor_init(void);
void resource_monitor_start(void);
void resource_monitor_set_topside(const char *ip_addr, uint16_t port);
void resource_monitor_get_telemetry(telemetry_packet_t *telemetry);
void resource_monitor_inc_udp_rx(void);
void resource_monitor_inc_udp_errors(void);
