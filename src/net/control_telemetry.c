/*
 * Control Telemetry Sender — broadcasts setpoints, PID outputs, and errors
 * for all 6 DOF axes to topside at 10 Hz via UDP.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <string.h>

#include "control_telemetry.h"
#include "../control.h"
#include "net.h"

LOG_MODULE_REGISTER(ctrl_telem, LOG_LEVEL_INF);

#define SEND_INTERVAL_MS  100   /* 10 Hz */
#define STACK_SIZE        1536

K_THREAD_STACK_DEFINE(ctrl_telem_stack, STACK_SIZE);
static struct k_thread ctrl_telem_thread_data;

static uint32_t telem_seq;

static void ctrl_telem_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    while (!network_ready) {
        k_sleep(K_MSEC(100));
    }

    int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create control telemetry socket: %d", sock);
        return;
    }

    int on = 1;
    zsock_setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port   = htons(CONTROL_TELEM_PORT),
    };
    zsock_inet_pton(AF_INET, TOPSIDE_IP, &dest.sin_addr);

    LOG_INF("Control telemetry sender started (port %d, 10 Hz)", CONTROL_TELEM_PORT);

    while (1) {
        control_telemetry_t snap;
        control_get_telemetry(&snap);

        control_telem_packet_t pkt;
        pkt.sequence = htonl(telem_seq);
        memcpy(pkt.setpoint, snap.setpoint, sizeof(pkt.setpoint));
        memcpy(pkt.output, snap.output, sizeof(pkt.output));
        memcpy(pkt.error, snap.error, sizeof(pkt.error));

        size_t crc_len = sizeof(pkt) - sizeof(pkt.crc32);
        pkt.crc32 = htonl(crc32_calc(&pkt, crc_len));

        zsock_sendto(sock, &pkt, sizeof(pkt), 0,
                     (struct sockaddr *)&dest, sizeof(dest));
        telem_seq++;

        k_msleep(SEND_INTERVAL_MS);
    }
}

void control_telemetry_start(void)
{
    k_tid_t tid = k_thread_create(&ctrl_telem_thread_data,
                                   ctrl_telem_stack,
                                   K_THREAD_STACK_SIZEOF(ctrl_telem_stack),
                                   ctrl_telem_thread,
                                   NULL, NULL, NULL,
                                   9, 0, K_NO_WAIT);
    if (tid) {
        k_thread_name_set(tid, "ctrl_telem");
    }
}
