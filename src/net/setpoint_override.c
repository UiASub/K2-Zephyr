/*
 * Setpoint Override — UDP listener for manually setting axis setpoints
 * from topside for testing and debugging.
 *
 * Listens on SETPOINT_OVR_PORT (5007) for packets containing:
 *   | type (1B) | axis_mask (1B) | setpoints[6] (24B) | crc32 (4B) |
 *
 * Type 0x01 = SET: apply the override (axes with bits set in mask use the
 *                   provided setpoint; other axes are unaffected).
 * Type 0x02 = CLEAR: remove all overrides, return to normal stick control.
 *
 * Units: surge/sway in m/s, heave in m, roll/pitch/yaw in degrees.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <string.h>

#include "setpoint_override.h"
#include "../control.h"
#include "net.h"

LOG_MODULE_REGISTER(sp_override, LOG_LEVEL_INF);

#define SP_OVR_SET   0x01
#define SP_OVR_CLEAR 0x02

typedef struct {
    uint8_t type;           /* SP_OVR_SET or SP_OVR_CLEAR */
    uint8_t axis_mask;      /* bit 0=surge, 1=sway, 2=heave, 3=roll, 4=pitch, 5=yaw */
    float   setpoint[6];    /* target per axis (native byte order) */
    uint32_t crc32;         /* IEEE 802.3 over all preceding bytes */
} __attribute__((packed)) sp_ovr_packet_t;

#define STACK_SIZE 2048

K_THREAD_STACK_DEFINE(sp_ovr_stack, STACK_SIZE);
static struct k_thread sp_ovr_thread_data;

static const char *axis_names[6] = {
    "surge", "sway", "heave", "roll", "pitch", "yaw"
};

static void sp_ovr_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    while (!network_ready) {
        k_sleep(K_MSEC(100));
    }

    int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create setpoint override socket: %d", sock);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(SETPOINT_OVR_PORT),
    };

    if (zsock_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("Failed to bind setpoint override socket");
        zsock_close(sock);
        return;
    }

    LOG_INF("Setpoint override listener ready on port %d", SETPOINT_OVR_PORT);

    sp_ovr_packet_t pkt;
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);

    while (1) {
        int ret = zsock_recvfrom(sock, &pkt, sizeof(pkt), 0,
                                 (struct sockaddr *)&client, &client_len);

        if (ret != sizeof(pkt)) {
            if (ret < 0) {
                LOG_ERR("Setpoint override recv error: %d", ret);
                k_sleep(K_MSEC(100));
            } else {
                LOG_WRN("Setpoint override: wrong size %d (expected %d)",
                        ret, (int)sizeof(pkt));
            }
            continue;
        }

        /* Validate CRC */
        size_t crc_len = sizeof(pkt) - sizeof(pkt.crc32);
        uint32_t calc_crc = crc32_calc(&pkt, crc_len);
        if (calc_crc != pkt.crc32) {
            LOG_WRN("Setpoint override CRC mismatch");
            continue;
        }

        if (pkt.type == SP_OVR_SET) {
            float setpoints[6];
            memcpy(setpoints, pkt.setpoint, sizeof(setpoints));
            control_set_override(pkt.axis_mask, setpoints);
            for (int i = 0; i < 6; i++) {
                if (pkt.axis_mask & (1 << i)) {
                    LOG_DBG("  %s = %.3f", axis_names[i], (double)pkt.setpoint[i]);
                }
            }
        } else if (pkt.type == SP_OVR_CLEAR) {
            control_clear_override();
        } else {
            LOG_WRN("Setpoint override: unknown type 0x%02X", pkt.type);
        }
    }
}

void setpoint_override_start(void)
{
    k_tid_t tid = k_thread_create(&sp_ovr_thread_data,
                                   sp_ovr_stack,
                                   K_THREAD_STACK_SIZEOF(sp_ovr_stack),
                                   sp_ovr_thread,
                                   NULL, NULL, NULL,
                                   K_PRIO_COOP(7), 0, K_NO_WAIT);
    if (tid) {
        k_thread_name_set(tid, "sp_override");
    }
}
