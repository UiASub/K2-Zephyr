#include "pinger_adc.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#include <stdint.h>

LOG_MODULE_REGISTER(pinger_adc, LOG_LEVEL_INF);

#define ADC_NODE DT_PATH(zephyr_user)
#define PINGER_ADC_STACK_SIZE 4096
#define PINGER_ADC_PRIORITY 7

/*
 * Prototype detector settings.
 *
 * Target sample rate: 100 kS/s
 * N = 256 samples -> 2.56 ms window
 *
 * Goertzel bin spacing:
 * fs / N = 100000 / 256 = 390.625 Hz/bin
 *
 * 20 kHz test target:
 * k = round(20000 / 390.625) = 51
 * actual bin = 51 * 390.625 = 19922 Hz
 */

#define SAMPLE_RATE_HZ      100000
#define SAMPLE_PERIOD_US    10
#define N_SAMPLES           256

#define DETECT_RATIO_X10    30   /* 100 = 10.0x power ratio */

/*
 * Goertzel coefficients in Q15 fixed point:
 * coeff = 2*cos(2*pi*k/N) * 32768
 *
 * k=38 -> 14.844 kHz
 * k=51 -> 19.922 kHz
 * k=64 -> 25.000 kHz
 */
#define GOERTZEL_COEFF_15K_Q15   39040
#define GOERTZEL_COEFF_20K_Q15   20557
#define GOERTZEL_COEFF_25K_Q15   0

static const struct adc_dt_spec adc_chan =
    ADC_DT_SPEC_GET_BY_IDX(ADC_NODE, 0);

static K_THREAD_STACK_DEFINE(pinger_adc_stack, PINGER_ADC_STACK_SIZE);
static struct k_thread pinger_adc_thread_data;

static int16_t samples[N_SAMPLES];

static uint64_t goertzel_power_q15(const int16_t *x, int len, int32_t coeff_q15)
{
    int64_t q0 = 0;
    int64_t q1 = 0;
    int64_t q2 = 0;

    for (int i = 0; i < len; i++) {
        q0 = (int64_t)x[i] + ((int64_t)coeff_q15 * q1 >> 15) - q2;
        q2 = q1;
        q1 = q0;
    }

    /*
     * Power = q1^2 + q2^2 - coeff*q1*q2
     * coeff is Q15, so shift product back down by 15.
     */
    int64_t power = (q1 * q1) + (q2 * q2) -
                    (((int64_t)coeff_q15 * q1 * q2) >> 15);

    if (power < 0) {
        return 0;
    }

    return (uint64_t)power;
}

static int adc_read_one(int16_t *sample)
{
    struct adc_sequence sequence = {
        .buffer = sample,
        .buffer_size = sizeof(*sample),
    };

    int ret = adc_sequence_init_dt(&adc_chan, &sequence);
    if (ret < 0) {
        return ret;
    }

    return adc_read(adc_chan.dev, &sequence);
}

static int collect_samples(void)
{
    int64_t sum = 0;

    for (int i = 0; i < N_SAMPLES; i++) {
        int16_t raw = 0;

        int ret = adc_read_one(&raw);
        if (ret < 0) {
            return ret;
        }

        samples[i] = raw;
        sum += raw;

        /*
         * Crude timing for prototype.
         * Later this should be replaced by timer-triggered ADC/DMA.
         */
        k_busy_wait(SAMPLE_PERIOD_US);
    }

    /*
     * Remove DC offset.
     * Your ADC signal is biased around ~1.5-1.7 V.
     * Goertzel should see the AC signal, not the DC midpoint.
     */
    int16_t mean = (int16_t)(sum / N_SAMPLES);

    for (int i = 0; i < N_SAMPLES; i++) {
        samples[i] = samples[i] - mean;
    }

    return 0;
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

    LOG_INF("Pinger detector started: target=%d Hz, fs=%d Hz, N=%d",
            20000, SAMPLE_RATE_HZ, N_SAMPLES);

    while (1) {
        ret = collect_samples();
        if (ret < 0) {
            LOG_ERR("ADC sample collection failed: %d", ret);
            k_sleep(K_MSEC(500));
            continue;
        }

        uint64_t p15 = goertzel_power_q15(samples, N_SAMPLES, GOERTZEL_COEFF_15K_Q15);
        uint64_t p20 = goertzel_power_q15(samples, N_SAMPLES, GOERTZEL_COEFF_20K_Q15);
        uint64_t p25 = goertzel_power_q15(samples, N_SAMPLES, GOERTZEL_COEFF_25K_Q15);

        uint64_t noise = (p15 + p25) / 2U;

        if (noise == 0) {
            noise = 1;
        }

        uint64_t ratio_x10 = (p20 * 10U) / noise;
        bool detected = ratio_x10 >= DETECT_RATIO_X10;

        LOG_INF("Pinger TEST 20k: P15=%llu P20=%llu P25=%llu ratio=%llu.%01llu %s",
                (unsigned long long)p15,
                (unsigned long long)p20,
                (unsigned long long)p25,
                (unsigned long long)(ratio_x10 / 10U),
                (unsigned long long)(ratio_x10 % 10U),
                detected ? "DETECTED" : "");

        /*
         * Avoid flooding logs.
         * Detection window itself is ~2.56 ms, but for prototype logging
         * we slow down the print rate.
         */
        k_sleep(K_MSEC(100));
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