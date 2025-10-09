#pragma once
#include <stdint.h>

typedef enum {
    BM_FREMBAK   = 0,   
    BM_OPPNED    = 8,   
    BM_SIDESIDE  = 16, 
    BM_PITCH     = 24,  
    BM_YAW       = 32,  
    BM_ROLL      = 40,  
    BM_LYS       = 48, 
    BM_MANIP     = 56  
} bm_field_t;

/* pack/unpack single fields */
uint64_t bm_set_field(uint64_t bm, bm_field_t off, uint8_t v);
uint8_t  bm_get_field(uint64_t bm, bm_field_t off);

/* little-endian byte view helpers (for e.g. UART, not used by UDP) */
void     bm_to_bytes_le(uint64_t bm, uint8_t out[8]);
uint64_t bm_from_bytes_le(const uint8_t in[8]);

/* app-global “current” bitmask (simple holder for producer/consumer) */
void     bm_set_current(uint64_t v);
uint64_t bm_get_current(void);
