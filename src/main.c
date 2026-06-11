#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "vesc/vesc_uart_zephyr.h"

LOG_MODULE_REGISTER(k2_app, LOG_LEVEL_INF);

#define THRUSTER_5_CAN_ID 5
#define ON_DUTY 0.20f
#define OFF_DUTY 0.0f
#define PHASE_TIME_MS 3000
#define COMMAND_PERIOD_MS 100

int main(void)
{
    LOG_INF("K2 thruster 0 and 5 duty test starting");

    int ret = vesc_uart_init();
    if (ret < 0) {
        LOG_ERR("VESC UART init failed: %d", ret);
        return 0;
    }

    while (1) {
        LOG_INF("Thrusters 0 and 5 on at 20%%");
        for (int i = 0; i < PHASE_TIME_MS / COMMAND_PERIOD_MS; i++) {
            vesc_set_duty_local(ON_DUTY);
            vesc_set_duty_can(THRUSTER_5_CAN_ID, ON_DUTY);
            k_sleep(K_MSEC(COMMAND_PERIOD_MS));
        }

        LOG_INF("Thrusters 0 and 5 off");
        for (int i = 0; i < PHASE_TIME_MS / COMMAND_PERIOD_MS; i++) {
            vesc_set_duty_local(OFF_DUTY);
            vesc_set_duty_can(THRUSTER_5_CAN_ID, OFF_DUTY);
            k_sleep(K_MSEC(COMMAND_PERIOD_MS));
        }
    }

    return 0;
}
