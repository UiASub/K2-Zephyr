#include "pinger_adc.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pinger_adc, LOG_LEVEL_INF);

#define ADC_NODE DT_PATH(zephyr_user)
#define PINGER_ADC_STACK_SIZE 2048
#define PINGER_ADC_PRIORITY 7

static const struct adc_dt_spec adc_chan =
    ADC_DT_SPEC_GET_BY_IDX(ADC_NODE, 0);

static K_THREAD_STACK_DEFINE(pinger_adc_stack, PINGER_ADC_STACK_SIZE);
static struct k_thread pinger_adc_thread_data;

static void pinger_adc_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    int16_t sample = 0;

    struct adc_sequence sequence = {
        .buffer = &sample,
        .buffer_size = sizeof(sample),
    };

    int ret;

    if (!adc_is_ready_dt(&adc_chan)) {
        LOG_ERR("ADC device not ready");
        return;
    }

    ret = adc_channel_setup_dt(&adc_chan);
    if (ret < 0) {
        LOG_ERR("ADC channel setup failed: %d", ret);
        return;
    }

    ret = adc_sequence_init_dt(&adc_chan, &sequence);
    if (ret < 0) {
        LOG_ERR("ADC sequence init failed: %d", ret);
        return;
    }

    while (1) {
        sample = 0;

        ret = adc_read(adc_chan.dev, &sequence);
        if (ret < 0) {
            LOG_ERR("ADC read failed: %d", ret);
        } else {
            int32_t mv = sample;

            ret = adc_raw_to_millivolts_dt(&adc_chan, &mv);
            if (ret == 0) {
                LOG_INF("Pinger ADC raw=%d, voltage=%d mV", sample, mv);
            } else {
                LOG_INF("Pinger ADC raw=%d", sample);
            }
        }

        k_sleep(K_MSEC(500));
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