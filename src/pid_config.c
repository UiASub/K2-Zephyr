/*
 * PID Config — UDP service for live-tuning PID gains on all 6 axes
 *
 * This module listens on its own UDP port (5003) for packets from the topside
 * computer. It supports two operations:
 *   SET (0x01)     – update all 6 axes' P, I, D gains, reply with active values
 *   REQUEST (0x02) – reply with the current gains without changing anything
 *
 * Every packet carries a CRC32 checksum so we can detect corruption.
 * On CRC failure we silently drop the packet — the topside is responsible for
 * retrying if it doesn't get a reply within its timeout.
 *
 * Packet layout (77 bytes, both directions):
 *   | type (1B) | surge P,I,D (12B) | sway P,I,D (12B) | heave P,I,D (12B)
 *   | roll P,I,D (12B) | pitch P,I,D (12B) | yaw P,I,D (12B) | crc32 (4B) |
 *
 * To change a single axis the topside should REQUEST first, modify the axis
 * it cares about, then SET the full packet back. This keeps the protocol
 * simple — one packet always carries the complete state.
 *
 * The gains are stored behind a mutex so the future PID controller thread can
 * safely read them with pid_config_get_gains().
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <string.h>

#include "pid_config.h"
#include "net.h"

LOG_MODULE_REGISTER(pid_config, LOG_LEVEL_INF);

/* Packet type field values */
#define PID_PKT_SET     0x01   /* Topside wants to write new gains */
#define PID_PKT_REQUEST 0x02   /* Topside wants to read current gains */

/* Human-readable axis names for log messages */
static const char *axis_names[PID_AXIS_COUNT] = {
    "surge", "sway", "heave", "roll", "pitch", "yaw"
};

/*
 * Wire format for PID packets.
 * __attribute__((packed)) ensures the compiler doesn't add padding bytes
 * between fields — important because the topside must pack the bytes the
 * exact same way.
 *
 * The axes array holds P, I, D for each of the 6 DOF in order:
 * [0]=surge [1]=sway [2]=heave [3]=roll [4]=pitch [5]=yaw
 */
typedef struct {
    uint8_t    type;
    pid_gains_t axes[PID_AXIS_COUNT];   /* 6 axes x 3 floats = 72 bytes */
    uint32_t   crc32;
} __attribute__((packed)) pid_packet_t;

/*
 * The active PID gains for all 6 axes. Protected by a mutex because two
 * threads may access them: this listener thread (writes) and the future
 * PID controller (reads).
 * Initialised to zero — no control action until topside sends real values.
 */
static pid_gains_t current_gains[PID_AXIS_COUNT] = {0};
K_MUTEX_DEFINE(pid_gains_mutex);

/* Thread stack and control block — Zephyr needs these to create a thread */
K_THREAD_STACK_DEFINE(pid_config_stack, 2048);
static struct k_thread pid_config_thread_data;

/**
 * Thread-safe getter for the PID gains of a single axis.
 * Call this from the PID controller to get a consistent snapshot.
 */
pid_gains_t pid_config_get_gains(enum pid_axis axis)
{
    pid_gains_t gains = {0};

    if (axis >= PID_AXIS_COUNT) {
        return gains;
    }

    k_mutex_lock(&pid_gains_mutex, K_FOREVER);
    gains = current_gains[axis];
    k_mutex_unlock(&pid_gains_mutex);
    return gains;
}

/**
 * Build a reply packet with all current gains and send it back to the topside.
 * Used after both SET and REQUEST so the topside can confirm what the MCU has.
 */
static void send_gains_reply(int sock, struct sockaddr_in *dest)
{
    pid_packet_t reply;

    reply.type = PID_PKT_SET;

    k_mutex_lock(&pid_gains_mutex, K_FOREVER);
    memcpy(reply.axes, current_gains, sizeof(current_gains));
    k_mutex_unlock(&pid_gains_mutex);

    /* CRC covers everything except the CRC field itself */
    reply.crc32 = crc32_calc(&reply, sizeof(reply) - sizeof(reply.crc32));

    zsock_sendto(sock, &reply, sizeof(reply), 0,
                 (struct sockaddr *)dest, sizeof(*dest));
}

