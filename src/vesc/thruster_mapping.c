#include "thruster_mapping.h"
#include "vesc_uart_zephyr.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(thruster, LOG_LEVEL_INF);

/* 
 * Thruster Mixing Matrix (6DOF -> 8 thrusters)
 * 
 * Pure geometric contribution only — CW/CCW and wiring corrections
 * are handled separately in MOTOR_DIRECTION[].
 * 
 * Columns indexed by CAN ID:
 *   [0]TLF  [1]TLB  [2]BLB  [3]BLF  [4]BRF  [5]BRB  [6]TRB  [7]TRF
 * Rows: Surge, Sway, Heave, Roll, Pitch, Yaw
 * 
 * +1 = positive input increases thrust
 * -1 = positive input decreases thrust
 */
static const float THRUSTER_MATRIX[6][8] = {
    /*          TLF  TLB  BLB  BLF  BRF  BRB  TRB  TRF */
    /*Surge*/ {  1,  -1,  -1,   1,   1,  -1,  -1,   1 },
    /*Sway */ { -1,  -1,  -1,  -1,   1,   1,   1,   1 },
    /*Heave*/ { -1,  -1,   1,   1,   1,   1,  -1,  -1 },
    /*Roll */ {  1,   1,  -1,  -1,   1,   1,  -1,  -1 },
    /*Pitch*/ {  1,  -1,   1,  -1,  -1,   1,  -1,   1 },
    /*Yaw  */ {  1,  -1,  -1,   1,  -1,   1,   1,  -1 },
};

/*
 * Motor direction correction: accounts for prop rotation (CW/CCW)
 * and any wiring/soldering issues.
 * 
 * CW  prop → +1  (positive duty = intended thrust direction)
 * CCW prop → -1  (positive duty = opposite, needs sign flip)
 * 
 * Index: 0=TLF(CCW), 1=TLB(CW), 2=BLB(CCW), 3=BLF(CW),
 *        4=BRF(CCW, reversed soldering: -1*-1=+1),
 *        5=BRB(CW), 6=TRB(CCW), 7=TRF(CW)
 */
static const float MOTOR_DIRECTION[8] = {
    -1, +1, -1, +1, +1, +1, -1, +1
};

/* Maximum duty cycle for safety (50% for testing) */
#define MAX_DUTY 0.5f

/* Normalize int8_t (-128 to +127) to float (-1.0 to +1.0) */
static inline float normalize(int8_t value)
{
    return (float)value / 127.0f;
}

/* Clamp float value to range */
static inline float clamp_f(float value, float min_val, float max_val)
{
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

void thruster_calculate_6dof(int8_t surge, int8_t sway, int8_t heave,
                             int8_t roll, int8_t pitch, int8_t yaw,
                             thruster_output_t *output)
{
    /* Normalize inputs to -1.0 to +1.0 range */
    float inputs[6] = {
        normalize(surge),
        normalize(sway),
        normalize(heave),
        normalize(roll),
        normalize(pitch),
        normalize(yaw)
    };
    
    /* Matrix multiplication: thruster[i] = sum(matrix[axis][i] * input[axis]) */
    float raw[8];
    for (int i = 0; i < 8; i++) {
        float sum = 0.0f;
        for (int axis = 0; axis < 6; axis++) {
            sum += THRUSTER_MATRIX[axis][i] * inputs[axis];
        }
        raw[i] = sum;
    }

    /* Normalize: if any thruster exceeds ±1.0, scale all proportionally.
     * This preserves the ratio between DOFs instead of hard-clamping. */
    float max_raw = 0.0f;
    for (int i = 0; i < 8; i++) {
        float abs_val = raw[i] > 0 ? raw[i] : -raw[i];
        if (abs_val > max_raw) max_raw = abs_val;
    }
    if (max_raw > 1.0f) {
        float scale = 1.0f / max_raw;
        for (int i = 0; i < 8; i++) {
            raw[i] *= scale;
        }
    }

    /* Apply motor direction correction and MAX_DUTY scaling */
    for (int i = 0; i < 8; i++) {
        output->thruster[i] = MOTOR_DIRECTION[i] * raw[i] * MAX_DUTY;
    }

    /* Find max for logging */
    float max_output = 0.0f;
    for (int i = 0; i < 8; i++) {
        float abs_val = output->thruster[i] > 0 ? output->thruster[i] : -output->thruster[i];
        if (abs_val > max_output) max_output = abs_val;
    }
    
    /* Log if any thruster has significant output */
    if (max_output > 0.01f) {
        /* Convert to percentage integers for logging */
        int t_pct[8];
        for (int i = 0; i < 8; i++) {
            t_pct[i] = (int)(output->thruster[i] * 100);
        }
        LOG_INF("T[TLF:%+3d TLB:%+3d BLB:%+3d BLF:%+3d BRF:%+3d BRB:%+3d TRB:%+3d TRF:%+3d]%%",
                t_pct[0], t_pct[1], t_pct[2], t_pct[3], 
                t_pct[4], t_pct[5], t_pct[6], t_pct[7]);
    }
}

void thruster_send_outputs(const thruster_output_t *output)
{


    /* TLF (CAN 0) is connected directly via UART */
    vesc_set_duty_local(output->thruster[THRUSTER_TLF]);
    
    /* Remaining 7 thrusters via CAN bus */
    vesc_set_duty_can(THRUSTER_TLB, output->thruster[THRUSTER_TLB]);
    vesc_set_duty_can(THRUSTER_BLB, output->thruster[THRUSTER_BLB]);
    vesc_set_duty_can(THRUSTER_BLF, output->thruster[THRUSTER_BLF]);
    vesc_set_duty_can(THRUSTER_BRF, output->thruster[THRUSTER_BRF]);
    vesc_set_duty_can(THRUSTER_BRB, output->thruster[THRUSTER_BRB]);
    vesc_set_duty_can(THRUSTER_TRB, output->thruster[THRUSTER_TRB]);
    vesc_set_duty_can(THRUSTER_TRF, output->thruster[THRUSTER_TRF]);
}