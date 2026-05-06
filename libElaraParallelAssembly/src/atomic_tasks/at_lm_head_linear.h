// src/atomic_tasks/at_lm_head_linear.h
#pragma once
#include <stdint.h>

#include "../memory/epa_ghs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AT_LM_HEAD_OK   0
#define AT_LM_HEAD_ERR -1

// Flags
#define AT_LM_HEAD_F_NONE        0u
#define AT_LM_HEAD_F_HAS_BIAS    (1u << 0)
#define AT_LM_HEAD_F_INLINE_W    (1u << 1)
#define AT_LM_HEAD_F_INLINE_B    (1u << 2)

// Weight layout (row-major):
//   W: [vocab_size][d_model]    (i.e., output rows = vocab)
// Bias layout:
//   B: [vocab_size]
//
// X layout:
//   X: [n_tokens][d_model]
// Logits layout:
//   logits: [n_tokens][vocab_size]
//
// Payload layout (v1):
//   [AtLmHeadHdr_v1]
//   [X floats]         n_tokens*d_model
//   [logits floats]    n_tokens*vocab_size
//   if INLINE_W:
//      [W floats]      vocab_size*d_model
//   if HAS_BIAS and INLINE_B:
//      [B floats]      vocab_size
//
// If not inline, W/B are resolved via provider callbacks by id.

typedef struct AtLmHeadHdr_v1 {
    uint32_t flags;

    uint32_t n_tokens;
    uint32_t d_model;
    uint32_t vocab_size;

    // external ids (ignored if INLINE)
    uint32_t w_id;
    uint32_t b_id;

    uint32_t rsv0;
    uint32_t rsv1;
} AtLmHeadHdr_v1;

// Provider API (weak stubs in .c; override in runtime)
const float *at_ro_lm_head_weight_get(uint32_t weight_id, uint32_t *out_rows, uint32_t *out_cols);
const float *at_ro_lm_head_bias_get(uint32_t bias_id, uint32_t *out_len);

int at_lm_head_linear_run(epa_ghs_t *ghs, epa_ghs_handle_t h);

#ifdef __cplusplus
}
#endif
