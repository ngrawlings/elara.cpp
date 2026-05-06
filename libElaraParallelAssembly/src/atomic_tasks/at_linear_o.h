// src/atomic_tasks/at_linear_o.h
#pragma once
#include <stdint.h>

#include "../memory/epa_ghs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Return codes
#define AT_LINEAR_O_OK   0
#define AT_LINEAR_O_ERR -1

// Flags
#define AT_LINEAR_O_F_NONE      0u
#define AT_LINEAR_O_F_HAS_BIAS  (1u << 0)
#define AT_LINEAR_O_F_INLINE_W  (1u << 1)  // W lives in payload (for testing)
#define AT_LINEAR_O_F_INLINE_B  (1u << 2)  // B lives in payload (for testing)

// Weight layout for W:
// W is row-major [out_dim][in_dim]
// so W[row*out_dim_stride + col] == W[row*in_dim + col]
//
// X layout:
// X is row-major [n_tokens][in_dim]
//
// Y layout:
// Y is row-major [n_tokens][out_dim]
//
// Default payload layout (v1, implicit offsets):
//   [AtLinearOHdr_v1]
//   [X floats] size = n_tokens * in_dim
//   [Y floats] size = n_tokens * out_dim
//   if AT_LINEAR_O_F_INLINE_W:
//      [W floats] size = out_dim * in_dim
//   if AT_LINEAR_O_F_INLINE_B:
//      [B floats] size = out_dim
//
// If not inline, W/B are resolved via provider callbacks by id.

typedef struct AtLinearOHdr_v1 {
    uint32_t flags;

    uint32_t n_tokens;   // usually seqlen_q
    uint32_t in_dim;     // usually n_heads * head_dim
    uint32_t out_dim;    // usually d_model

    uint32_t weight_id;  // external RO weight handle/id (ignored if INLINE_W)
    uint32_t bias_id;    // external RO bias handle/id (ignored if INLINE_B)

    // reserved (future: explicit offsets, dtype, strides, scaling, etc.)
    uint32_t rsv0, rsv1;
} AtLinearOHdr_v1;

/*
Read-only weight provider interface.

You will later back this with:
- GPU resident weights
- Elara silicon RO bank
- host-pinned memory
etc.

For now, CPU reference uses these callbacks.

Return:
  - pointer to float array, or NULL on missing
  - write rows/cols (for W) or len (for bias) if non-NULL

W expected shape:
  rows = out_dim, cols = in_dim

Bias expected length:
  len = out_dim
*/

// These are intentionally weak in the .c with default NULL-return stubs.
// You can provide real implementations in your runtime layer.
const float *at_ro_weight_matrix_get(uint32_t weight_id, uint32_t *out_rows, uint32_t *out_cols);
const float *at_ro_bias_vector_get(uint32_t bias_id, uint32_t *out_len);

int at_linear_o_run(epa_ghs_t *ghs, epa_ghs_handle_t h);

#ifdef __cplusplus
}
#endif
