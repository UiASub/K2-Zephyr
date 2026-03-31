/*
 * Axis Config — UDP service for receiving IMU axis remapping and offset
 *
 * Listens on UDP port 5004 for configuration packets from topside.
 * Supports remapping of:
 *   - Yaw/Pitch/Roll axes (for angular PID)
 *   - Accelerometer X/Y/Z axes (for translational PID)
 *   - IMU offset from center of mass (for centripetal compensation)
 *
 * Packet layout (30 bytes):
 *   | type (1B)
 *   | yaw_src (1B) | yaw_sign (1B) | pitch_src (1B) | pitch_sign (1B)
 *   | roll_src (1B) | roll_sign (1B)
 *   | ax_src (1B) | ax_sign (1B) | ay_src (1B) | ay_sign (1B)
 *   | az_src (1B) | az_sign (1B)
 *   | pad (1B)
 *   | offset_x (4B float) | offset_y (4B float) | offset_z (4B float)
 *   | crc32 (4B)
 *
 * Type values:
 *   0x01 = SET    — update config, reply with active values
 *   0x02 = REQUEST — reply with current config
 *
 * src values (YPR): 0=yaw, 1=pitch, 2=roll
 * src values (accel): 0=x, 1=y, 2=z
 * sign values: 0=positive(+1), 1=negative(-1)
 * offset: millimeters from IMU to center of mass
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <string.h>

#include "axis_config.h"
#include "net.h"

LOG_MODULE_REGISTER(axis_config, LOG_LEVEL_INF);

#define AXIS_PKT_SET     0x01
#define AXIS_PKT_REQUEST 0x02

static const char *ypr_names[] = {"yaw", "pitch", "roll"};
static const char *accel_names[] = {"x", "y", "z"};

typedef struct {
    uint8_t  type;
    /* YPR remap */
    uint8_t  yaw_src;
    uint8_t  yaw_sign;
    uint8_t  pitch_src;
    uint8_t  pitch_sign;
    uint8_t  roll_src;
    uint8_t  roll_sign;
    /* Accel remap */
    uint8_t  ax_src;
    uint8_t  ax_sign;
    uint8_t  ay_src;
    uint8_t  ay_sign;
    uint8_t  az_src;
    uint8_t  az_sign;
    uint8_t  _pad;
    /* IMU offset from center of mass (mm) */
    float    offset_x;
    float    offset_y;
    float    offset_z;
    /* Integrity */
    uint32_t crc32;
} __attribute__((packed)) axis_packet_t;

/* Default: identity mapping, no inversion */
static axis_map_t ypr_map[3] = {
    { .src = AXIS_SRC_YAW,   .sign = 1 },
    { .src = AXIS_SRC_PITCH, .sign = 1 },
    { .src = AXIS_SRC_ROLL,  .sign = 1 },
};

static axis_map_t accel_map[3] = {
    { .src = ACCEL_SRC_X, .sign = 1 },
    { .src = ACCEL_SRC_Y, .sign = 1 },
    { .src = ACCEL_SRC_Z, .sign = 1 },
};

static imu_offset_t current_offset = { .x = 0.0f, .y = 0.0f, .z = 0.0f };

K_MUTEX_DEFINE(axis_map_mutex);

K_THREAD_STACK_DEFINE(axis_config_stack, 2048);
static struct k_thread axis_config_thread_data;

void axis_config_remap_ypr(float raw_yaw, float raw_pitch, float raw_roll,
                           float *out_yaw, float *out_pitch, float *out_roll)
{
    float raw[3] = { raw_yaw, raw_pitch, raw_roll };

    k_mutex_lock(&axis_map_mutex, K_FOREVER);
    *out_yaw   = raw[ypr_map[0].src] * ypr_map[0].sign;
    *out_pitch = raw[ypr_map[1].src] * ypr_map[1].sign;
    *out_roll  = raw[ypr_map[2].src] * ypr_map[2].sign;
    k_mutex_unlock(&axis_map_mutex);
}

void axis_config_remap_accel(float raw_ax, float raw_ay, float raw_az,
                             float *out_ax, float *out_ay, float *out_az)
{
    float raw[3] = { raw_ax, raw_ay, raw_az };

    k_mutex_lock(&axis_map_mutex, K_FOREVER);
    *out_ax = raw[accel_map[0].src] * accel_map[0].sign;
    *out_ay = raw[accel_map[1].src] * accel_map[1].sign;
    *out_az = raw[accel_map[2].src] * accel_map[2].sign;
    k_mutex_unlock(&axis_map_mutex);
}

imu_offset_t axis_config_get_offset(void)
{
    imu_offset_t off;
    k_mutex_lock(&axis_map_mutex, K_FOREVER);
    off = current_offset;
    k_mutex_unlock(&axis_map_mutex);
    return off;
}

static bool valid_src(uint8_t s)
{
    return s <= 2;
}

static int8_t decode_sign(uint8_t s)
{
    return s ? -1 : 1;
}

