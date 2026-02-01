#include "vesc_protocol.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vesc_proto, LOG_LEVEL_DBG);

/* Packet markers */
#define VESC_START_BYTE  0x02
#define VESC_STOP_BYTE   0x03

/* Commands (from datatypes.h in VESC firmware) */
typedef enum {
    COMM_SET_DUTY = 5,
    COMM_CAN_FORWARD = 34,
} COMM_PACKET_ID;

/* CRC16-CCITT used by VESC (polynomial 0x1021) */
static uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static void buf_append_int32(uint8_t *buf, int32_t val, size_t *idx)
{
    buf[(*idx)++] = (val >> 24) & 0xFF;
    buf[(*idx)++] = (val >> 16) & 0xFF;
    buf[(*idx)++] = (val >> 8) & 0xFF;
    buf[(*idx)++] = val & 0xFF;
}

static size_t vesc_wrap_packet(uint8_t *buf,
                               const uint8_t *payload,
                               size_t payload_len)
{
    size_t idx = 0;

    buf[idx++] = VESC_START_BYTE;
    buf[idx++] = payload_len;

    for (size_t i = 0; i < payload_len; i++) {
        buf[idx++] = payload[i];
    }

    uint16_t crc = crc16(payload, payload_len);
    buf[idx++] = (crc >> 8) & 0xFF;
    buf[idx++] = crc & 0xFF;

    buf[idx++] = VESC_STOP_BYTE;

    return idx;
}

size_t vesc_build_set_duty(uint8_t *buf, float duty)
{
    uint8_t payload[8];
    size_t p = 0;

    payload[p++] = COMM_SET_DUTY;

    /* Duty is -100000 to +100000 (representing -100% to +100%) */
    int32_t duty_val = (int32_t)(duty * 100000.0f);
    
    LOG_DBG("UART duty input: %d/1000 -> raw: %d", (int)(duty * 1000), duty_val);
    
    buf_append_int32(payload, duty_val, &p);

    return vesc_wrap_packet(buf, payload, p);
}

/* Build CAN forwarded duty command (COMM_FORWARD_CAN) */
size_t vesc_build_set_duty_can(uint8_t *buf, uint8_t can_id, float duty)
{
    uint8_t payload[16];
    size_t p = 0;

    payload[p++] = COMM_CAN_FORWARD;  // 34
    payload[p++] = can_id;             // CAN ID (e.g., 118)
    payload[p++] = COMM_SET_DUTY;      // 5

    /* Duty is -100000 to +100000 (representing -100% to +100%) */
    int32_t duty_val = (int32_t)(duty * 100000.0f);
    
    LOG_DBG("CAN[%d] duty input: %d/1000 -> raw: %d", can_id, (int)(duty * 1000), duty_val);
    
    buf_append_int32(payload, duty_val, &p);

    return vesc_wrap_packet(buf, payload, p);
}