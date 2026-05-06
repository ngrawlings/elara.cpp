// unittests/ctests/test_at_rmsnorm_qkv_rope_fused.c
#include "ctest.h"

#include "atomic_tasks/at_rmsnorm_qkv_rope_fused.h"
#include "memory/epa_ghs.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

static int feq(float a, float b) {
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d < 1e-4f;
}

static void rope_apply_one_ref(float *vec, uint32_t rotary_dim, uint32_t pos, float theta) {
    // Must match the implementation in at_rmsnorm_qkv_rope_fused.c
    const float inv_theta = 1.0f / theta;

    for (uint32_t i = 0; i + 1 < rotary_dim; i += 2) {
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

static void rmsnorm_ref(const float *x, float *xn, const float *w, uint32_t d_model, float eps) {
    float ms = 0.0f;
    for (uint32_t i = 0; i < d_model; i++) ms += x[i] * x[i];
    ms /= (float)d_model;
    float inv = 1.0f / sqrtf(ms + eps);
    for (uint32_t i = 0; i < d_model; i++) xn[i] = x[i] * (w[i] * inv);
}

CTEST(test_at_rmsnorm_qkv_rope_fused_basic)
{
    epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    // Small, deterministic dims
    const uint32_t n_tokens   = 2;
    const uint32_t d_model    = 4;

    const uint32_t n_heads    = 1;
    const uint32_t n_kv_heads = 1;
    const uint32_t head_dim   = 4;

    const uint32_t rotary_dim = 4;      // rotate full head
    const float eps           = 1e-5f;
    const float theta         = 10000.0f;

    const uint32_t out_rows = (n_heads + 2u * n_kv_heads) * head_dim; // (1 + 2)*4 = 12

    const size_t pos_count = n_tokens;
    const size_t x_count   = (size_t)n_tokens * d_model;
    const size_t q_count   = (size_t)n_tokens * n_heads    * head_dim;
    const size_t kv_count  = (size_t)n_tokens * n_kv_heads * head_dim;

    const size_t w_count   = (size_t)out_rows * d_model;

    // Payload required by v1:
    // hdr + positions + X + Q + K + V + scratch_Xn + inline_rms_w + inline_qkv_w
    const size_t bytes =
        sizeof(AtRmsnormQkvRopeHdr_v1) +
        pos_count * sizeof(uint32_t) +
        x_count   * sizeof(float) +
        q_count   * sizeof(float) +
        kv_count  * sizeof(float) +
        kv_count  * sizeof(float) +
        x_count   * sizeof(float) +
        (size_t)d_model * sizeof(float) +
        w_count * sizeof(float);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t *)base;

    AtRmsnormQkvRopeHdr_v1 *hdr = (AtRmsnormQkvRopeHdr_v1 *)p;
    memset(hdr, 0, sizeof(*hdr));
    hdr->flags      = AT_RQRF_F_INLINE_RMS_W | AT_RQRF_F_INLINE_QKV_W | AT_RQRF_F_QKV_ORDER_QKV;
    hdr->n_tokens   = n_tokens;
    hdr->d_model    = d_model;
    hdr->n_heads    = n_heads;
    hdr->n_kv_heads = n_kv_heads;
    hdr->head_dim   = head_dim;
    hdr->rotary_dim = rotary_dim;
    hdr->rms_eps    = eps;
    hdr->rope_theta = theta;
    p += sizeof(*hdr);

    uint32_t *pos = (uint32_t *)p;
    p += pos_count * sizeof(uint32_t);

    float *X = (float *)p;
    p += x_count * sizeof(float);

    float *Q = (float *)p;
    p += q_count * sizeof(float);

    float *K = (float *)p;
    p += kv_count * sizeof(float);

    float *V = (float *)p;
    p += kv_count * sizeof(float);

    float *Xn_scratch = (float *)p;
    p += x_count * sizeof(float);

    const float *rms_w = (const float *)p;
    p += (size_t)d_model * sizeof(float);

    const float *qkv_w = (const float *)p;

    // ---- Init positions
    pos[0] = 1;
    pos[1] = 7;

    // ---- Init X
    // token0: [1, 2, 3, 4]
    // token1: [5, 6, 7, 8]
    for (uint32_t t = 0; t < n_tokens; t++) {
        for (uint32_t i = 0; i < d_model; i++) {
            X[t*d_model + i] = (float)(1 + (int)(t*d_model + i));
        }
    }

    // ---- RMS weight = all 1s (so just scaled by inv_rms)
    float *rms_w_mut = (float *)(uintptr_t)rms_w;
    for (uint32_t i = 0; i < d_model; i++) rms_w_mut[i] = 1.0f;

    // ---- QKV weight: identity blocks
    // out_rows = 12, d_model = 4
    // rows 0..3   -> Q (identity)
    // rows 4..7   -> K (identity)
    // rows 8..11  -> V (identity)
    float *W = (float *)(uintptr_t)qkv_w;
    memset(W, 0, w_count * sizeof(float));
    for (uint32_t block = 0; block < 3; block++) {
        for (uint32_t i = 0; i < d_model; i++) {
            uint32_t row = block * d_model + i;
            W[(size_t)row * d_model + i] = 1.0f;
        }
    }

    // ---- Run AT
    ASSERT_TRUE(at_rmsnorm_qkv_rope_fused_run(ghs, h) == AT_RQRF_OK);

    // ---- Build reference expected outputs
    float xn_ref[2][4];
    for (uint32_t t = 0; t < n_tokens; t++) {
        rmsnorm_ref(&X[t*d_model], xn_ref[t], rms_w, d_model, eps);
    }

    // Expected:
    // Q = RoPE(xn_ref)
    // K = RoPE(xn_ref)
    // V = xn_ref
    for (uint32_t t = 0; t < n_tokens; t++) {
        float q_expect[4] = { xn_ref[t][0], xn_ref[t][1], xn_ref[t][2], xn_ref[t][3] };
        float k_expect[4] = { xn_ref[t][0], xn_ref[t][1], xn_ref[t][2], xn_ref[t][3] };
        float v_expect[4] = { xn_ref[t][0], xn_ref[t][1], xn_ref[t][2], xn_ref[t][3] };

        rope_apply_one_ref(q_expect, rotary_dim, pos[t], theta);
        rope_apply_one_ref(k_expect, rotary_dim, pos[t], theta);

        // Q layout: [t][n_heads][head_dim] and n_heads=1
        float *q_got = &Q[(size_t)t * head_dim];
        float *k_got = &K[(size_t)t * head_dim];
        float *v_got = &V[(size_t)t * head_dim];

        for (uint32_t i = 0; i < head_dim; i++) {
            ASSERT_TRUE(feq(q_got[i], q_expect[i]));
            ASSERT_TRUE(feq(k_got[i], k_expect[i]));
            ASSERT_TRUE(feq(v_got[i], v_expect[i]));
        }
    }

    // sanity: scratch exists and was written (not required to verify, but good signal)
    for (uint32_t i = 0; i < (uint32_t)x_count; i++) {
        ASSERT_TRUE(isfinite(Xn_scratch[i]));
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

void ctest_register_test_at_rmsnorm_qkv_rope_fused(void) {
    const char *F = "test_at_rmsnorm_qkv_rope_fused.c";
    REG(F, test_at_rmsnorm_qkv_rope_fused_basic);
}