static void apply_packet(const axis_packet_t *pkt)
{
    if (!valid_src(pkt->yaw_src) || !valid_src(pkt->pitch_src) ||
        !valid_src(pkt->roll_src) || !valid_src(pkt->ax_src) ||
        !valid_src(pkt->ay_src) || !valid_src(pkt->az_src)) {
        LOG_WRN("Axis config: invalid source index, dropping");
        return;
    }

    k_mutex_lock(&axis_map_mutex, K_FOREVER);

    ypr_map[0].src  = pkt->yaw_src;
    ypr_map[0].sign = decode_sign(pkt->yaw_sign);
    ypr_map[1].src  = pkt->pitch_src;
    ypr_map[1].sign = decode_sign(pkt->pitch_sign);
    ypr_map[2].src  = pkt->roll_src;
    ypr_map[2].sign = decode_sign(pkt->roll_sign);

    accel_map[0].src  = pkt->ax_src;
    accel_map[0].sign = decode_sign(pkt->ax_sign);
    accel_map[1].src  = pkt->ay_src;
    accel_map[1].sign = decode_sign(pkt->ay_sign);
    accel_map[2].src  = pkt->az_src;
    accel_map[2].sign = decode_sign(pkt->az_sign);

    current_offset.x = pkt->offset_x;
    current_offset.y = pkt->offset_y;
    current_offset.z = pkt->offset_z;

    k_mutex_unlock(&axis_map_mutex);

    LOG_INF("Axis YPR: yaw=%s%s pitch=%s%s roll=%s%s",
            pkt->yaw_sign ? "-" : "+", ypr_names[pkt->yaw_src],
            pkt->pitch_sign ? "-" : "+", ypr_names[pkt->pitch_src],
            pkt->roll_sign ? "-" : "+", ypr_names[pkt->roll_src]);
    LOG_INF("Axis Accel: x=%s%s y=%s%s z=%s%s",
            pkt->ax_sign ? "-" : "+", accel_names[pkt->ax_src],
            pkt->ay_sign ? "-" : "+", accel_names[pkt->ay_src],
            pkt->az_sign ? "-" : "+", accel_names[pkt->az_src]);
    LOG_INF("IMU offset: x=%d y=%d z=%d mm",
            (int)pkt->offset_x, (int)pkt->offset_y, (int)pkt->offset_z);
}

static void send_config_reply(int sock, struct sockaddr_in *dest)
{
    axis_packet_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = AXIS_PKT_SET;

    k_mutex_lock(&axis_map_mutex, K_FOREVER);

    reply.yaw_src    = ypr_map[0].src;
    reply.yaw_sign   = (ypr_map[0].sign < 0) ? 1 : 0;
    reply.pitch_src  = ypr_map[1].src;
    reply.pitch_sign = (ypr_map[1].sign < 0) ? 1 : 0;
    reply.roll_src   = ypr_map[2].src;
    reply.roll_sign  = (ypr_map[2].sign < 0) ? 1 : 0;

    reply.ax_src  = accel_map[0].src;
    reply.ax_sign = (accel_map[0].sign < 0) ? 1 : 0;
    reply.ay_src  = accel_map[1].src;
    reply.ay_sign = (accel_map[1].sign < 0) ? 1 : 0;
    reply.az_src  = accel_map[2].src;
    reply.az_sign = (accel_map[2].sign < 0) ? 1 : 0;

    reply.offset_x = current_offset.x;
    reply.offset_y = current_offset.y;
    reply.offset_z = current_offset.z;

    k_mutex_unlock(&axis_map_mutex);

    reply.crc32 = crc32_calc(&reply, sizeof(reply) - sizeof(reply.crc32));

    zsock_sendto(sock, &reply, sizeof(reply), 0,
                 (struct sockaddr *)dest, sizeof(*dest));
}

static void axis_config_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1); ARG_UNUSED(arg2); ARG_UNUSED(arg3);

    int sock;
    struct sockaddr_in bind_addr, client_addr;
    socklen_t client_addr_len;
    axis_packet_t packet;
    int ret;

    while (!network_ready) {
        k_sleep(K_MSEC(100));
    }

    sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create axis config socket: %d", sock);
        return;
    }

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port        = htons(AXIS_CONFIG_PORT);

    ret = zsock_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    if (ret < 0) {
        LOG_ERR("Failed to bind axis config socket: %d", ret);
        zsock_close(sock);
        return;
    }

    LOG_INF("Axis config listener ready on port %d", AXIS_CONFIG_PORT);

    while (1) {
        client_addr_len = sizeof(client_addr);
        ret = zsock_recvfrom(sock, &packet, sizeof(packet), 0,
                             (struct sockaddr *)&client_addr, &client_addr_len);

        if (ret != sizeof(axis_packet_t)) {
            if (ret < 0) {
                LOG_ERR("Axis config recv error: %d", ret);
                k_sleep(K_MSEC(100));
            } else {
                LOG_WRN("Axis config: wrong packet size %d (expected %d)",
                        ret, sizeof(axis_packet_t));
            }
            continue;
        }

        uint32_t calc_crc = crc32_calc(&packet,
                                       sizeof(packet) - sizeof(packet.crc32));
        if (calc_crc != packet.crc32) {
            LOG_ERR("Axis config CRC mismatch (calc 0x%08X, recv 0x%08X)",
                    calc_crc, packet.crc32);
            continue;
        }

        switch (packet.type) {
        case AXIS_PKT_SET:
            apply_packet(&packet);
            send_config_reply(sock, &client_addr);
            break;

        case AXIS_PKT_REQUEST:
            LOG_INF("Axis config requested");
            send_config_reply(sock, &client_addr);
            break;

        default:
            LOG_WRN("Axis config: unknown packet type 0x%02X", packet.type);
            break;
        }
    }
}

void axis_config_start(void)
{
    k_tid_t tid = k_thread_create(&axis_config_thread_data,
                                  axis_config_stack,
                                  K_THREAD_STACK_SIZEOF(axis_config_stack),
                                  axis_config_thread,
                                  NULL, NULL, NULL,
                                  K_PRIO_COOP(7), 0, K_NO_WAIT);
    if (tid) {
        LOG_INF("Axis config thread started");
    } else {
        LOG_ERR("Failed to start axis config thread");
    }
}
