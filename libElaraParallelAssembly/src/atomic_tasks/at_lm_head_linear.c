// src/atomic_tasks/at_lm_head_linear.c
#include "at_lm_head_linear.h"

#include <stddef.h>
#include <string.h>

__attribute__((weak))
const float *at_ro_lm_head_weight_get(uint32_t weight_id, uint32_t *out_rows, uint32_t *out_cols) {
    (void)weight_id;
    if (out_rows) *out_rows = 0;
    if (out_cols) *out_cols = 0;
    return NULL;
}

__attribute__((weak))
const float *at_ro_lm_head_bias_get(uint32_t bias_id, uint32_t *out_len) {
    (void)bias_id;
    if (out_len) *out_len = 0;
    return NULL;
}

static int mul_overflows_size_t(size_t a, size_t b) {
    if (a == 0 || b == 0) return 0;
    return a > (SIZE_MAX / b);
}

int at_lm_head_linear_run(epa_ghs_t *ghs, epa_ghs_handle_t h) {
    if (!ghs) return AT_LM_HEAD_ERR;

    void *base = NULL;
    epa_ghs_meta_t meta;
    if (epa_ghs_get_meta(ghs, h, &meta) != EPA_GHS_OK) return AT_LM_HEAD_ERR;
    if (epa_ghs_get_ptr(ghs, h, &base) != EPA_GHS_OK)  return AT_LM_HEAD_ERR;
    if (!base) return AT_LM_HEAD_ERR;

    if (meta.size_bytes < (uint32_t)sizeof(AtLmHeadHdr_v1)) return AT_LM_HEAD_ERR;

    uint8_t *p = (uint8_t *)base;
    AtLmHeadHdr_v1 *hdr = (AtLmHeadHdr_v1 *)p;

    const uint32_t flags     = hdr->flags;
    const uint32_t n_tokens  = hdr->n_tokens;
    const uint32_t d_model   = hdr->d_model;
    const uint32_t vocab     = hdr->vocab_size;

    if (n_tokens == 0 || d_model == 0 || vocab == 0) return AT_LM_HEAD_ERR;

    if (mul_overflows_size_t((size_t)n_tokens, (size_t)d_model)) return AT_LM_HEAD_ERR;
    if (mul_overflows_size_t((size_t)n_tokens, (size_t)vocab))  return AT_LM_HEAD_ERR;
    if (mul_overflows_size_t((size_t)vocab, (size_t)d_model))   return AT_LM_HEAD_ERR;

    const size_t x_count   = (size_t)n_tokens * (size_t)d_model;
    const size_t out_count = (size_t)n_tokens * (size_t)vocab;
    const size_t w_count   = (size_t)vocab * (size_t)d_model;
    const size_t b_count   = (size_t)vocab;

    size_t bytes_needed = sizeof(AtLmHeadHdr_v1)
        + x_count   * sizeof(float)
        + out_count * sizeof(float);

    if (flags & AT_LM_HEAD_F_INLINE_W) {
        bytes_needed += w_count * sizeof(float);
    }
    if ((flags & AT_LM_HEAD_F_HAS_BIAS) && (flags & AT_LM_HEAD_F_INLINE_B)) {
        bytes_needed += b_count * sizeof(float);
    }

    if ((size_t)meta.size_bytes < bytes_needed) return AT_LM_HEAD_ERR;

    p += sizeof(AtLmHeadHdr_v1);

    float *X      = (float *)p; p += x_count   * sizeof(float);
    float *LOGITS = (float *)p; p += out_count * sizeof(float);

    const float *W = NULL;
    const float *B = NULL;

    if (flags & AT_LM_HEAD_F_INLINE_W) {
        W = (const float *)p; p += w_count * sizeof(float);
    } else {
        uint32_t r=0,c=0;
        W = at_ro_lm_head_weight_get(hdr->w_id, &r, &c);
        if (!W) return AT_LM_HEAD_ERR;
        if (r && c && (r != vocab || c != d_model)) return AT_LM_HEAD_ERR;
    }

    if (flags & AT_LM_HEAD_F_HAS_BIAS) {
        if (flags & AT_LM_HEAD_F_INLINE_B) {
            B = (const float *)p; p += b_count * sizeof(float);
        } else {
            uint32_t len=0;
            B = at_ro_lm_head_bias_get(hdr->b_id, &len);
            if (!B) return AT_LM_HEAD_ERR;
            if (len && len != vocab) return AT_LM_HEAD_ERR;
        }
    }

    // logits[t, v] = sum_k X[t,k] * W[v,k] + B[v]
    for (uint32_t t = 0; t < n_tokens; t++) {
        const float *xrow = &X[(size_t)t * d_model];
        float *lrow = &LOGITS[(size_t)t * vocab];

        for (uint32_t v = 0; v < vocab; v++) {
            const float *wrow = &W[(size_t)v * d_model];
            float acc = 0.0f;

            for (uint32_t k = 0; k < d_model; k++) {
                acc += xrow[k] * wrow[k];
            }
            if (B) acc += B[v];
            lrow[v] = acc;
        }
    }

    return AT_LM_HEAD_OK;
}
