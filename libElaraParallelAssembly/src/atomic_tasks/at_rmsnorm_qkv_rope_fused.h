// src/atomic_tasks/at_rmsnorm_qkv_rope_fused.h
#pragma once
#include <stdint.h>

#include "../memory/epa_ghs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AT_RQRF_OK   0
#define AT_RQRF_ERR -1

// Flags
#define AT_RQRF_F_NONE         0u
#define AT_RQRF_F_HAS_BIAS     (1u << 0)

// Inline weights (for ctests / bringup)
#define AT_RQRF_F_INLINE_RMS_W (1u << 1)
#define AT_RQRF_F_INLINE_QKV_W (1u << 2)
#define AT_RQRF_F_INLINE_QKV_B (1u << 3)

// Layout choices
// If set, we store Q,K,V in the order: Q then K then V (recommended default).
#define AT_RQRF_F_QKV_ORDER_QKV (1u << 4)

// Header + payload layout (v1):
//
// [AtRmsnormQkvRopeHdr_v1]
// [positions u32]             n_tokens
// [X float32]                 n_tokens*d_model
// [Q float32]                 n_tokens*n_heads*head_dim
// [K float32]                 n_tokens*n_kv_heads*head_dim
// [V float32]                 n_tokens*n_kv_heads*head_dim
// [scratch_norm float32]      n_tokens*d_model   (optional but REQUIRED for this v1 impl)
//
// If INLINE_RMS_W:
//   [rms_w float32]           d_model
//
// If INLINE_QKV_W:
//   [qkv_w float32]           out_rows*d_model   where out_rows = (n_heads + 2*n_kv_heads)*head_dim
//
// If HAS_BIAS and INLINE_QKV_B:
//   [qkv_b float32]           out_rows
//
// Weight conventions:
// - RMS weight: [d_model]
// - QKV weight: row-major [out_rows][d_model]
//   rows are packed as: Q rows, then K rows, then V rows (QKV order)
//
// RoPE:
// - Applied to Q and K only.
// - Applied on first rotary_dim of each head vector (must be even, <= head_dim).
// - angle = pos * inv_freq(i)
//   inv_freq(i) = 1 / pow(theta, i/rotary_dim) where i indexes even dims (0,2,4,...)

typedef struct AtRmsnormQkvRopeHdr_v1 {
    uint32_t flags;

    uint32_t n_tokens;
    uint32_t d_model;

    uint32_t n_heads;      // query heads
    uint32_t n_kv_heads;   // kv heads (grouped query attention)
    uint32_t head_dim;     // per-head dim

    uint32_t rotary_dim;   // even, <= head_dim
    float    rms_eps;      // typical 1e-5

    float    rope_theta;   // typical 10000.0
    uint32_t w_rms_id;     // RO weight id (ignored if INLINE_RMS_W)
    uint32_t w_qkv_id;     // RO weight id (ignored if INLINE_QKV_W)
    uint32_t b_qkv_id;     // RO bias id (ignored if INLINE_QKV_B)

    uint32_t rsv0;
    uint32_t rsv1;
} AtRmsnormQkvRopeHdr_v1;

// RO provider API (weak stubs in .c; override in runtime)
const float *at_ro_rms_weight_get(uint32_t weight_id, uint32_t *out_len);
const float *at_ro_qkv_weight_get(uint32_t weight_id, uint32_t *out_rows, uint32_t *out_cols);
const float *at_ro_qkv_bias_get(uint32_t bias_id, uint32_t *out_len);

int at_rmsnorm_qkv_rope_fused_run(epa_ghs_t *ghs, epa_ghs_handle_t h);

#ifdef __cplusplus
}
#endif
