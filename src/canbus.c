#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdint.h>
#include "canbus.h"

LOG_MODULE_REGISTER(canbus, LOG_LEVEL_INF);

// Local CAN device handle
static const struct device *can_dev = NULL;

/**
 * @brief CAN transmit callback
 */
static void tx_callback(const struct device *dev, int error, void *user_data)
{
    const char *sender = user_data;
    if (error) {
        LOG_ERR("CAN send failed [%d] from %s", error, sender);
    } else {
        LOG_INF("CAN frame sent OK from %s", sender);
    }
}

/**
 * @brief Initialize CAN bus at 500 kbps
 */
int canbus_init(void)
{
    LOG_INF("Initializing CAN...");

    can_dev = DEVICE_DT_GET(DT_NODELABEL(can1));
    if (!device_is_ready(can_dev)) {
        LOG_ERR("CAN device not ready");
        return -1;
    }

    // Example timing for 500 kbps on 48MHz clock (adjust if needed)
    struct can_timing timing = {
        .sjw = 1,
        .prop_seg = 1,
        .phase_seg1 = 13,
        .phase_seg2 = 2,
        .prescaler = 6
    };

    int ret = can_set_timing(can_dev, &timing);
    if (ret) {
        LOG_ERR("Failed to set CAN timing: %d", ret);
        return ret;
    }

    ret = can_start(can_dev);
    if (ret) {
        LOG_ERR("Failed to start CAN controller: %d", ret);
        return ret;
    }

    LOG_INF("CAN initialized successfully at 500kbps");
    return 0;
}

/**
 * @brief Simple test frame (legacy)
 */
int send_test_frame(void)
{
    if (!can_dev) {
        LOG_ERR("CAN not initialized");
        return -ENODEV;
    }

    struct can_frame frame = {
        .id = 0x223,  // Example: VESC ID 68 + CMD=3 (set RPM)
        .dlc = 8,
        .flags = 0
    };

    int32_t rpm = 2000;
    frame.data[0] = (rpm >> 24) & 0xFF;
    frame.data[1] = (rpm >> 16) & 0xFF;
    frame.data[2] = (rpm >> 8) & 0xFF;
    frame.data[3] = rpm & 0xFF;
    memset(&frame.data[4], 0, 4);

    LOG_INF("Sending test frame to VESC (RPM=%d)", rpm);
    return can_send(can_dev, &frame, K_FOREVER, tx_callback, "test");
}

/**
 * @brief Helper: encode 32-bit big-endian integer
 */
static inline void put_i32_be(uint8_t *dst, int32_t v)
{
    dst[0] = (uint8_t)((v >> 24) & 0xFF);
    dst[1] = (uint8_t)((v >> 16) & 0xFF);
    dst[2] = (uint8_t)((v >> 8) & 0xFF);
    dst[3] = (uint8_t)((v >> 0) & 0xFF);
}

/**
 * @brief Helper: build extended CAN ID for VESC commands
 */
static inline uint32_t vesc_eid(uint8_t cmd, uint8_t vesc_id)
{
    return ((uint32_t)cmd << 8) | vesc_id;
}

/**
 * @brief Send a current command (drive current)
 */
int send_set_current(uint8_t vesc_id, float amps)
{
    if (!can_dev) {
        LOG_ERR("CAN not initialized");
        return -ENODEV;
    }

    const uint8_t CMD_SET_CURRENT = 1;
    int32_t scaled = (int32_t)(amps * 1000.0f);

    struct can_frame fr = {
        .id = vesc_eid(CMD_SET_CURRENT, vesc_id),
        .dlc = 4,
        .flags = CAN_FRAME_IDE // use 29-bit extended ID
    };

    put_i32_be(fr.data, scaled);

    LOG_INF("Sending SET_CURRENT %.2fA to VESC ID=%d", amps, vesc_id);
    return can_send(can_dev, &fr, K_NO_WAIT, tx_callback, "set_current");
}

/**
 * @brief Send a braking current command
 */
int send_set_current_brake(uint8_t vesc_id, float brake_amps)
{
    if (!can_dev) {
        LOG_ERR("CAN not initialized");
        return -ENODEV;
    }

    const uint8_t CMD_SET_CURRENT_BRAKE = 2;
    int32_t scaled = (int32_t)(brake_amps * 1000.0f);

    struct can_frame fr = {
        .id = vesc_eid(CMD_SET_CURRENT_BRAKE, vesc_id),
        .dlc = 4,
        .flags = CAN_FRAME_IDE
    };

    put_i32_be(fr.data, scaled);

    LOG_INF("Sending BRAKE_CURRENT %.2fA to VESC ID=%d", brake_amps, vesc_id);
    return can_send(can_dev, &fr, K_NO_WAIT, tx_callback, "set_brake");
}
