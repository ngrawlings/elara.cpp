// src/atomic_tasks/at_linear_o.c
#include "at_linear_o.h"

#include <stddef.h>
#include <string.h>

// ------------------------------------------------------------
// Default weak stubs for RO weight providers.
// Provide real ones elsewhere (runtime) to supply weights.
// ------------------------------------------------------------
__attribute__((weak))
const float *at_ro_weight_matrix_get(uint32_t weight_id, uint32_t *out_rows, uint32_t *out_cols) {
    (void)weight_id;
    if (out_rows) *out_rows = 0;
    if (out_cols) *out_cols = 0;
    return NULL;
}

__attribute__((weak))
const float *at_ro_bias_vector_get(uint32_t bias_id, uint32_t *out_len) {
    (void)bias_id;
    if (out_len) *out_len = 0;
    return NULL;
}

static int mul_overflows_size_t(size_t a, size_t b) {
    if (a == 0 || b == 0) return 0;
    return a > (SIZE_MAX / b);
}

int at_linear_o_run(epa_ghs_t *ghs, epa_ghs_handle_t h) {
    if (!ghs) return AT_LINEAR_O_ERR;

    void *base = NULL;
    epa_ghs_meta_t meta;
    if (epa_ghs_get_meta(ghs, h, &meta) != EPA_GHS_OK) return AT_LINEAR_O_ERR;
    if (epa_ghs_get_ptr(ghs, h, &base) != EPA_GHS_OK)  return AT_LINEAR_O_ERR;
    if (!base) return AT_LINEAR_O_ERR;

    if (meta.size_bytes < (uint32_t)sizeof(AtLinearOHdr_v1)) return AT_LINEAR_O_ERR;

    uint8_t *p = (uint8_t *)base;
    AtLinearOHdr_v1 *hdr = (AtLinearOHdr_v1 *)p;

    const uint32_t flags    = hdr->flags;
    const uint32_t n_tokens = hdr->n_tokens;
    const uint32_t in_dim   = hdr->in_dim;
    const uint32_t out_dim  = hdr->out_dim;

    if (n_tokens == 0 || in_dim == 0 || out_dim == 0) return AT_LINEAR_O_ERR;

    // counts
    if (mul_overflows_size_t((size_t)n_tokens, (size_t)in_dim))  return AT_LINEAR_O_ERR;
    if (mul_overflows_size_t((size_t)n_tokens, (size_t)out_dim)) return AT_LINEAR_O_ERR;
    if (mul_overflows_size_t((size_t)out_dim,  (size_t)in_dim))  return AT_LINEAR_O_ERR;

    const size_t x_count = (size_t)n_tokens * (size_t)in_dim;
    const size_t y_count = (size_t)n_tokens * (size_t)out_dim;
    const size_t w_count = (size_t)out_dim  * (size_t)in_dim;
    const size_t b_count = (size_t)out_dim;

    // payload layout: [hdr][X][Y][(W?)][(B?)]
    size_t bytes_needed = sizeof(AtLinearOHdr_v1);
    bytes_needed += x_count * sizeof(float);
    bytes_needed += y_count * sizeof(float);
    if (flags & AT_LINEAR_O_F_INLINE_W) bytes_needed += w_count * sizeof(float);
    if (flags & AT_LINEAR_O_F_INLINE_B) bytes_needed += b_count * sizeof(float);

    if ((size_t)meta.size_bytes < bytes_needed) return AT_LINEAR_O_ERR;

    p += sizeof(AtLinearOHdr_v1);

    float *X = (float *)p; p += x_count * sizeof(float);
    float *Y = (float *)p; p += y_count * sizeof(float);

    const float *W = NULL;
    const float *B = NULL;

    // Resolve W
    if (flags & AT_LINEAR_O_F_INLINE_W) {
        W = (const float *)p;
        p += w_count * sizeof(float);
    } else {
        uint32_t wr = 0, wc = 0;
        W = at_ro_weight_matrix_get(hdr->weight_id, &wr, &wc);
        if (!W) return AT_LINEAR_O_ERR;
        // Optional shape enforcement if provider supplies it
        if (wr && wc) {
            if (wr != out_dim || wc != in_dim) return AT_LINEAR_O_ERR;
        }
    }

    // Resolve B (optional)
    if (flags & AT_LINEAR_O_F_HAS_BIAS) {
        if (flags & AT_LINEAR_O_F_INLINE_B) {
            B = (const float *)p;
            p += b_count * sizeof(float);
        } else {
            uint32_t blen = 0;
            B = at_ro_bias_vector_get(hdr->bias_id, &blen);
            if (!B) return AT_LINEAR_O_ERR;
            if (blen && blen != out_dim) return AT_LINEAR_O_ERR;
        }
    }

    // Compute: Y[t, j] = sum_k X[t,k] * W[j,k] + B[j]
    // W row-major [out_dim][in_dim]
    // X row-major [n_tokens][in_dim]
    // Y row-major [n_tokens][out_dim]
    for (uint32_t t = 0; t < n_tokens; t++) {
        const float *xrow = &X[(size_t)t * in_dim];
        float *yrow = &Y[(size_t)t * out_dim];

        for (uint32_t j = 0; j < out_dim; j++) {
            const float *wrow = &W[(size_t)j * in_dim];
            float acc = 0.0f;

            // dot
            for (uint32_t k = 0; k < in_dim; k++) {
                acc += xrow[k] * wrow[k];
            }
            if (B) acc += B[j];
            yrow[j] = acc;
        }
    }

    return AT_LINEAR_O_OK;
}
