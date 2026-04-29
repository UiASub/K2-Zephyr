#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stddef.h>

/* Network addresses */
#define STATIC_DEVICE_IP   "10.77.0.2"
#define TOPSIDE_IP         "10.77.0.255"
#define STATIC_NETMASK     "255.255.255.0"
#define STATIC_GATEWAY     "0.0.0.0"
#define UDP_COMMAND_PORT   12345
#define TELEMETRY_UDP_PORT 12346
#define SENSOR_PORT        5002
#define PID_CONFIG_PORT    5003
#define AXIS_CONFIG_PORT   5004
#define CONTROL_TELEM_PORT 5005
#define LOG_UDP_PORT       5006
#define SETPOINT_OVR_PORT  5007
#define SYSTEM_CONTROL_PORT 5008

extern bool network_ready;
extern int udp_sock;

void network_init(void);
void udp_server_start(void);
void sensor_sender_start(void);

/* Shared CRC32 (IEEE 802.3) — used by net.c and resource_monitor.c */
uint32_t crc32_calc(const void *data, size_t length);
