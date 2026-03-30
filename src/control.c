#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include "control.h"
#include "led.h"
#include "pid_controller.h"
#include "pid_config.h"
#include "axis_config.h"
#include "vn100s.h"
#include "vesc/thruster_mapping.h"
#include "vesc/vesc_uart_zephyr.h"

LOG_MODULE_REGISTER(rov_control, LOG_LEVEL_INF);

/* ---------------------------------------------------------------------------
 * Configuration
 * --------------------------------------------------------------------------- */
#define CONTROL_PERIOD_MS   20       /* 50 Hz control loop */
#define CONTROL_DT          0.02f    /* seconds */
#define COMMS_TIMEOUT_MS    2000     /* 2 s without UDP → kill thrusters */
#define MAX_RATE_DPS        45.0f    /* max joystick rate command (deg/s) */
#define MAX_SPEED_MPS       1.0f     /* max speed setpoint for surge/sway (m/s) */
#define MAX_DEPTH_RATE_MPS  0.5f     /* max depth rate from joystick (m/s) */
#define PID_OUTPUT_LIMIT    1.0f     /* PID output range ±1.0 (maps to ±50% via mixing) */
#define SPEED_DECAY         0.995f   /* leaky integrator factor for accel→speed */
#define LOG_INTERVAL        25       /* log every 25 iterations = 500 ms */

/* ---------------------------------------------------------------------------
 * Thread / queue
 * --------------------------------------------------------------------------- */
K_THREAD_STACK_DEFINE(rov_control_stack, 4096);
static struct k_thread rov_control_thread_data;

K_MSGQ_DEFINE(rov_command_queue, sizeof(rov_command_t), 10, 4);

/* ---------------------------------------------------------------------------
 * Pilot setpoints (raw joystick, written by UDP rx, read by control loop)
 * --------------------------------------------------------------------------- */
static struct {
    int8_t surge;
    int8_t sway;
    int8_t heave;
    int8_t roll;
    int8_t pitch;
    int8_t yaw;
    uint8_t light;
    uint8_t manipulator;
} pilot = {0};

K_MUTEX_DEFINE(pilot_mutex);

/* Timestamp of most recent command arrival (ms) */
static int64_t last_cmd_time;

/* ---------------------------------------------------------------------------
 * PID controllers — one per DOF
 * --------------------------------------------------------------------------- */
static pid_controller_t pid[PID_AXIS_COUNT];

/* Angle setpoints for roll / pitch / yaw (degrees, integrated from stick) */
static float angle_setpoint[3];          /* [0]=roll [1]=pitch [2]=yaw */

/* Estimated speed for surge / sway (m/s, integrated from accelerometer) */
static float est_speed[2];               /* [0]=surge(x) [1]=sway(y) */

/* Depth setpoint (m, integrated from stick) — sensor stub for now */
static float depth_setpoint;

/* ---------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------- */

/* Wrap angle difference to (-180, +180] */
static float wrap_180(float angle)
{
    while (angle > 180.0f)  angle -= 360.0f;
    while (angle <= -180.0f) angle += 360.0f;
    return angle;
}

/* Normalize int8_t joystick value to -1.0 … +1.0 */
static inline float stick_normalize(int8_t v)
{
    return (float)v / 127.0f;
}

/* Read depth sensor — stub: returns 0 until real sensor is integrated */
static float depth_sensor_read(void)
{
    return 0.0f;
}

/* ---------------------------------------------------------------------------
 * Sync PID gains from the UDP-configurable store
 * --------------------------------------------------------------------------- */
static void sync_pid_gains(void)
{
    for (int i = 0; i < PID_AXIS_COUNT; i++) {
        pid_gains_t g = pid_config_get_gains((enum pid_axis)i);
        pid_set_gains(&pid[i], g.kp, g.ki, g.kd);
    }
}

/* ---------------------------------------------------------------------------
 * Core stabilisation step — called every CONTROL_DT
 *
 * Produces 6 float outputs in [-1, +1] for the mixing matrix.
 * --------------------------------------------------------------------------- */
