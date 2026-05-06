// unittests/ctests/test_at_mlp_fused.c
#include "../ctest.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

#include "memory/epa_ghs.h"
#include "atomic_tasks/at_mlp_fused.h"

static int feq(float a, float b) {
    float d = fabsf(a - b);
    return d <= 1e-4f; // MLP has expf in silu; allow slightly larger tol
}

static inline float silu_ref(float x) {
    return x / (1.0f + expf(-x));
}

// Reference implementation: same equations as AT
static void ref_mlp_fused(
    const float *X,
    const float *Wg, const float *Wu, const float *Wd,
    const float *Bg, const float *Bu, const float *Bd,
    uint32_t n_tokens, uint32_t d_model, uint32_t hidden_dim,
    float *Yref
) {
    // Scratch ACT
    float ACT[(size_t)n_tokens * (size_t)hidden_dim];

    // ACT[t,h] = silu(X*Wg^T + Bg) * (X*Wu^T + Bu)
    for (uint32_t t = 0; t < n_tokens; t++) {
        const float *xrow = &X[(size_t)t * d_model];
        float *actrow = &ACT[(size_t)t * hidden_dim];

        for (uint32_t h = 0; h < hidden_dim; h++) {
            const float *wg = &Wg[(size_t)h * d_model];
            const float *wu = &Wu[(size_t)h * d_model];

            float gate = 0.0f;
            float up   = 0.0f;

            for (uint32_t k = 0; k < d_model; k++) {
                float xv = xrow[k];
                gate += xv * wg[k];
                up   += xv * wu[k];
            }
            if (Bg) gate += Bg[h];
            if (Bu) up   += Bu[h];

            actrow[h] = silu_ref(gate) * up;
        }
    }

    // Y[t,j] = ACT*Wd^T + Bd
    for (uint32_t t = 0; t < n_tokens; t++) {
        const float *actrow = &ACT[(size_t)t * hidden_dim];
        float *yrow = &Yref[(size_t)t * d_model];

        for (uint32_t j = 0; j < d_model; j++) {
            const float *wd = &Wd[(size_t)j * hidden_dim];
            float acc = 0.0f;

            for (uint32_t h = 0; h < hidden_dim; h++) {
                acc += actrow[h] * wd[h];
            }
            if (Bd) acc += Bd[j];
            yrow[j] = acc;
        }
    }
}

static epa_ghs_handle_t make_payload_inline(
    epa_ghs_t *ghs,
    uint32_t n_tokens, uint32_t d_model, uint32_t hidden_dim,
    int with_bias,
    float **out_X, float **out_Y,
    float **out_Wg, float **out_Wu, float **out_Wd,
    float **out_Bg, float **out_Bu, float **out_Bd,
    float **out_ACT,
    AtMlpFusedHdr_v1 **out_hdr
) {
    const size_t x_count   = (size_t)n_tokens * (size_t)d_model;
    const size_t y_count   = (size_t)n_tokens * (size_t)d_model;

    const size_t wg_count  = (size_t)hidden_dim * (size_t)d_model;
    const size_t wu_count  = (size_t)hidden_dim * (size_t)d_model;
    const size_t wd_count  = (size_t)d_model    * (size_t)hidden_dim;

    const size_t bg_count  = (size_t)hidden_dim;
    const size_t bu_count  = (size_t)hidden_dim;
    const size_t bd_count  = (size_t)d_model;

    const size_t act_count = (size_t)n_tokens * (size_t)hidden_dim;

    uint32_t flags = AT_MLP_F_INLINE_W | AT_MLP_F_HAS_SCRATCH;
    if (with_bias) flags |= (AT_MLP_F_HAS_BIAS | AT_MLP_F_INLINE_B);

    size_t bytes = sizeof(AtMlpFusedHdr_v1)
                 + x_count * sizeof(float)
                 + y_count * sizeof(float)
                 + (wg_count + wu_count + wd_count) * sizeof(float);

    if (with_bias) bytes += (bg_count + bu_count + bd_count) * sizeof(float);

    bytes += act_count * sizeof(float); // scratch

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    ASSERT_TRUE(base != NULL);

    memset(base, 0, bytes);

    uint8_t *p = (uint8_t *)base;

    AtMlpFusedHdr_v1 *hdr = (AtMlpFusedHdr_v1 *)p;
    hdr->flags      = flags;
    hdr->n_tokens   = n_tokens;
    hdr->d_model    = d_model;
    hdr->hidden_dim = hidden_dim;

    // ids unused in inline mode
    hdr->w_gate_id = hdr->w_up_id = hdr->w_down_id = 0;
    hdr->b_gate_id = hdr->b_up_id = hdr->b_down_id = 0;

    p += sizeof(*hdr);

    float *X  = (float *)p; p += x_count * sizeof(float);
    float *Y  = (float *)p; p += y_count * sizeof(float);

    float *Wg = (float *)p; p += wg_count * sizeof(float);
    float *Wu = (float *)p; p += wu_count * sizeof(float);
    float *Wd = (float *)p; p += wd_count * sizeof(float);

    float *Bg = NULL, *Bu = NULL, *Bd = NULL;
    if (with_bias) {
        Bg = (float *)p; p += bg_count * sizeof(float);
        Bu = (float *)p; p += bu_count * sizeof(float);
        Bd = (float *)p; p += bd_count * sizeof(float);
    }

    float *ACT = (float *)p; // scratch

    if (out_hdr) *out_hdr = hdr;
    if (out_X)   *out_X = X;
    if (out_Y)   *out_Y = Y;
    if (out_Wg)  *out_Wg = Wg;
    if (out_Wu)  *out_Wu = Wu;
    if (out_Wd)  *out_Wd = Wd;
    if (out_Bg)  *out_Bg = Bg;
    if (out_Bu)  *out_Bu = Bu;
    if (out_Bd)  *out_Bd = Bd;
    if (out_ACT) *out_ACT = ACT;

    return h;
}

