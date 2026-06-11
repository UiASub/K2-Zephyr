#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float depth_m;
    float pressure_mbar;
    float temperature_c;
    uint32_t raw_pressure;
    uint32_t raw_temperature;
    int64_t timestamp_ms;
} depth_sensor_sample_t;

void depth_sensor_start(void);
bool depth_sensor_get_latest(depth_sensor_sample_t *out);
bool depth_sensor_get_depth_m(float *depth_m);