/**
 * Main loop for the PID config listener.
 * Runs in its own thread so it never blocks the control loop or command socket.
 */
static void pid_config_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1); ARG_UNUSED(arg2); ARG_UNUSED(arg3);

    int sock;
    struct sockaddr_in bind_addr, client_addr;
    socklen_t client_addr_len;
    pid_packet_t packet;
    int ret;

    /* Wait until the network interface is up (IP assigned, link ready) */
    while (!network_ready) {
        k_sleep(K_MSEC(100));
    }

    /* Create a UDP socket — SOCK_DGRAM means UDP (no connection needed) */
    sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create PID config socket: %d", sock);
        return;
    }

    /* Bind to port 5003 on all interfaces (INADDR_ANY) so we accept packets
     * regardless of which IP they were addressed to */
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port        = htons(PID_CONFIG_PORT);

    ret = zsock_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    if (ret < 0) {
        LOG_ERR("Failed to bind PID config socket: %d", ret);
        zsock_close(sock);
        return;
    }

    LOG_INF("PID config listener ready on port %d", PID_CONFIG_PORT);

    while (1) {
        /* recvfrom blocks until a packet arrives.
         * It also fills client_addr with the sender's IP+port so we can reply */
        client_addr_len = sizeof(client_addr);
        ret = zsock_recvfrom(sock, &packet, sizeof(packet), 0,
                             (struct sockaddr *)&client_addr, &client_addr_len);

        /* Reject packets that aren't exactly the right size */
        if (ret != sizeof(pid_packet_t)) {
            if (ret < 0) {
                LOG_ERR("PID config recv error: %d", ret);
                k_sleep(K_MSEC(100));
            } else {
                LOG_WRN("PID config: wrong packet size %d (expected %d)",
                        ret, sizeof(pid_packet_t));
            }
            continue;
        }

        /* CRC check — recompute over everything except the CRC field and
         * compare with what the sender put in the packet */
        uint32_t calc_crc = crc32_calc(&packet,
                                       sizeof(packet) - sizeof(packet.crc32));
        uint32_t recv_crc = packet.crc32;

        if (calc_crc != recv_crc) {
            LOG_ERR("PID config CRC mismatch (calc 0x%08X, recv 0x%08X)",
                    calc_crc, recv_crc);
            /* Drop corrupted packet — topside will timeout and resend */
            continue;
        }

        switch (packet.type) {
        case PID_PKT_SET:
            /* Store all 6 axes' gains atomically and reply so topside can verify */
            k_mutex_lock(&pid_gains_mutex, K_FOREVER);
            memcpy(current_gains, packet.axes, sizeof(current_gains));
            k_mutex_unlock(&pid_gains_mutex);

            /* Log each axis (gains x1000 for readability without %f) */
            for (int i = 0; i < PID_AXIS_COUNT; i++) {
                LOG_INF("PID %-5s  P=%d.%03d  I=%d.%03d  D=%d.%03d",
                        axis_names[i],
                        (int)packet.axes[i].kp,
                        (int)(packet.axes[i].kp * 1000) % 1000,
                        (int)packet.axes[i].ki,
                        (int)(packet.axes[i].ki * 1000) % 1000,
                        (int)packet.axes[i].kd,
                        (int)(packet.axes[i].kd * 1000) % 1000);
            }

            send_gains_reply(sock, &client_addr);
            break;

        case PID_PKT_REQUEST:
            /* Just reply with current gains, don't change anything */
            LOG_INF("PID gains requested");
            send_gains_reply(sock, &client_addr);
            break;

        default:
            LOG_WRN("PID config: unknown packet type 0x%02X", packet.type);
            break;
        }
    }
}

/**
 * Spawn the PID config listener thread.
 * Called once from main() during startup.
 */
void pid_config_start(void)
{
    k_tid_t tid = k_thread_create(&pid_config_thread_data,
                                  pid_config_stack,
                                  K_THREAD_STACK_SIZEOF(pid_config_stack),
                                  pid_config_thread,
                                  NULL, NULL, NULL,
                                  K_PRIO_COOP(7), 0, K_NO_WAIT);
    if (tid) {
        LOG_INF("PID config thread started");
    } else {
        LOG_ERR("Failed to start PID config thread");
    }
}