static void stabilise(float out[6])
{
    /* ---- Read sensors ---- */
    float raw_yaw, raw_pitch, raw_roll;
    vn100s_get_ypr(&raw_yaw, &raw_pitch, &raw_roll);

    /* Apply axis remapping (configured from topside) so PID sees the
     * correct orientation even if the IMU is mounted non-standard. */
    float yaw_meas, pitch_meas, roll_meas;
    axis_config_remap_ypr(raw_yaw, raw_pitch, raw_roll,
                          &yaw_meas, &pitch_meas, &roll_meas);

    float raw_ax, raw_ay, raw_az;
    vn100s_get_accel(&raw_ax, &raw_ay, &raw_az);

    /* Apply accelerometer axis remapping */
    float ax, ay, az;
    axis_config_remap_accel(raw_ax, raw_ay, raw_az, &ax, &ay, &az);

    /* Compensate for centripetal acceleration due to IMU offset from
     * center of mass.  When the ROV rotates, an off-center IMU sees
     * centripetal accel a_c = omega^2 * r.  We subtract it so the
     * PID's speed estimate reflects true translational motion. */
    imu_offset_t off = axis_config_get_offset();
    if (off.x != 0.0f || off.y != 0.0f || off.z != 0.0f) {
        float yr_rad, pr_rad, rr_rad;
        vn100s_get_rates(&yr_rad, &pr_rad, &rr_rad);
        /* Convert deg/s to rad/s */
        const float DEG2RAD = 0.017453293f;
        yr_rad *= DEG2RAD;
        pr_rad *= DEG2RAD;
        rr_rad *= DEG2RAD;
        /* Offset in metres (stored as mm) */
        float rx = off.x * 0.001f;
        float ry = off.y * 0.001f;
        float rz = off.z * 0.001f;
        /* Centripetal: a_centripetal = omega x (omega x r)
         * Simplified per-axis dominant terms: */
        ax -= (pr_rad * pr_rad + yr_rad * yr_rad) * rx;
        ay -= (rr_rad * rr_rad + yr_rad * yr_rad) * ry;
        az -= (rr_rad * rr_rad + pr_rad * pr_rad) * rz;
    }

    float depth_meas = depth_sensor_read();

    /* ---- Grab pilot stick snapshot ---- */
    int8_t p_surge, p_sway, p_heave, p_roll, p_pitch, p_yaw;
    k_mutex_lock(&pilot_mutex, K_FOREVER);
    p_surge = pilot.surge;
    p_sway  = pilot.sway;
    p_heave = pilot.heave;
    p_roll  = pilot.roll;
    p_pitch = pilot.pitch;
    p_yaw   = pilot.yaw;
    k_mutex_unlock(&pilot_mutex);

    /* ---- Sync latest PID gains from topside ---- */
    sync_pid_gains();

    /* ================================================================
     *  ROLL / PITCH / YAW  —  angle-tracking PID
     *
     *  Stick → rate (deg/s) → integrate → angle setpoint
     *  PID(angle_error) → output
     *  Bypass if gains == 0: passthrough raw stick
     * ================================================================ */

    /* Roll */
    if (pid_is_disabled(&pid[PID_ROLL])) {
        out[3] = stick_normalize(p_roll);
        /* Keep setpoint tracking current angle so switching to PID is bumpless */
        angle_setpoint[0] = roll_meas;
        pid_reset(&pid[PID_ROLL]);
    } else {
        angle_setpoint[0] += stick_normalize(p_roll) * MAX_RATE_DPS * CONTROL_DT;
        /* Virtual setpoint near measurement so derivative-on-measurement works,
         * while wrap_180 gives the correct shortest-path error. */
        float sp = roll_meas + wrap_180(angle_setpoint[0] - roll_meas);
        out[3] = pid_compute(&pid[PID_ROLL], sp, roll_meas, CONTROL_DT);
    }

    /* Pitch — negate measurement: IMU positive = nose up, thruster positive = nose down */
    if (pid_is_disabled(&pid[PID_PITCH])) {
        out[4] = stick_normalize(p_pitch);
        angle_setpoint[1] = -pitch_meas;
        pid_reset(&pid[PID_PITCH]);
    } else {
        angle_setpoint[1] += stick_normalize(p_pitch) * MAX_RATE_DPS * CONTROL_DT;
        float meas = -pitch_meas;
        float sp = meas + wrap_180(angle_setpoint[1] - meas);
        out[4] = pid_compute(&pid[PID_PITCH], sp, meas, CONTROL_DT);
    }

    /* Yaw */
    if (pid_is_disabled(&pid[PID_YAW])) {
        out[5] = stick_normalize(p_yaw);
        angle_setpoint[2] = yaw_meas;
        pid_reset(&pid[PID_YAW]);
    } else {
        angle_setpoint[2] += stick_normalize(p_yaw) * MAX_RATE_DPS * CONTROL_DT;
        angle_setpoint[2] = wrap_180(angle_setpoint[2]);
        float sp = yaw_meas + wrap_180(angle_setpoint[2] - yaw_meas);
        out[5] = pid_compute(&pid[PID_YAW], sp, yaw_meas, CONTROL_DT);
    }

    /* ================================================================
     *  SURGE / SWAY  —  speed-tracking PID (accelerometer integration)
     *
     *  Leaky-integrate accel → estimated speed
     *  Stick → speed setpoint
     *  PID(speed_error) → output
     *  Bypass if gains == 0: passthrough raw stick
     * ================================================================ */

    /* Update estimated speeds (leaky integrator to limit drift) */
    est_speed[0] = est_speed[0] * SPEED_DECAY + ax * CONTROL_DT;
    est_speed[1] = est_speed[1] * SPEED_DECAY + ay * CONTROL_DT;

    /* Surge */
    if (pid_is_disabled(&pid[PID_SURGE])) {
        out[0] = stick_normalize(p_surge);
        est_speed[0] = 0.0f;
        pid_reset(&pid[PID_SURGE]);
    } else {
        float sp = stick_normalize(p_surge) * MAX_SPEED_MPS;
        out[0] = pid_compute(&pid[PID_SURGE], sp, est_speed[0], CONTROL_DT);
    }

    /* Sway */
    if (pid_is_disabled(&pid[PID_SWAY])) {
        out[1] = stick_normalize(p_sway);
        est_speed[1] = 0.0f;
        pid_reset(&pid[PID_SWAY]);
    } else {
        float sp = stick_normalize(p_sway) * MAX_SPEED_MPS;
        out[1] = pid_compute(&pid[PID_SWAY], sp, est_speed[1], CONTROL_DT);
    }

    /* ================================================================
     *  HEAVE  —  depth-tracking PID (stub sensor for now)
     *
     *  Stick → depth rate → integrate → depth setpoint
     *  PID(depth_error) → output
     *  Bypass if gains == 0: passthrough raw stick
     * ================================================================ */

    /* Heave — negate measurement: positive depth = deeper, thruster positive = up */
    if (pid_is_disabled(&pid[PID_HEAVE])) {
        out[2] = stick_normalize(p_heave);
        depth_setpoint = -depth_meas;
        pid_reset(&pid[PID_HEAVE]);
    } else {
        depth_setpoint += stick_normalize(p_heave) * MAX_DEPTH_RATE_MPS * CONTROL_DT;
        out[2] = pid_compute(&pid[PID_HEAVE], depth_setpoint, -depth_meas, CONTROL_DT);
    }
}

