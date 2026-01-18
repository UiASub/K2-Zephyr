#include "vesc_protocol.h"

/* Packet markers */
#define VESC_START_BYTE  0x02
#define VESC_STOP_BYTE   0x03

/* Commands (from datatypes.h in VESC firmware) */
typedef enum {
    COMM_SET_DUTY = 0,
    COMM_SET_CURRENT = 1,
    COMM_GET_VALUES = 4,
    COMM_CAN_FORWARD = 34,
    COMM_CAN_SET_CURRENT = 35
} COMM_PACKET_ID;

static uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
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

size_t vesc_build_set_current(uint8_t *buf, float current)
{
    uint8_t payload[8];
    size_t p = 0;

    payload[p++] = COMM_SET_CURRENT;

    int32_t current_mA = (int32_t)(current * 1000.0f);
    buf_append_int32(payload, current_mA, &p);

    return vesc_wrap_packet(buf, payload, p);
}

size_t vesc_build_can_set_current(uint8_t *buf,
                                  uint8_t can_id,
                                  float current)
{
    uint8_t payload[10];
    size_t p = 0;

    payload[p++] = COMM_CAN_SET_CURRENT;
    payload[p++] = can_id;

    int32_t current_mA = (int32_t)(current * 1000.0f);
    buf_append_int32(payload, current_mA, &p);

    return vesc_wrap_packet(buf, payload, p);
}
