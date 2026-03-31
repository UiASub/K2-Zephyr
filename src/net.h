#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stddef.h>

/* Network addresses */
#define STATIC_DEVICE_IP   "192.168.1.100"
#define TOPSIDE_IP         "192.168.1.255"
#define STATIC_NETMASK     "255.255.255.0"
#define STATIC_GATEWAY     "192.168.1.1"
#define UDP_COMMAND_PORT   12345
#define TELEMETRY_UDP_PORT 12346
#define SENSOR_PORT        5002
#define PID_CONFIG_PORT    5003
#define AXIS_CONFIG_PORT   5004

extern bool network_ready;
extern int udp_sock;

void network_init(void);
void udp_server_start(void);

/* Shared CRC32 (IEEE 802.3) — used by net.c and resource_monitor.c */
uint32_t crc32_calc(const void *data, size_t length);