#ifndef MS5837_H
#define MS5837_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float depth_m;
    float pressure_mbar;
    float temperature_c;
    bool valid;
    int64_t age_ms;
    uint8_t addr;
    int last_error;
    uint32_t init_attempts;
    uint32_t read_errors;
} ms5837_sample_t;

int ms5837_init(void);
void ms5837_get_sample(ms5837_sample_t *sample);
float ms5837_get_depth_m(void);
void ms5837_task(void *p1, void *p2, void *p3);

#endif /* MS5837_H */
