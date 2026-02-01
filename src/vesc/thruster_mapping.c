#include "thruster_mapping.h"
#include "vesc_uart_zephyr.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(thruster, LOG_LEVEL_INF);

/* Normalize int8_t (-128 to +127) to float (-0.5 to +0.5) for 50% max duty */
static inline float normalize(int8_t value)
{
    return (float)value / 254.0f;  // 127 -> 0.5, -128 -> -0.504 (close to -0.5)
}

/* TEST MODE: Max 50% duty cycle for safety */
#define MAX_TEST_DUTY 0.5f

void thruster_calculate_6dof(int8_t surge, int8_t sway, int8_t heave,
                             int8_t roll, int8_t pitch, int8_t yaw,
                             thruster_output_t *output)
{
    /* Normalize inputs directly to -0.5 to +0.5 range (50% max duty) */
    float s_surge = normalize(surge);
    float s_sway = normalize(sway);
    float s_heave = normalize(heave);
    float s_yaw = normalize(yaw);

    /* 
     * TEST MODE: Each thruster assigned to single axis
     * Thruster 0: surge
     * Thruster 1: sway
     * Thruster 2: heave
     * Thruster 3: yaw
     * Thrusters 4-7: disabled
     */
    
    output->thruster[0] = s_surge;   // Surge only
    output->thruster[1] = s_sway;    // Sway only
    output->thruster[2] = s_heave;   // Heave only
    output->thruster[3] = s_yaw;     // Yaw only
    output->thruster[4] = 0.0f;      // Disabled
    output->thruster[5] = 0.0f;      // Disabled
    output->thruster[6] = 0.0f;      // Disabled
    output->thruster[7] = 0.0f;      // Disabled

    /* Log thruster values if any are non-zero */
    if (output->thruster[0] != 0.0f || output->thruster[1] != 0.0f ||
        output->thruster[2] != 0.0f || output->thruster[3] != 0.0f) {
        /* Convert to percentage integers since Zephyr LOG doesn't support %f */
        int t0_pct = (int)(output->thruster[0] * 100);
        int t1_pct = (int)(output->thruster[1] * 100);
        int t2_pct = (int)(output->thruster[2] * 100);
        int t3_pct = (int)(output->thruster[3] * 100);
        LOG_INF("Thrusters: T0(surge)=%d%% T1(sway)=%d%% T2(heave)=%d%% T3(yaw)=%d%%",
                t0_pct, t1_pct, t2_pct, t3_pct);
    }
}

void thruster_send_outputs(const thruster_output_t *output)
{
    /* Debug: print what we're about to send */
    int t0 = (int)(output->thruster[0] * 1000);
    LOG_DBG("Sending T0: %d/1000", t0);
    
    /* Send to local VESC via UART (Thruster 0 - surge) */
    vesc_set_duty_local(output->thruster[0]);
    
    /* Send to remote VESCs via CAN (only 1-3 active for testing) */
    vesc_set_duty_can(THRUSTER_FR_TOP, output->thruster[1]);   // sway
    vesc_set_duty_can(THRUSTER_BL_TOP, output->thruster[2]);   // heave
    vesc_set_duty_can(THRUSTER_BR_TOP, output->thruster[3]);   // yaw
    
    /* Thrusters 4-7 disabled for testing */
    // vesc_set_duty_can(THRUSTER_FL_BOTTOM, output->thruster[4]);
    // vesc_set_duty_can(THRUSTER_FR_BOTTOM, output->thruster[5]);
    // vesc_set_duty_can(THRUSTER_BL_BOTTOM, output->thruster[6]);
    // vesc_set_duty_can(THRUSTER_BR_BOTTOM, output->thruster[7]);
}