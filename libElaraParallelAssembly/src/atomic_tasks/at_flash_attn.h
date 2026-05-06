// atomic_tasks/at_flash_attn.h
#pragma once
#include <stdint.h>

#include "../memory/epa_ghs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Return codes
#define AT_FLASH_ATTN_OK   0
#define AT_FLASH_ATTN_ERR -1

// Flags
#define AT_FLASH_ATTN_F_CAUSAL (1u << 0)
// If set, apply scale = 1/sqrt(head_dim) to dot products
#define AT_FLASH_ATTN_F_SCALE  (1u << 1)

// Header packed at start of the GHS payload
typedef struct AtFlashAttnHdr_v1 {
    uint32_t flags;
    uint32_t n_heads;
    uint32_t head_dim;
    uint32_t seqlen_q;
    uint32_t seqlen_k;
    // reserved for future (offsets/strides/types/etc.)
    uint32_t rsv0, rsv1, rsv2, rsv3;
} AtFlashAttnHdr_v1;

// Entry point: run flash attention on a single packed payload handle.
int at_flash_attn_run(epa_ghs_t *ghs, epa_ghs_handle_t h);

#ifdef __cplusplus
}
#endif
