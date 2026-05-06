// src/atomic_tasks/at_rmsnorm_qkv_rope_fused.c
#include "at_rmsnorm_qkv_rope_fused.h"

#include <stddef.h>
#include <string.h>
#include <math.h>

// If you ever see undefined refs to sqrtf/cosf/sinf/powf at link time,
// add -lm to your link flags.

__attribute__((weak))
const float *at_ro_rms_weight_get(uint32_t weight_id, uint32_t *out_len) {
    (void)weight_id;
    if (out_len) *out_len = 0;
    return NULL;
}

__attribute__((weak))
const float *at_ro_qkv_weight_get(uint32_t weight_id, uint32_t *out_rows, uint32_t *out_cols) {
    (void)weight_id;
    if (out_rows) *out_rows = 0;
    if (out_cols) *out_cols = 0;
    return NULL;
}

__attribute__((weak))
const float *at_ro_qkv_bias_get(uint32_t bias_id, uint32_t *out_len) {
    (void)bias_id;
    if (out_len) *out_len = 0;
    return NULL;
}

static int mul_overflows_size_t(size_t a, size_t b) {
    if (a == 0 || b == 0) return 0;
    return a > (SIZE_MAX / b);
}

static void rope_apply_one(float *vec, uint32_t head_dim, uint32_t rotary_dim, uint32_t pos, float theta) {
    // Apply RoPE on [0..rotary_dim) in pairs
    // inv_freq(i) = 1 / pow(theta, i/rotary_dim) for i even indices (0,2,...)
    const float inv_theta = 1.0f / theta;

    (void)head_dim;
    for (uint32_t i = 0; i + 1 < rotary_dim; i += 2) {
        // exponent = i / rotary_dim
        // freq = theta^(-i/rotary_dim) = (1/theta)^(i/rotary_dim)
        float exponent = (float)i / (float)rotary_dim;
        float freq = powf(inv_theta, exponent);
        float ang = (float)pos * freq;

        float c = cosf(ang);
        float s = sinf(ang);

        float x0 = vec[i + 0];
        float x1 = vec[i + 1];

        vec[i + 0] = x0 * c - x1 * s;
        vec[i + 1] = x0 * s + x1 * c;
    }
}

