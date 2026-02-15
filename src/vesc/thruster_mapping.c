#include "thruster_mapping.h"
#include "vesc_uart_zephyr.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(thruster, LOG_LEVEL_INF);

/* 
 * Thruster Mixing Matrix (6DOF -> 8 thrusters)
 * 
 * Columns: T1(FR-Top), T2(FL-Top), T3(FR-Bot), T4(FL-Bot), 
 *          T5(BR-Top), T6(BL-Top), T7(BR-Bot), T8(BL-Bot)
 * Rows: Surge, Sway, Heave, Roll, Pitch, Yaw
 * 
 * Each row defines how that DOF contributes to each thruster.
 * +1 = positive input increases thrust
 * -1 = positive input decreases thrust
 */
static const float THRUSTER_MATRIX[6][8] = {
    {  1,  1,  1,  1, -1, -1, -1, -1 },  // Surge: front thrusters forward, back thrusters reverse
    {  1, -1,  1, -1,  1, -1,  1, -1 },  // Sway: right side positive, left side negative
    { -1, -1,  1,  1, -1, -1,  1,  1 },  // Heave: bottom thrusters positive, top negative
    { -1,  1,  1, -1, -1,  1,  1, -1 },  // Roll: left up, right down
    {  1,  1, -1, -1, -1, -1,  1,  1 },  // Pitch: front up, back down
    { -1,  1, -1,  1,  1, -1,  1, -1 }   // Yaw: diagonal pairs for rotation
};

/*
 * Motor direction correction: accounts for physical mounting orientation.
 * +1 = motor mounted so positive duty = forward thrust
 * -1 = motor mounted reversed (flip sign)
 * 
 * TODO: Set these based on your actual motor mounting directions.
 *       Test each motor individually and flip the sign if it spins the wrong way.
 *       Index: 0=FR-Top, 1=FL-Top, 2=FR-Bot, 3=FL-Bot,
 *              4=BR-Top, 5=BL-Top, 6=BR-Bot, 7=BL-Bot
 */
static const float MOTOR_DIRECTION[8] = {
    +1, +1, +1, +1, +1, +1, +1, +1
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
    for (int i = 0; i < 8; i++) {
        float sum = 0.0f;
        for (int axis = 0; axis < 6; axis++) {
            sum += THRUSTER_MATRIX[axis][i] * inputs[axis];
        }
        /* Apply motor direction correction, then scale and clamp */
        output->thruster[i] = clamp_f(MOTOR_DIRECTION[i] * sum * MAX_DUTY, -MAX_DUTY, MAX_DUTY);
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
        LOG_INF("T[FR-t:%+3d FL-t:%+3d FR-b:%+3d FL-b:%+3d BR-t:%+3d BL-t:%+3d BR-b:%+3d BL-b:%+3d]%%",
                t_pct[0], t_pct[1], t_pct[2], t_pct[3], 
                t_pct[4], t_pct[5], t_pct[6], t_pct[7]);
    }
}

void thruster_send_outputs(const thruster_output_t *output)
{
    /* Thruster 0 (FR-Top) is connected directly via UART */
    vesc_set_duty_local(output->thruster[0]);
    
    /* Thrusters 1-7 are connected via CAN bus */
    vesc_set_duty_can(THRUSTER_FR_TOP,     output->thruster[1]);  // FL-Top
    vesc_set_duty_can(THRUSTER_BL_TOP,     output->thruster[2]);  // FR-Bottom
    vesc_set_duty_can(THRUSTER_BR_TOP,     output->thruster[3]);  // FL-Bottom
    vesc_set_duty_can(THRUSTER_FL_BOTTOM,  output->thruster[4]);  // BR-Top
    vesc_set_duty_can(THRUSTER_FR_BOTTOM,  output->thruster[5]);  // BL-Top
    vesc_set_duty_can(THRUSTER_BL_BOTTOM,  output->thruster[6]);  // BR-Bottom
    vesc_set_duty_can(THRUSTER_BR_BOTTOM,  output->thruster[7]);  // BL-Bottom
}