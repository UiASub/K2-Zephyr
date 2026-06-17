#include "pinger_adc.h"


#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <zephyr/sys/time_units.h>
#include <stdbool.h>

LOG_MODULE_REGISTER(pinger_adc, LOG_LEVEL_INF);

#define ADC_NODE DT_PATH(zephyr_user)
#define PINGER_ADC_STACK_SIZE 4096
#define PINGER_ADC_PRIORITY 7

#define SAMPLE_RATE_HZ      712000.0f
#define N_SAMPLES           1024

#define TARGET_FREQ_HZ          30000.0f
#define LOWER_GUARD_FREQ_HZ     25000.0f
#define UPPER_GUARD_FREQ_HZ     35000.0f

#define DETECT_RATIO_X10        80          /* 8.0x */
#define DETECT_SHARE_X10        800         /* 80.0% */
#define DETECT_MIN_TARGET       30000000ULL /* weak absolute noise gate */

#define PING_CONFIRM_COUNT      1
#define PINGER_HOLD_MS          5000U
#define PING_PERIOD_MS          2000U
#define PING_PERIOD_TOL_MS      200U
#define CADENCE_NEEDED          3
#define PING_REFRACTORY_MS      500U

#define PULSE_HISTORY_COUNT 32U
#define PULSE_END_MS        250U

static uint16_t pulse_pp_history[PULSE_HISTORY_COUNT];
static uint32_t pulse_history_filled;
static uint32_t pulse_history_index;

static uint16_t current_pulse_peak_pp;
static bool current_pulse_clipped;
static uint32_t last_pulse_window_ms;
static bool pulse_active;

static uint16_t last_pulse_peak_pp;
static bool last_pulse_was_clipped;
static uint32_t last_pulse_rank;


static const struct adc_dt_spec adc_chan =
    ADC_DT_SPEC_GET_BY_IDX(ADC_NODE, 0);

static K_THREAD_STACK_DEFINE(pinger_adc_stack, PINGER_ADC_STACK_SIZE);
static struct k_thread pinger_adc_thread_data;

static int16_t samples[N_SAMPLES];
static int16_t last_raw_min;
static int16_t last_raw_mean;
static int16_t last_raw_max;
static int16_t last_raw_pp;
static bool last_raw_clipped;
static uint64_t target_noise_floor = 30000000ULL;

static uint64_t goertzel_power_float(const int16_t *x, int len, float freq_hz, float fs_hz)
{
    float w = 2.0f * 3.14159265359f * freq_hz / fs_hz;
    float coeff = 2.0f * cosf(w);

    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;

    for (int i = 0; i < len; i++) {
        q0 = (float)x[i] + coeff * q1 - q2;
        q2 = q1;
        q1 = q0;
    }

    float power = q1 * q1 + q2 * q2 - coeff * q1 * q2;

    if (power <= 0.0f) {
        return 0;
    }

    return (uint64_t)power;
}


static int adc_read_block(void)
{
    struct adc_sequence_options options = {
        .extra_samplings = N_SAMPLES - 1,
        .interval_us = 0,
    };

    struct adc_sequence sequence = {
        .buffer = samples,
        .buffer_size = sizeof(samples),
        .options = &options,
    };

    int ret = adc_sequence_init_dt(&adc_chan, &sequence);
    if (ret < 0) {
        return ret;
    }

    return adc_read(adc_chan.dev, &sequence);
}

static int collect_samples(uint32_t *cycles_used)
{
    uint32_t c0 = k_cycle_get_32();

    int ret = adc_read_block();

    uint32_t c1 = k_cycle_get_32();
    *cycles_used = c1 - c0;

    if (ret < 0) {
        return ret;
    }

    int64_t sum = 0;
    int16_t min_raw = INT16_MAX;
    int16_t max_raw = INT16_MIN;

    for (int i = 0; i < N_SAMPLES; i++) {
        int16_t raw = samples[i];

        sum += raw;

        if (raw < min_raw) {
            min_raw = raw;
        }

        if (raw > max_raw) {
            max_raw = raw;
        }
    }

    int16_t mean = (int16_t)(sum / N_SAMPLES);

    last_raw_min = min_raw;
    last_raw_mean = mean;
    last_raw_max = max_raw;
    last_raw_pp = max_raw - min_raw;

    last_raw_clipped =
        (min_raw <= 20) ||
        (max_raw >= 4075) ||
        (last_raw_pp >= 4070);

    for (int i = 0; i < N_SAMPLES; i++) {
        samples[i] = samples[i] - mean;
    }

    return 0;
}

