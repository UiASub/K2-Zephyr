#pragma once

#include <zephyr/kernel.h>

extern bool network_ready;

void network_init(void);
void udp_server_thread(void *arg1, void *arg2, void *arg3);
void udp_server_start(void); // start the UDP server thread (creates it internally)

extern int udp_sock;