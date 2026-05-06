// src/atomic_tasks/at_mlp_fused.c
#include "at_mlp_fused.h"

#include <stddef.h>
#include <string.h>
#include <math.h>

// ------------------------------------------------------------
// Weak stubs: you override these in your runtime to supply RO weights.
// ------------------------------------------------------------
__attribute__((weak))
const float *at_ro_weight_get(uint32_t weight_id, uint32_t *out_rows, uint32_t *out_cols) {
    (void)weight_id;
    if (out_rows) *out_rows = 0;
    if (out_cols) *out_cols = 0;
    return NULL;
}

__attribute__((weak))
const float *at_ro_bias_get(uint32_t bias_id, uint32_t *out_len) {
    (void)bias_id;
    if (out_len) *out_len = 0;
    return NULL;
}

static int mul_overflows_size_t(size_t a, size_t b) {
    if (a == 0 || b == 0) return 0;
    return a > (SIZE_MAX / b);
}

// silu(x) = x / (1 + exp(-x))
static inline float silu(float x) {
    return x / (1.0f + expf(-x));
}

int at_mlp_fused_run(epa_ghs_t *ghs, epa_ghs_handle_t h) {
    if (!ghs) return AT_MLP_ERR;

    void *base = NULL;
    epa_ghs_meta_t meta;
    if (epa_ghs_get_meta(ghs, h, &meta) != EPA_GHS_OK) return AT_MLP_ERR;
    if (epa_ghs_get_ptr(ghs, h, &base) != EPA_GHS_OK)  return AT_MLP_ERR;
    if (!base) return AT_MLP_ERR;

    if (meta.size_bytes < (uint32_t)sizeof(AtMlpFusedHdr_v1)) return AT_MLP_ERR;

    uint8_t *p = (uint8_t *)base;
    AtMlpFusedHdr_v1 *hdr = (AtMlpFusedHdr_v1 *)p;

    const uint32_t flags      = hdr->flags;
    const uint32_t n_tokens   = hdr->n_tokens;
    const uint32_t d_model    = hdr->d_model;
    const uint32_t hidden_dim = hdr->hidden_dim;

    if (n_tokens == 0 || d_model == 0 || hidden_dim == 0) return AT_MLP_ERR;

    if (mul_overflows_size_t((size_t)n_tokens, (size_t)d_model)) return AT_MLP_ERR;
    if (mul_overflows_size_t((size_t)n_tokens, (size_t)hidden_dim)) return AT_MLP_ERR;
    if (mul_overflows_size_t((size_t)hidden_dim, (size_t)d_model)) return AT_MLP_ERR;
    if (mul_overflows_size_t((size_t)d_model, (size_t)hidden_dim)) return AT_MLP_ERR;

    const size_t x_count   = (size_t)n_tokens * (size_t)d_model;
    const size_t y_count   = (size_t)n_tokens * (size_t)d_model;
    const size_t wg_count  = (size_t)hidden_dim * (size_t)d_model;
    const size_t wu_count  = (size_t)hidden_dim * (size_t)d_model;
    const size_t wd_count  = (size_t)d_model    * (size_t)hidden_dim;
    const size_t bg_count  = (size_t)hidden_dim;
    const size_t bu_count  = (size_t)hidden_dim;
    const size_t bd_count  = (size_t)d_model;
    const size_t act_count = (size_t)n_tokens * (size_t)hidden_dim;

    // For this CPU reference, require scratch to avoid malloc.
    if ((flags & AT_MLP_F_HAS_SCRATCH) == 0) return AT_MLP_ERR;

    // Compute expected payload size
    size_t bytes_needed = sizeof(AtMlpFusedHdr_v1)
        + x_count * sizeof(float)
        + y_count * sizeof(float);

    if (flags & AT_MLP_F_INLINE_W) {
        bytes_needed += (wg_count + wu_count + wd_count) * sizeof(float);
    }
    if ((flags & AT_MLP_F_HAS_BIAS) && (flags & AT_MLP_F_INLINE_B)) {
        bytes_needed += (bg_count + bu_count + bd_count) * sizeof(float);
    }
    // scratch ACT
    bytes_needed += act_count * sizeof(float);

    if ((size_t)meta.size_bytes < bytes_needed) return AT_MLP_ERR;

    // Layout decode
    p += sizeof(AtMlpFusedHdr_v1);

    float *X = (float *)p; p += x_count * sizeof(float);
    float *Y = (float *)p; p += y_count * sizeof(float);

    const float *Wg = NULL;
    const float *Wu = NULL;
    const float *Wd = NULL;

    const float *Bg = NULL;
    const float *Bu = NULL;
    const float *Bd = NULL;

    if (flags & AT_MLP_F_INLINE_W) {
        Wg = (const float *)p; p += wg_count * sizeof(float);
        Wu = (const float *)p; p += wu_count * sizeof(float);
        Wd = (const float *)p; p += wd_count * sizeof(float);
    } else {
        uint32_t r=0,c=0;

        Wg = at_ro_weight_get(hdr->w_gate_id, &r, &c);
        if (!Wg) return AT_MLP_ERR;
        if (r && c && (r != hidden_dim || c != d_model)) return AT_MLP_ERR;

        Wu = at_ro_weight_get(hdr->w_up_id, &r, &c);
        if (!Wu) return AT_MLP_ERR;
        if (r && c && (r != hidden_dim || c != d_model)) return AT_MLP_ERR;

        Wd = at_ro_weight_get(hdr->w_down_id, &r, &c);
        if (!Wd) return AT_MLP_ERR;
        if (r && c && (r != d_model || c != hidden_dim)) return AT_MLP_ERR;
    }

    if (flags & AT_MLP_F_HAS_BIAS) {
        if (flags & AT_MLP_F_INLINE_B) {
            Bg = (const float *)p; p += bg_count * sizeof(float);
            Bu = (const float *)p; p += bu_count * sizeof(float);
            Bd = (const float *)p; p += bd_count * sizeof(float);
        } else {
            uint32_t len = 0;

            Bg = at_ro_bias_get(hdr->b_gate_id, &len);
            if (!Bg) return AT_MLP_ERR;
            if (len && len != hidden_dim) return AT_MLP_ERR;

            Bu = at_ro_bias_get(hdr->b_up_id, &len);
            if (!Bu) return AT_MLP_ERR;
            if (len && len != hidden_dim) return AT_MLP_ERR;

            Bd = at_ro_bias_get(hdr->b_down_id, &len);
            if (!Bd) return AT_MLP_ERR;
            if (len && len != d_model) return AT_MLP_ERR;
        }
    }

    float *ACT = (float *)p;
    // p += act_count*sizeof(float); (not needed further)

    // ------------------------------------------------------------
    // Phase 1: compute ACT[t,h] = silu(gate[t,h]) * up[t,h]
    // gate[t,h] = dot(X[t,:], Wg[h,:]) + Bg[h]
    // up[t,h]   = dot(X[t,:], Wu[h,:]) + Bu[h]
    // ------------------------------------------------------------
    for (uint32_t t = 0; t < n_tokens; t++) {
        const float *xrow = &X[(size_t)t * d_model];
        float *actrow = &ACT[(size_t)t * hidden_dim];

        for (uint32_t hidx = 0; hidx < hidden_dim; hidx++) {
            const float *wg = &Wg[(size_t)hidx * d_model];
            const float *wu = &Wu[(size_t)hidx * d_model];

            float gate = 0.0f;
            float up   = 0.0f;

            for (uint32_t k = 0; k < d_model; k++) {
                const float xv = xrow[k];
                gate += xv * wg[k];
                up   += xv * wu[k];
            }

            if (Bg) gate += Bg[hidx];
            if (Bu) up   += Bu[hidx];

            actrow[hidx] = silu(gate) * up;
        }
    }

    // ------------------------------------------------------------
    // Phase 2: Y[t,j] = dot(ACT[t,:], Wd[j,:]) + Bd[j]
    // Wd row-major [d_model][hidden_dim]
    // ------------------------------------------------------------
    for (uint32_t t = 0; t < n_tokens; t++) {
        const float *actrow = &ACT[(size_t)t * hidden_dim];
        float *yrow = &Y[(size_t)t * d_model];

        for (uint32_t j = 0; j < d_model; j++) {
            const float *wd = &Wd[(size_t)j * hidden_dim];
            float acc = 0.0f;

            for (uint32_t hidx = 0; hidx < hidden_dim; hidx++) {
                acc += actrow[hidx] * wd[hidx];
            }

            if (Bd) acc += Bd[j];
            yrow[j] = acc;
        }
    }

    return AT_MLP_OK;
}