static uint32_t rank_pulse_pp(uint16_t pp)
{
    uint32_t rank = 0;

    for (uint32_t i = 0; i < pulse_history_filled; i++) {
        if (pulse_pp_history[i] > pp) {
            rank++;
        }
    }

    return rank;
}


static void store_pulse_peak(uint16_t peak_pp, bool clipped)
{
    last_pulse_peak_pp = peak_pp;
    last_pulse_was_clipped = clipped;

    /*
     * If clipped, rank is not meaningful. It is simply "very strong".
     */
    if (clipped) {
        last_pulse_rank = 0;
    } else {
        last_pulse_rank = rank_pulse_pp(peak_pp);
    }

    /*
     * Store only non-clipped values in history.
     * Clipped values would poison the ranking because they all collapse to ~4095.
     */
    if (!clipped) {
        pulse_pp_history[pulse_history_index] = peak_pp;

        pulse_history_index++;
        if (pulse_history_index >= PULSE_HISTORY_COUNT) {
            pulse_history_index = 0;
        }

        if (pulse_history_filled < PULSE_HISTORY_COUNT) {
            pulse_history_filled++;
        }
    }
}


static void pinger_adc_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    if (!adc_is_ready_dt(&adc_chan)) {
        LOG_ERR("ADC device not ready");
        return;
    }

    int ret = adc_channel_setup_dt(&adc_chan);
    if (ret < 0) {
        LOG_ERR("ADC channel setup failed: %d", ret);
        return;
    }

    LOG_INF("Pinger detector started: target=%u Hz, fixed_fs=%u Hz, N=%d",
        (unsigned int)TARGET_FREQ_HZ,
        (unsigned int)SAMPLE_RATE_HZ,
        N_SAMPLES);

    int ping_confirm_count = 0;
    uint32_t last_ping_ms = 0;
    uint32_t prev_candidate_ping_ms = 0;
    uint32_t last_candidate_ping_ms = 0;
    uint32_t last_cadence_dt_ms = 0;
    int cadence_hits = 0;
    while (1) {
        uint32_t cycles_used = 0;

        ret = collect_samples(&cycles_used);
        if (ret < 0) {
            LOG_ERR("ADC sample collection failed: %d", ret);
            k_sleep(K_MSEC(500));
            continue;
        }

    
        float fs_for_detection_hz = SAMPLE_RATE_HZ;

        uint64_t p_low = goertzel_power_float(samples, N_SAMPLES, LOWER_GUARD_FREQ_HZ, fs_for_detection_hz);
        uint64_t p_target = goertzel_power_float(samples, N_SAMPLES, TARGET_FREQ_HZ, fs_for_detection_hz);
        uint64_t p_high = goertzel_power_float(samples, N_SAMPLES, UPPER_GUARD_FREQ_HZ, fs_for_detection_hz);

        uint64_t noise = (p_low + p_high) / 2U;

        if (noise == 0) {
            noise = 1;
        }

        uint64_t ratio_x10 = (p_target * 10U) / noise;

        uint64_t total_power = p_low + p_target + p_high;
        uint64_t target_share_x10 = 0;
        uint64_t adaptive_gate = (target_noise_floor * 5U) + DETECT_MIN_TARGET;

        if (total_power > 0) {
            target_share_x10 = (p_target * 1000U) / total_power;
        }

        bool tone_shape =
            (ratio_x10 >= DETECT_RATIO_X10) &&
            (target_share_x10 >= DETECT_SHARE_X10);
        
        
        bool ping_window =
            tone_shape &&
            (p_target >= adaptive_gate);

        uint32_t now_ms = k_uptime_get_32();
        if (ping_window) {
            if (!pulse_active) {
                pulse_active = true;
                current_pulse_peak_pp = 0;
                current_pulse_clipped = false;
            }

            if ((uint16_t)last_raw_pp > current_pulse_peak_pp) {
                current_pulse_peak_pp = (uint16_t)last_raw_pp;
            }

            if (last_raw_clipped) {
                current_pulse_clipped = true;
            }

            last_pulse_window_ms = k_uptime_get_32();
        }

        if (pulse_active && !ping_window) {
            uint32_t since_pulse_ms = now_ms - last_pulse_window_ms;

            if (since_pulse_ms >= PULSE_END_MS) {
                store_pulse_peak(current_pulse_peak_pp, current_pulse_clipped);
                pulse_active = false;
            }
        }

        if (ping_window) {
            /*
            * Debounce/refractory:
            * Treat repeated detection windows close together as the same burst.
            */
            bool new_candidate =
                (last_candidate_ping_ms == 0U) ||
                ((now_ms - last_candidate_ping_ms) >= PING_REFRACTORY_MS);

            if (new_candidate) {
                last_candidate_ping_ms = now_ms;

                if (ping_confirm_count < PING_CONFIRM_COUNT) {
                    ping_confirm_count++;
                }

                if (ping_confirm_count >= PING_CONFIRM_COUNT) {
                    if (prev_candidate_ping_ms == 0U) {
                        /*
                        * First candidate establishes the cadence phase.
                        */
                        prev_candidate_ping_ms = now_ms;
                        last_cadence_dt_ms = 0U;
                    } else {
                        uint32_t dt = now_ms - prev_candidate_ping_ms;
                        last_cadence_dt_ms = dt;

                        bool one_period =
                            (dt >= (PING_PERIOD_MS - PING_PERIOD_TOL_MS)) &&
                            (dt <= (PING_PERIOD_MS + PING_PERIOD_TOL_MS));

                        bool two_periods =
                            (dt >= ((2U * PING_PERIOD_MS) - PING_PERIOD_TOL_MS)) &&
                            (dt <= ((2U * PING_PERIOD_MS) + PING_PERIOD_TOL_MS));

                        if (one_period || two_periods) {
                            /*
                            * Candidate landed on beat.
                            * Only now do we re-anchor the phase.
                            */
                            if (cadence_hits < CADENCE_NEEDED) {
                                cadence_hits++;
                            }

                            prev_candidate_ping_ms = now_ms;
                        } else if (dt > ((2U * PING_PERIOD_MS) + PING_PERIOD_TOL_MS)) {
                            /*
                            * Long gap: old train is gone. Restart cadence.
                            */
                            cadence_hits = 0;
                            prev_candidate_ping_ms = now_ms;
                        } else {
                            /*
                            * Too-soon burst: likely interference.
                            * Ignore it and keep the old phase anchor.
                            */
                        }
                    }

                    /*
                    * Only cadence-confirmed ping trains refresh DETECTED hold.
                    */
                    if (cadence_hits >= CADENCE_NEEDED) {
                        last_ping_ms = now_ms;
                    }
                }
            }
        } else {
            ping_confirm_count = 0;
        }

        if (!tone_shape) {
            if (p_target > target_noise_floor) {
                target_noise_floor += (p_target - target_noise_floor) / 64U;
            } else {
                target_noise_floor -= (target_noise_floor - p_target) / 64U;
            }
        }

        bool detected = false;
        uint32_t ping_age_ms = 0;

        if (last_ping_ms != 0U) {
            ping_age_ms = now_ms - last_ping_ms;
            detected = ping_age_ms < PINGER_HOLD_MS;
        }

        const char *strength_state;

        if (last_raw_clipped) {
            strength_state = "CLIP";
        } else if (last_raw_pp > 2500) {
            strength_state = "HIGH";
        } else if (last_raw_pp > 1000) {
            strength_state = "MED";
        } else if (last_raw_pp > 300) {
            strength_state = "LOW";
        } else {
            strength_state = "TINY";
        }

        static bool prev_detected = false;
        static uint32_t last_log_ms = 0;
        bool should_log =
            ping_window ||
            (detected != prev_detected) ||
            ((now_ms - last_log_ms) >= 1000U);

        if (should_log) {
            prev_detected = detected;
            last_log_ms = now_ms;

            LOG_INF("\n ==== Hydrophone: \n %s, pp=%04d %s \n ====",
                    detected ? "DETECTED" : "LISTENING...",
                    last_raw_pp,
                    strength_state);
        }
        k_sleep(K_MSEC(1));
    }
}


void pinger_adc_start(void)
{
    k_tid_t tid = k_thread_create(&pinger_adc_thread_data,
                                  pinger_adc_stack,
                                  K_THREAD_STACK_SIZEOF(pinger_adc_stack),
                                  pinger_adc_thread,
                                  NULL, NULL, NULL,
                                  PINGER_ADC_PRIORITY,
                                  0,
                                  K_NO_WAIT);

    k_thread_name_set(tid, "pinger_adc");
}