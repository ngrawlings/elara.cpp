// atomic_tasks/at_flash_attn.c
#include "at_flash_attn.h"

#include <stddef.h>
#include <string.h>
#include <math.h>

static inline float at_inv_sqrtf(float x) {
    // Simple and portable
    return 1.0f / sqrtf(x);
}

int at_flash_attn_run(epa_ghs_t *ghs, epa_ghs_handle_t h) {
    if (!ghs) return AT_FLASH_ATTN_ERR;

    // Get pointer + meta for bounds checking
    void *base = NULL;
    epa_ghs_meta_t meta;
    if (epa_ghs_get_meta(ghs, h, &meta) != EPA_GHS_OK) return AT_FLASH_ATTN_ERR;
    if (epa_ghs_get_ptr(ghs, h, &base) != EPA_GHS_OK)  return AT_FLASH_ATTN_ERR;
    if (!base) return AT_FLASH_ATTN_ERR;

    if (meta.size_bytes < sizeof(AtFlashAttnHdr_v1)) return AT_FLASH_ATTN_ERR;

    uint8_t *p = (uint8_t *)base;
    const AtFlashAttnHdr_v1 *hdr = (const AtFlashAttnHdr_v1 *)p;

    const uint32_t flags    = hdr->flags;
    const uint32_t n_heads  = hdr->n_heads;
    const uint32_t head_dim = hdr->head_dim;
    const uint32_t seqlen_q = hdr->seqlen_q;
    const uint32_t seqlen_k = hdr->seqlen_k;

    if (n_heads == 0 || head_dim == 0 || seqlen_q == 0 || seqlen_k == 0) {
        return AT_FLASH_ATTN_ERR;
    }

    // Counts
    const size_t q_count   = (size_t)seqlen_q * (size_t)n_heads * (size_t)head_dim;
    const size_t k_count   = (size_t)seqlen_k * (size_t)n_heads * (size_t)head_dim;
    const size_t v_count   = (size_t)seqlen_k * (size_t)n_heads * (size_t)head_dim;
    const size_t out_count = (size_t)seqlen_q * (size_t)n_heads * (size_t)head_dim;

    const size_t bytes_needed =
        sizeof(AtFlashAttnHdr_v1) +
        (q_count + k_count + v_count + out_count) * sizeof(float);

    if ((size_t)meta.size_bytes < bytes_needed) {
        return AT_FLASH_ATTN_ERR;
    }

    // Layout: [Hdr][Q][K][V][Out]
    p += sizeof(AtFlashAttnHdr_v1);
    float *Q   = (float *)p; p += q_count   * sizeof(float);
    float *K   = (float *)p; p += k_count   * sizeof(float);
    float *V   = (float *)p; p += v_count   * sizeof(float);
    float *Out = (float *)p; // p += out_count * sizeof(float);

    // Optional scaling
    const float scale = (flags & AT_FLASH_ATTN_F_SCALE) ? at_inv_sqrtf((float)head_dim) : 1.0f;
    const int causal = (flags & AT_FLASH_ATTN_F_CAUSAL) ? 1 : 0;

    // Compute: for each q position and head:
    //   scores[k] = dot(q, k) * scale
    //   weights = softmax(scores)
    //   out = sum_k weights[k] * v[k]
    //
    // Streaming softmax in 2 passes (no temp allocation):
    for (uint32_t tq = 0; tq < seqlen_q; tq++) {
        for (uint32_t hidx = 0; hidx < n_heads; hidx++) {
            const size_t q_base = ((size_t)tq * n_heads + hidx) * head_dim;
            float *qv = &Q[q_base];

            // 1) Find max score for numerical stability
            float max_score = -INFINITY;
            for (uint32_t tk = 0; tk < seqlen_k; tk++) {
                if (causal && tk > tq) break;

                const size_t k_base = ((size_t)tk * n_heads + hidx) * head_dim;
                float *kv = &K[k_base];

                float dot = 0.0f;
                for (uint32_t d = 0; d < head_dim; d++) {
                    dot += qv[d] * kv[d];
                }
                dot *= scale;

                if (dot > max_score) max_score = dot;
            }

            // If everything was masked (possible in weird configs), produce zeros
            if (!isfinite(max_score)) {
                const size_t out_base = ((size_t)tq * n_heads + hidx) * head_dim;
                memset(&Out[out_base], 0, head_dim * sizeof(float));
                continue;
            }

            // 2) Accumulate denom and weighted sum
            float denom = 0.0f;
            const size_t out_base = ((size_t)tq * n_heads + hidx) * head_dim;

            // zero output accumulator
            for (uint32_t d = 0; d < head_dim; d++) {
                Out[out_base + d] = 0.0f;
            }

            for (uint32_t tk = 0; tk < seqlen_k; tk++) {
                if (causal && tk > tq) break;

                const size_t k_base = ((size_t)tk * n_heads + hidx) * head_dim;
                float *kv = &K[k_base];

                float dot = 0.0f;
                for (uint32_t d = 0; d < head_dim; d++) {
                    dot += qv[d] * kv[d];
                }
                dot *= scale;

                float w = expf(dot - max_score);
                denom += w;

                const size_t v_base = ((size_t)tk * n_heads + hidx) * head_dim;
                float *vv = &V[v_base];

                for (uint32_t d = 0; d < head_dim; d++) {
                    Out[out_base + d] += w * vv[d];
                }
            }

            // Normalize
            if (denom > 0.0f) {
                float inv = 1.0f / denom;
                for (uint32_t d = 0; d < head_dim; d++) {
                    Out[out_base + d] *= inv;
                }
            } else {
                // Shouldn't happen unless all weights underflow
                memset(&Out[out_base], 0, head_dim * sizeof(float));
            }
        }
    }

    return AT_FLASH_ATTN_OK;
}
