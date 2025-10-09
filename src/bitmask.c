#include "bitmask.h"

static volatile uint64_t g_current_bm = 0;

uint64_t bm_set_field(uint64_t bm, bm_field_t off, uint8_t v) {
    const uint64_t mask = (uint64_t)0xFF << off;
    bm = (bm & ~mask) | ((uint64_t)v << off);
    return bm;
}

uint8_t bm_get_field(uint64_t bm, bm_field_t off) {
    return (uint8_t)((bm >> off) & 0xFF);
}

void bm_to_bytes_le(uint64_t bm, uint8_t out[8]) {
    for (int i = 0; i < 8; ++i) out[i] = (uint8_t)((bm >> (8 * i)) & 0xFF);
}

uint64_t bm_from_bytes_le(const uint8_t in[8]) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= ((uint64_t)in[i]) << (8 * i);
    return v;
}

/* global holder */
void bm_set_current(uint64_t v) { g_current_bm = v; }
uint64_t bm_get_current(void)   { return g_current_bm; }
