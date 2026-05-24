/*
 * Frame Control — UDP listener for reference-frame lock commands.
 *
 * Listens on FRAME_CONTROL_PORT (5009) for packets:
 *   | magic "FRM1" (4B) | command (1B) | reserved (3B)
 *   | sequence (u32 big-endian) | crc32 (u32 big-endian) |
 *
 * Commands:
 *   0x01 = lock current attitude as the world-up reference
 *   0x02 = unlock reference-frame compensation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <string.h>

#include "../control.h"
#include "frame_control.h"
#include "net.h"
#include "resource_monitor.h"

LOG_MODULE_REGISTER(frame_control, LOG_LEVEL_INF);

#define FRAME_MAGIC "FRM1"
#define FRAME_CMD_LOCK   0x01
#define FRAME_CMD_UNLOCK 0x02
#define STACK_SIZE 2048

typedef struct {
    char magic[4];
    uint8_t command;
    uint8_t reserved[3];
    uint32_t sequence;
    uint32_t crc32;
} __attribute__((packed)) frame_packet_t;

K_THREAD_STACK_DEFINE(frame_control_stack, STACK_SIZE);
static struct k_thread frame_control_thread_data;

static void frame_control_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (!network_ready) {
        k_sleep(K_MSEC(100));
    }

    int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create frame control socket: %d", sock);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(FRAME_CONTROL_PORT),
    };

    if (zsock_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("Failed to bind frame control socket");
        zsock_close(sock);
        return;
    }

    LOG_INF("Frame control listener ready on port %d", FRAME_CONTROL_PORT);

    frame_packet_t pkt;
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);

    while (1) {
        int ret = zsock_recvfrom(sock, &pkt, sizeof(pkt), 0,
                                 (struct sockaddr *)&client, &client_len);

        if (ret != sizeof(pkt)) {
            if (ret < 0) {
                LOG_ERR("Frame control recv error: %d", ret);
                k_sleep(K_MSEC(100));
            } else {
                LOG_WRN("Frame control: wrong size %d (expected %d)",
                        ret, (int)sizeof(pkt));
                resource_monitor_inc_udp_errors();
            }
            continue;
        }

        uint32_t calc_crc = crc32_calc(&pkt, sizeof(pkt) - sizeof(pkt.crc32));
        uint32_t recv_crc = ntohl(pkt.crc32);
        if (calc_crc != recv_crc) {
            LOG_WRN("Frame control CRC mismatch");
            resource_monitor_inc_udp_errors();
            continue;
        }

        if (memcmp(pkt.magic, FRAME_MAGIC, sizeof(pkt.magic)) != 0) {
            LOG_WRN("Frame control: unknown magic");
            resource_monitor_inc_udp_errors();
            continue;
        }

        switch (pkt.command) {
        case FRAME_CMD_LOCK:
            control_frame_lock_enable();
            resource_monitor_inc_udp_rx();
            LOG_INF("Frame lock requested by topside (seq #%u)", ntohl(pkt.sequence));
            break;
        case FRAME_CMD_UNLOCK:
            control_frame_lock_disable();
            resource_monitor_inc_udp_rx();
            LOG_INF("Frame unlock requested by topside (seq #%u)", ntohl(pkt.sequence));
            break;
        default:
            LOG_WRN("Frame control: unknown command 0x%02X", pkt.command);
            resource_monitor_inc_udp_errors();
            break;
        }
    }
}

void frame_control_start(void)
{
    k_tid_t tid = k_thread_create(&frame_control_thread_data,
                                  frame_control_stack,
                                  K_THREAD_STACK_SIZEOF(frame_control_stack),
                                  frame_control_thread,
                                  NULL, NULL, NULL,
                                  K_PRIO_COOP(7), 0, K_NO_WAIT);
    if (tid) {
        k_thread_name_set(tid, "frame_control");
    }
}
