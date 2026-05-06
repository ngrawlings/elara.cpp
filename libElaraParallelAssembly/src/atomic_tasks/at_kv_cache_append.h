// src/atomic_tasks/at_kv_cache_append.h
#pragma once
#include <stdint.h>

#include "../memory/epa_ghs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Return codes
#define AT_KV_OK    0
#define AT_KV_ERR  -1

// Flags (v1: none required yet)
#define AT_KV_F_NONE 0u

/*
Payload layout (v1, packed, offsets implicit):

[AtKvAppendHdr_v1]
[K_new floats]  size = append_len * n_heads * head_dim
[V_new floats]  size = append_len * n_heads * head_dim
[K_cache floats] size = capacity * n_heads * head_dim
[V_cache floats] size = capacity * n_heads * head_dim

Append copies K_new/V_new into cache at index [cur_len .. cur_len+append_len-1],
then increments cur_len.

All arrays are float32 for now.
*/

typedef struct AtKvAppendHdr_v1 {
    uint32_t flags;

    uint32_t n_heads;
    uint32_t head_dim;

    uint32_t capacity;     // total slots in KV cache
    uint32_t cur_len;      // current filled length (0..capacity)

    uint32_t append_len;   // how many new tokens to append (often 1)

    // reserved for future: explicit offsets, dtype, strides, etc.
    uint32_t rsv0, rsv1, rsv2;
} AtKvAppendHdr_v1;

int at_kv_cache_append_run(epa_ghs_t *ghs, epa_ghs_handle_t h);

#ifdef __cplusplus
}
#endif
