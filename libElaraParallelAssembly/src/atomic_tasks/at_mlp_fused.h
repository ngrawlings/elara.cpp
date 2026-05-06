// src/atomic_tasks/at_mlp_fused.h
#pragma once
#include <stdint.h>

#include "../memory/epa_ghs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Return codes
#define AT_MLP_OK   0
#define AT_MLP_ERR -1

// Flags
#define AT_MLP_F_NONE         0u
#define AT_MLP_F_HAS_BIAS     (1u << 0)  // bias vectors exist for projections
#define AT_MLP_F_INLINE_W     (1u << 1)  // all weights are inline in payload
#define AT_MLP_F_INLINE_B     (1u << 2)  // all bias are inline in payload
#define AT_MLP_F_HAS_SCRATCH  (1u << 3)  // scratch buffer present in payload (required for CPU ref here)

// Weight layouts (row-major):
//   Wg: [hidden_dim][d_model]
//   Wu: [hidden_dim][d_model]
//   Wd: [d_model][hidden_dim]
//
// X layout: [n_tokens][d_model]
// Y layout: [n_tokens][d_model]
//
// Payload layout (v1, implicit offsets):
//   [AtMlpFusedHdr_v1]
//   [X floats]  n_tokens*d_model
//   [Y floats]  n_tokens*d_model
//   if INLINE_W:
//      [Wg floats] hidden_dim*d_model
//      [Wu floats] hidden_dim*d_model
//      [Wd floats] d_model*hidden_dim
//   if HAS_BIAS and INLINE_B:
//      [Bg floats] hidden_dim
//      [Bu floats] hidden_dim
//      [Bd floats] d_model
//   if HAS_SCRATCH:
//      [ACT floats] n_tokens*hidden_dim   (silu(gate)*up)
//
// If not inline, weights/bias are resolved via provider callbacks by id.

typedef struct AtMlpFusedHdr_v1 {
    uint32_t flags;

    uint32_t n_tokens;
    uint32_t d_model;
    uint32_t hidden_dim;

    // external ids (ignored if INLINE)
    uint32_t w_gate_id;
    uint32_t w_up_id;
    uint32_t w_down_id;

    uint32_t b_gate_id;  // only used if HAS_BIAS and not INLINE_B
    uint32_t b_up_id;
    uint32_t b_down_id;

    uint32_t rsv0;
} AtMlpFusedHdr_v1;

// RO provider API (weak stubs in .c; override in runtime)
const float *at_ro_weight_get(uint32_t weight_id, uint32_t *out_rows, uint32_t *out_cols);
const float *at_ro_bias_get(uint32_t bias_id, uint32_t *out_len);

int at_mlp_fused_run(epa_ghs_t *ghs, epa_ghs_handle_t h);

#ifdef __cplusplus
}
#endif
