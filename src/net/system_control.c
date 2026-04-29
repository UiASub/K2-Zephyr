/*
 * System Control — UDP listener for high-level MCU control commands.
 *
 * Listens on SYSTEM_CONTROL_PORT (5008) for reset packets:
 *   | magic "RST1" (4B) | sequence (u32 big-endian) | crc32 (u32 big-endian) |
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/reboot.h>
#include <string.h>

#include "net.h"
#include "resource_monitor.h"
#include "system_control.h"

LOG_MODULE_REGISTER(system_control, LOG_LEVEL_INF);

#define RESET_MAGIC "RST1"
#define STACK_SIZE 2048

typedef struct {
    char magic[4];
    uint32_t sequence;
    uint32_t crc32;
} __attribute__((packed)) reset_packet_t;

K_THREAD_STACK_DEFINE(system_control_stack, STACK_SIZE);
static struct k_thread system_control_thread_data;

static void system_control_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (!network_ready) {
        k_sleep(K_MSEC(100));
    }

    int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create system control socket: %d", sock);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(SYSTEM_CONTROL_PORT),
    };

    if (zsock_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("Failed to bind system control socket");
        zsock_close(sock);
        return;
    }

    LOG_INF("System control listener ready on port %d", SYSTEM_CONTROL_PORT);

    reset_packet_t pkt;
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);

    while (1) {
        int ret = zsock_recvfrom(sock, &pkt, sizeof(pkt), 0,
                                 (struct sockaddr *)&client, &client_len);

        if (ret != sizeof(pkt)) {
            if (ret < 0) {
                LOG_ERR("System control recv error: %d", ret);
                k_sleep(K_MSEC(100));
            } else {
                LOG_WRN("System control: wrong size %d (expected %d)",
                        ret, (int)sizeof(pkt));
            }
            continue;
        }

        uint32_t calc_crc = crc32_calc(&pkt, sizeof(pkt) - sizeof(pkt.crc32));
        uint32_t recv_crc = ntohl(pkt.crc32);
        if (calc_crc != recv_crc) {
            LOG_WRN("System control CRC mismatch");
            resource_monitor_inc_udp_errors();
            continue;
        }

        if (memcmp(pkt.magic, RESET_MAGIC, sizeof(pkt.magic)) != 0) {
            LOG_WRN("System control: unknown command");
            resource_monitor_inc_udp_errors();
            continue;
        }

        resource_monitor_inc_udp_rx();
        LOG_WRN("MCU reset requested by topside (seq #%u)", ntohl(pkt.sequence));
        k_msleep(100);
        sys_reboot(SYS_REBOOT_COLD);
    }
}

void system_control_start(void)
{
    k_tid_t tid = k_thread_create(&system_control_thread_data,
                                  system_control_stack,
                                  K_THREAD_STACK_SIZEOF(system_control_stack),
                                  system_control_thread,
                                  NULL, NULL, NULL,
                                  K_PRIO_COOP(7), 0, K_NO_WAIT);
    if (tid) {
        k_thread_name_set(tid, "system_control");
    }
}