CTEST(test_at_mlp_fused_basic)
{
    epa_ghs_t *ghs = epa_ghs_create(1 << 22, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_tokens   = 2;
    const uint32_t d_model    = 4;
    const uint32_t hidden_dim = 6;

    float *X=NULL, *Y=NULL, *Wg=NULL, *Wu=NULL, *Wd=NULL;
    float *Bg=NULL, *Bu=NULL, *Bd=NULL, *ACT=NULL;
    AtMlpFusedHdr_v1 *hdr=NULL;

    epa_ghs_handle_t h = make_payload_inline(
        ghs, n_tokens, d_model, hidden_dim,
        /*with_bias=*/1,
        &X, &Y, &Wg, &Wu, &Wd, &Bg, &Bu, &Bd, &ACT, &hdr
    );

    // Fill X with small deterministic values
    for (uint32_t t = 0; t < n_tokens; t++) {
        for (uint32_t k = 0; k < d_model; k++) {
            X[(size_t)t * d_model + k] = 0.1f * (float)(1 + (int)t + (int)k);
        }
    }

    // Fill weights with mild values
    for (uint32_t hidx = 0; hidx < hidden_dim; hidx++) {
        for (uint32_t k = 0; k < d_model; k++) {
            Wg[(size_t)hidx * d_model + k] = 0.01f * (float)(1 + (int)hidx + (int)k);
            Wu[(size_t)hidx * d_model + k] = 0.02f * (float)(1 + (int)hidx - (int)k);
        }
    }
    for (uint32_t j = 0; j < d_model; j++) {
        for (uint32_t hidx = 0; hidx < hidden_dim; hidx++) {
            Wd[(size_t)j * hidden_dim + hidx] = 0.03f * (float)(1 + (int)j + (int)hidx);
        }
    }

    // Biases
    for (uint32_t hidx = 0; hidx < hidden_dim; hidx++) {
        Bg[hidx] = 0.001f * (float)hidx;
        Bu[hidx] = -0.001f * (float)hidx;
    }
    for (uint32_t j = 0; j < d_model; j++) {
        Bd[j] = 0.01f * (float)j;
    }

    // Run
    int rc = at_mlp_fused_run(ghs, h);
    ASSERT_TRUE(rc == AT_MLP_OK);

    // Reference compare
    float Yref[(size_t)n_tokens * (size_t)d_model];
    ref_mlp_fused(X, Wg, Wu, Wd, Bg, Bu, Bd, n_tokens, d_model, hidden_dim, Yref);

    for (uint32_t i = 0; i < n_tokens * d_model; i++) {
        ASSERT_TRUE(feq(Y[i], Yref[i]));
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_mlp_fused_no_bias)
{
    epa_ghs_t *ghs = epa_ghs_create(1 << 22, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_tokens   = 3;
    const uint32_t d_model    = 3;
    const uint32_t hidden_dim = 5;

    float *X=NULL, *Y=NULL, *Wg=NULL, *Wu=NULL, *Wd=NULL;
    float *Bg=NULL, *Bu=NULL, *Bd=NULL, *ACT=NULL;
    AtMlpFusedHdr_v1 *hdr=NULL;

    epa_ghs_handle_t h = make_payload_inline(
        ghs, n_tokens, d_model, hidden_dim,
        /*with_bias=*/0,
        &X, &Y, &Wg, &Wu, &Wd, &Bg, &Bu, &Bd, &ACT, &hdr
    );

    for (uint32_t t = 0; t < n_tokens; t++) {
        for (uint32_t k = 0; k < d_model; k++) {
            X[(size_t)t * d_model + k] = (float)((int)t - (int)k) * 0.2f;
        }
    }

    for (uint32_t hidx = 0; hidx < hidden_dim; hidx++) {
        for (uint32_t k = 0; k < d_model; k++) {
            Wg[(size_t)hidx * d_model + k] = 0.015f * (float)(1 + (int)k);
            Wu[(size_t)hidx * d_model + k] = 0.010f * (float)(1 + (int)hidx);
        }
    }
    for (uint32_t j = 0; j < d_model; j++) {
        for (uint32_t hidx = 0; hidx < hidden_dim; hidx++) {
            Wd[(size_t)j * hidden_dim + hidx] = 0.02f * (float)(1 + (int)j - (int)hidx);
        }
    }

    int rc = at_mlp_fused_run(ghs, h);
    ASSERT_TRUE(rc == AT_MLP_OK);

    float Yref[(size_t)n_tokens * (size_t)d_model];
    ref_mlp_fused(X, Wg, Wu, Wd, NULL, NULL, NULL, n_tokens, d_model, hidden_dim, Yref);

    for (uint32_t i = 0; i < n_tokens * d_model; i++) {
        ASSERT_TRUE(feq(Y[i], Yref[i]));
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_mlp_fused_missing_scratch_fails) {
    epa_ghs_t *ghs = epa_ghs_create(1 << 20, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_tokens   = 1;
    const uint32_t d_model    = 2;
    const uint32_t hidden_dim = 3;

    const size_t x_count  = (size_t)n_tokens * d_model;
    const size_t y_count  = (size_t)n_tokens * d_model;
    const size_t wg_count = (size_t)hidden_dim * d_model;
    const size_t wu_count = (size_t)hidden_dim * d_model;
    const size_t wd_count = (size_t)d_model * hidden_dim;

    // Intentionally omit scratch region and omit HAS_SCRATCH flag.
    const size_t bytes =
        sizeof(AtMlpFusedHdr_v1)
      + x_count * sizeof(float)
      + y_count * sizeof(float)
      + (wg_count + wu_count + wd_count) * sizeof(float);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    ASSERT_TRUE(base != NULL);

    memset(base, 0, bytes);

    AtMlpFusedHdr_v1 *hdr = (AtMlpFusedHdr_v1 *)base;
    hdr->flags      = AT_MLP_F_INLINE_W; // no HAS_SCRATCH
    hdr->n_tokens   = n_tokens;
    hdr->d_model    = d_model;
    hdr->hidden_dim = hidden_dim;

    int rc = at_mlp_fused_run(ghs, h);
    ASSERT_TRUE(rc == AT_MLP_ERR);

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

void ctest_register_test_at_mlp_fused(void) {
    const char *F = "test_at_mlp_fused.c";
    REG(F, test_at_mlp_fused_basic);
    REG(F, test_at_mlp_fused_no_bias);
    REG(F, test_at_mlp_fused_missing_scratch_fails);
}