int at_rmsnorm_qkv_rope_fused_run(epa_ghs_t *ghs, epa_ghs_handle_t h) {
    if (!ghs) return AT_RQRF_ERR;

    void *base = NULL;
    epa_ghs_meta_t meta;
    if (epa_ghs_get_meta(ghs, h, &meta) != EPA_GHS_OK) return AT_RQRF_ERR;
    if (epa_ghs_get_ptr(ghs, h, &base) != EPA_GHS_OK)  return AT_RQRF_ERR;
    if (!base) return AT_RQRF_ERR;

    if (meta.size_bytes < (uint32_t)sizeof(AtRmsnormQkvRopeHdr_v1)) return AT_RQRF_ERR;

    uint8_t *p = (uint8_t *)base;
    AtRmsnormQkvRopeHdr_v1 *hdr = (AtRmsnormQkvRopeHdr_v1 *)p;

    const uint32_t flags     = hdr->flags;
    const uint32_t n_tokens  = hdr->n_tokens;
    const uint32_t d_model   = hdr->d_model;
    const uint32_t n_heads   = hdr->n_heads;
    const uint32_t n_kv      = hdr->n_kv_heads;
    const uint32_t head_dim  = hdr->head_dim;
    const uint32_t rotary_dim = hdr->rotary_dim;
    const float eps          = hdr->rms_eps;
    const float theta        = hdr->rope_theta;

    if (n_tokens == 0 || d_model == 0 || n_heads == 0 || n_kv == 0 || head_dim == 0) return AT_RQRF_ERR;
    if (rotary_dim > head_dim) return AT_RQRF_ERR;
    if ((rotary_dim & 1u) != 0u) return AT_RQRF_ERR; // must be even

    // out_rows = (Q + K + V) rows, each of length head_dim
    const uint32_t out_rows = (n_heads + 2u * n_kv) * head_dim;

    // size checks
    if (mul_overflows_size_t((size_t)n_tokens, (size_t)d_model)) return AT_RQRF_ERR;
    if (mul_overflows_size_t((size_t)n_tokens, (size_t)n_heads)) return AT_RQRF_ERR;
    if (mul_overflows_size_t((size_t)n_tokens, (size_t)n_kv))    return AT_RQRF_ERR;
    if (mul_overflows_size_t((size_t)out_rows, (size_t)d_model)) return AT_RQRF_ERR;

    const size_t pos_count = (size_t)n_tokens;
    const size_t x_count   = (size_t)n_tokens * (size_t)d_model;

    const size_t q_count   = (size_t)n_tokens * (size_t)n_heads * (size_t)head_dim;
    const size_t kv_count  = (size_t)n_tokens * (size_t)n_kv    * (size_t)head_dim;

    const size_t w_count   = (size_t)out_rows * (size_t)d_model;
    const size_t b_count   = (size_t)out_rows;

    // Required payload regions in this v1:
    // positions + X + Q + K + V + scratch_norm
    size_t bytes_needed =
        sizeof(AtRmsnormQkvRopeHdr_v1) +
        pos_count * sizeof(uint32_t) +
        x_count   * sizeof(float) +
        q_count   * sizeof(float) +
        kv_count  * sizeof(float) +  // K
        kv_count  * sizeof(float) +  // V
        x_count   * sizeof(float);   // scratch_norm

    if (flags & AT_RQRF_F_INLINE_RMS_W) bytes_needed += (size_t)d_model * sizeof(float);
    if (flags & AT_RQRF_F_INLINE_QKV_W) bytes_needed += w_count * sizeof(float);
    if ((flags & AT_RQRF_F_HAS_BIAS) && (flags & AT_RQRF_F_INLINE_QKV_B)) bytes_needed += b_count * sizeof(float);

    if ((size_t)meta.size_bytes < bytes_needed) return AT_RQRF_ERR;

    // Parse payload
    p += sizeof(AtRmsnormQkvRopeHdr_v1);

    uint32_t *positions = (uint32_t *)p;
    p += pos_count * sizeof(uint32_t);

    float *X = (float *)p;
    p += x_count * sizeof(float);

    float *Q = (float *)p;
    p += q_count * sizeof(float);

    float *K = (float *)p;
    p += kv_count * sizeof(float);

    float *V = (float *)p;
    p += kv_count * sizeof(float);

    float *Xn = (float *)p; // scratch: normalized X
    p += x_count * sizeof(float);

    const float *rms_w = NULL;
    const float *qkv_w = NULL;
    const float *qkv_b = NULL;

    // Resolve RMS weight
    if (flags & AT_RQRF_F_INLINE_RMS_W) {
        rms_w = (const float *)p;
        p += (size_t)d_model * sizeof(float);
    } else {
        uint32_t len = 0;
        rms_w = at_ro_rms_weight_get(hdr->w_rms_id, &len);
        if (!rms_w) return AT_RQRF_ERR;
        if (len && len != d_model) return AT_RQRF_ERR;
    }

    // Resolve QKV weight
    if (flags & AT_RQRF_F_INLINE_QKV_W) {
        qkv_w = (const float *)p;
        p += w_count * sizeof(float);
    } else {
        uint32_t r=0, c=0;
        qkv_w = at_ro_qkv_weight_get(hdr->w_qkv_id, &r, &c);
        if (!qkv_w) return AT_RQRF_ERR;
        if (r && c && (r != out_rows || c != d_model)) return AT_RQRF_ERR;
    }

    // Resolve bias if present
    if (flags & AT_RQRF_F_HAS_BIAS) {
        if (flags & AT_RQRF_F_INLINE_QKV_B) {
            qkv_b = (const float *)p;
            p += b_count * sizeof(float);
        } else {
            uint32_t len = 0;
            qkv_b = at_ro_qkv_bias_get(hdr->b_qkv_id, &len);
            if (!qkv_b) return AT_RQRF_ERR;
            if (len && len != out_rows) return AT_RQRF_ERR;
        }
    }

    // ---- 1) RMSNorm: Xn[t,*] = X[t,*] * (rms_w[*] * inv_rms)
    for (uint32_t t = 0; t < n_tokens; t++) {
        const float *xrow = &X[(size_t)t * d_model];
        float *xnrow = &Xn[(size_t)t * d_model];

        float mean_sq = 0.0f;
        for (uint32_t i = 0; i < d_model; i++) {
            float v = xrow[i];
            mean_sq += v * v;
        }
        mean_sq /= (float)d_model;

        float inv_rms = 1.0f / sqrtf(mean_sq + eps);

        for (uint32_t i = 0; i < d_model; i++) {
            xnrow[i] = xrow[i] * (rms_w[i] * inv_rms);
        }
    }

    // ---- 2) QKV projection:
    // out_row j: y[j] = dot(Xn, W[j,*]) + bias[j]
    //
    // Row packing:
    //   Q rows: n_heads*head_dim
    //   K rows: n_kv*head_dim
    //   V rows: n_kv*head_dim
    const uint32_t q_rows = n_heads * head_dim;
    const uint32_t k_rows = n_kv   * head_dim;
    const uint32_t v_rows = n_kv   * head_dim;

    for (uint32_t t = 0; t < n_tokens; t++) {
        const float *xnrow = &Xn[(size_t)t * d_model];

        // Project Q
        for (uint32_t r = 0; r < q_rows; r++) {
            const float *wrow = &qkv_w[(size_t)r * d_model];
            float acc = qkv_b ? qkv_b[r] : 0.0f;
            for (uint32_t i = 0; i < d_model; i++) acc += xnrow[i] * wrow[i];
            Q[(size_t)t * q_rows + r] = acc;
        }

        // Project K
        for (uint32_t r = 0; r < k_rows; r++) {
            const uint32_t row = q_rows + r;
            const float *wrow = &qkv_w[(size_t)row * d_model];
            float acc = qkv_b ? qkv_b[row] : 0.0f;
            for (uint32_t i = 0; i < d_model; i++) acc += xnrow[i] * wrow[i];
            K[(size_t)t * k_rows + r] = acc;
        }

        // Project V
        for (uint32_t r = 0; r < v_rows; r++) {
            const uint32_t row = q_rows + k_rows + r;
            const float *wrow = &qkv_w[(size_t)row * d_model];
            float acc = qkv_b ? qkv_b[row] : 0.0f;
            for (uint32_t i = 0; i < d_model; i++) acc += xnrow[i] * wrow[i];
            V[(size_t)t * v_rows + r] = acc;
        }
    }

    // ---- 3) Apply RoPE to Q and K
    // Q: [t][n_heads][head_dim]
    // K: [t][n_kv][head_dim]
    for (uint32_t t = 0; t < n_tokens; t++) {
        const uint32_t pos = positions[t];

        // Q
        for (uint32_t hq = 0; hq < n_heads; hq++) {
            float *qv = &Q[((size_t)t * n_heads + hq) * head_dim];
            rope_apply_one(qv, head_dim, rotary_dim, pos, theta);
        }

        // K
        for (uint32_t hk = 0; hk < n_kv; hk++) {
            float *kv = &K[((size_t)t * n_kv + hk) * head_dim];
            rope_apply_one(kv, head_dim, rotary_dim, pos, theta);
        }
    }

    return AT_RQRF_OK;
}