/* ---------------------------------------------------------------------------
 * Light / manipulator stubs
 * --------------------------------------------------------------------------- */
static void rov_set_light(uint8_t brightness)
{
    if (brightness > 0) {
        LOG_DBG("Light: %d%% (%d/255)", (brightness * 100) / 255, brightness);
    }
}

static void rov_set_manipulator(uint8_t position)
{
    if (position > 0) {
        LOG_DBG("Manipulator: %d", position);
    }
}

/* ---------------------------------------------------------------------------
 * Control thread
 * --------------------------------------------------------------------------- */
static void rov_control_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    rov_command_t command;
    int64_t next_send_time = k_uptime_get();
    int log_counter = 0;

    LOG_INF("ROV control thread started (50 Hz, PID stabilisation)");

    while (1) {
        /* --- Dequeue new pilot commands (non-blocking) --- */
        while (k_msgq_get(&rov_command_queue, &command, K_NO_WAIT) == 0) {
            k_mutex_lock(&pilot_mutex, K_FOREVER);
            pilot.surge       = command.surge;
            pilot.sway        = command.sway;
            pilot.heave       = command.heave;
            pilot.roll        = command.roll;
            pilot.pitch       = command.pitch;
            pilot.yaw         = command.yaw;
            pilot.light       = command.light;
            pilot.manipulator = command.manipulator;
            k_mutex_unlock(&pilot_mutex);

            last_cmd_time = k_uptime_get();
            gpio_pin_toggle_dt(&led);
        }

        /* --- Comms timeout check --- */
        int64_t now = k_uptime_get();
        if ((now - last_cmd_time) > COMMS_TIMEOUT_MS) {
            /* Kill all thrusters */
            static const float zeros[6] = {0};
            thruster_output_t output;
            thruster_calculate_6dof(zeros, &output);
            thruster_send_outputs(&output);
        } else {
            /* --- Run stabilisation and send to thrusters --- */
            float dof_out[6];
            stabilise(dof_out);

            /* --- Periodic PID debug logging (every 500 ms) --- */
            if (++log_counter >= LOG_INTERVAL) {
                log_counter = 0;

                float y, p, r;
                vn100s_get_ypr(&y, &p, &r);

                /* Roll / Pitch / Yaw: setpoint(sp), measured(ms), output(o) */
                LOG_INF("R sp:%d ms:%d o:%d | P sp:%d ms:%d o:%d | Y sp:%d ms:%d o:%d",
                        (int)angle_setpoint[0], (int)r, (int)(dof_out[3] * 100),
                        (int)angle_setpoint[1], (int)p, (int)(dof_out[4] * 100),
                        (int)angle_setpoint[2], (int)y, (int)(dof_out[5] * 100));

                /* Surge / Sway / Heave output + estimated speeds (mm/s) */
                LOG_INF("Su:%d%% Sw:%d%% Hv:%d%% | spd[%d %d]mm/s",
                        (int)(dof_out[0] * 100),
                        (int)(dof_out[1] * 100),
                        (int)(dof_out[2] * 100),
                        (int)(est_speed[0] * 1000),
                        (int)(est_speed[1] * 1000));
            }

            thruster_output_t output;
            thruster_calculate_6dof(dof_out, &output);
            thruster_send_outputs(&output);

            /* Peripherals */
            k_mutex_lock(&pilot_mutex, K_FOREVER);
            if (pilot.light > 0) {
                rov_set_light(pilot.light);
            }
            if (pilot.manipulator > 0) {
                rov_set_manipulator(pilot.manipulator);
            }
            k_mutex_unlock(&pilot_mutex);
        }

        /* --- Sleep until next period --- */
        next_send_time += CONTROL_PERIOD_MS;
        k_sleep(K_TIMEOUT_ABS_MS(next_send_time));
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

void rov_control_init(void)
{
    LOG_INF("Initializing ROV 6DOF control system with PID...");

    /* Initialize VESC UART */
    int ret = vesc_uart_init();
    if (ret < 0) {
        LOG_ERR("Failed to initialize VESC UART: %d", ret);
        return;
    }

    /* Initialize all PID controllers (gains start at 0 → bypass mode) */
    for (int i = 0; i < PID_AXIS_COUNT; i++) {
        pid_init(&pid[i], 0.0f, 0.0f, 0.0f, -PID_OUTPUT_LIMIT, PID_OUTPUT_LIMIT);
    }

    /* Zero state */
    for (int i = 0; i < 3; i++) angle_setpoint[i] = 0.0f;
    for (int i = 0; i < 2; i++) est_speed[i] = 0.0f;
    depth_setpoint = 0.0f;
    last_cmd_time = 0;

    LOG_INF("ROV control system initialized (50 Hz, PID stabilisation)");
}

void rov_control_start(void)
{
    k_tid_t tid = k_thread_create(&rov_control_thread_data,
                                  rov_control_stack,
                                  K_THREAD_STACK_SIZEOF(rov_control_stack),
                                  rov_control_thread,
                                  NULL, NULL, NULL,
                                  K_PRIO_COOP(8), 0, K_NO_WAIT);
    if (tid) {
        LOG_INF("ROV control thread started");
    } else {
        LOG_ERR("Failed to start ROV control thread");
    }
}

void rov_send_command(uint32_t sequence, uint64_t payload)
{
    rov_command_t command;

    command.sequence    = sequence;
    command.surge       = (int8_t)((payload >> 0)  & 0xFF) - 128;
    command.sway        = (int8_t)((payload >> 8)  & 0xFF) - 128;
    command.heave       = (int8_t)((payload >> 16) & 0xFF) - 128;
    command.roll        = (int8_t)((payload >> 24) & 0xFF) - 128;
    command.pitch       = (int8_t)((payload >> 32) & 0xFF) - 128;
    command.yaw         = (int8_t)((payload >> 40) & 0xFF) - 128;
    command.light       = (uint8_t)((payload >> 48) & 0xFF);
    command.manipulator = (uint8_t)((payload >> 56) & 0xFF);

    if (k_msgq_put(&rov_command_queue, &command, K_NO_WAIT) != 0) {
        LOG_WRN("ROV command queue full! Command #%u dropped", sequence);
    }
}
