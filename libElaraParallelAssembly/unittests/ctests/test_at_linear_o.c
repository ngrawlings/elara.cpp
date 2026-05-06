// unittests/ctests/test_at_linear_o.c
#include "../ctest.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

#include "memory/epa_ghs.h"
#include "atomic_tasks/at_linear_o.h"

static int feq(float a, float b) {
    // Mild tolerance; dot products accumulate
    float d = fabsf(a - b);
    return d <= 1e-5f;
}

static void ref_linear_o(
    const float *X, const float *W, const float *B,
    uint32_t n_tokens, uint32_t in_dim, uint32_t out_dim,
    float *Yref
) {
    for (uint32_t t = 0; t < n_tokens; t++) {
        for (uint32_t j = 0; j < out_dim; j++) {
            float acc = 0.0f;
            const float *xrow = &X[(size_t)t * in_dim];
            const float *wrow = &W[(size_t)j * in_dim];
            for (uint32_t k = 0; k < in_dim; k++) {
                acc += xrow[k] * wrow[k];
            }
            if (B) acc += B[j];
            Yref[(size_t)t * out_dim + j] = acc;
        }
    }
}

static epa_ghs_handle_t make_payload_inline(
    epa_ghs_t *ghs,
    uint32_t n_tokens, uint32_t in_dim, uint32_t out_dim,
    int with_bias,
    float **out_X, float **out_Y, float **out_W, float **out_B,
    AtLinearOHdr_v1 **out_hdr
) {
    const size_t x_count = (size_t)n_tokens * in_dim;
    const size_t y_count = (size_t)n_tokens * out_dim;
    const size_t w_count = (size_t)out_dim * in_dim;
    const size_t b_count = (size_t)out_dim;

    uint32_t flags = AT_LINEAR_O_F_INLINE_W;
    if (with_bias) flags |= (AT_LINEAR_O_F_HAS_BIAS | AT_LINEAR_O_F_INLINE_B);

    size_t bytes = sizeof(AtLinearOHdr_v1)
                 + x_count * sizeof(float)
                 + y_count * sizeof(float)
                 + w_count * sizeof(float)
                 + (with_bias ? (b_count * sizeof(float)) : 0);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    ASSERT_TRUE(base != NULL);

    memset(base, 0, bytes);

    uint8_t *p = (uint8_t *)base;

    AtLinearOHdr_v1 *hdr = (AtLinearOHdr_v1 *)p;
    hdr->flags    = flags;
    hdr->n_tokens = n_tokens;
    hdr->in_dim   = in_dim;
    hdr->out_dim  = out_dim;
    hdr->weight_id = 0;
    hdr->bias_id   = 0;

    p += sizeof(*hdr);

    float *X = (float *)p; p += x_count * sizeof(float);
    float *Y = (float *)p; p += y_count * sizeof(float);
    float *W = (float *)p; p += w_count * sizeof(float);
    float *B = NULL;
    if (with_bias) {
        B = (float *)p;
        p += b_count * sizeof(float);
    }

    if (out_hdr) *out_hdr = hdr;
    if (out_X)   *out_X = X;
    if (out_Y)   *out_Y = Y;
    if (out_W)   *out_W = W;
    if (out_B)   *out_B = B;

    return h;
}

CTEST(test_at_linear_o_basic)
{
    epa_ghs_t *ghs = epa_ghs_create(1 << 20, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_tokens = 2;
    const uint32_t in_dim   = 4;
    const uint32_t out_dim  = 3;

    float *X = NULL, *Y = NULL, *W = NULL, *B = NULL;
    AtLinearOHdr_v1 *hdr = NULL;

    epa_ghs_handle_t h = make_payload_inline(
        ghs, n_tokens, in_dim, out_dim,
        /*with_bias=*/0,
        &X, &Y, &W, &B, &hdr
    );

    // Fill X and W with simple deterministic values (avoid rounding surprises)
    // X[t,k] = 1 + t + k
    for (uint32_t t = 0; t < n_tokens; t++) {
        for (uint32_t k = 0; k < in_dim; k++) {
            X[(size_t)t * in_dim + k] = (float)(1 + (int)t + (int)k);
        }
    }
    // W[j,k] = 0.1*(1 + j + k)
    for (uint32_t j = 0; j < out_dim; j++) {
        for (uint32_t k = 0; k < in_dim; k++) {
            W[(size_t)j * in_dim + k] = 0.1f * (float)(1 + (int)j + (int)k);
        }
    }

    // Run
    int rc = at_linear_o_run(ghs, h);
    ASSERT_TRUE(rc == AT_LINEAR_O_OK);

    // Reference compare
    float Yref[(size_t)n_tokens * out_dim];
    ref_linear_o(X, W, NULL, n_tokens, in_dim, out_dim, Yref);

    for (uint32_t i = 0; i < n_tokens * out_dim; i++) {
        ASSERT_TRUE(feq(Y[i], Yref[i]));
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_linear_o_with_bias)
{
    epa_ghs_t *ghs = epa_ghs_create(1 << 20, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_tokens = 3;
    const uint32_t in_dim   = 5;
    const uint32_t out_dim  = 4;

    float *X = NULL, *Y = NULL, *W = NULL, *B = NULL;
    AtLinearOHdr_v1 *hdr = NULL;

    epa_ghs_handle_t h = make_payload_inline(
        ghs, n_tokens, in_dim, out_dim,
        /*with_bias=*/1,
        &X, &Y, &W, &B, &hdr
    );

    // Fill X
    for (uint32_t t = 0; t < n_tokens; t++) {
        for (uint32_t k = 0; k < in_dim; k++) {
            X[(size_t)t * in_dim + k] = (float)((int)t - (int)k);
        }
    }

    // Fill W
    for (uint32_t j = 0; j < out_dim; j++) {
        for (uint32_t k = 0; k < in_dim; k++) {
            W[(size_t)j * in_dim + k] = (float)(j + 1) * 0.05f + (float)(k) * 0.01f;
        }
    }

    // Fill B
    for (uint32_t j = 0; j < out_dim; j++) {
        B[j] = (float)(j) * 0.25f;
    }

    int rc = at_linear_o_run(ghs, h);
    ASSERT_TRUE(rc == AT_LINEAR_O_OK);

    float Yref[(size_t)n_tokens * out_dim];
    ref_linear_o(X, W, B, n_tokens, in_dim, out_dim, Yref);

    for (uint32_t i = 0; i < n_tokens * out_dim; i++) {
        ASSERT_TRUE(feq(Y[i], Yref[i]));
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_linear_o_missing_weights_fails)
{
    // This test confirms the non-inline provider path fails by default
    // because at_ro_weight_matrix_get() weak stub returns NULL.
    epa_ghs_t *ghs = epa_ghs_create(1 << 20, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_tokens = 1;
    const uint32_t in_dim   = 2;
    const uint32_t out_dim  = 2;

    const size_t x_count = (size_t)n_tokens * in_dim;
    const size_t y_count = (size_t)n_tokens * out_dim;

    // payload: [hdr][X][Y] only (no inline W/B)
    const size_t bytes = sizeof(AtLinearOHdr_v1)
                       + x_count * sizeof(float)
                       + y_count * sizeof(float);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    ASSERT_TRUE(base != NULL);

    memset(base, 0, bytes);

    AtLinearOHdr_v1 *hdr = (AtLinearOHdr_v1 *)base;
    hdr->flags    = AT_LINEAR_O_F_NONE; // not inline => must use provider
    hdr->n_tokens = n_tokens;
    hdr->in_dim   = in_dim;
    hdr->out_dim  = out_dim;
    hdr->weight_id = 1234; // arbitrary

    uint8_t *p = (uint8_t *)base + sizeof(*hdr);
    float *X = (float *)p; p += x_count * sizeof(float);
    float *Y = (float *)p;

    X[0] = 1.0f; X[1] = 2.0f;
    Y[0] = 9.0f; Y[1] = 9.0f;

    int rc = at_linear_o_run(ghs, h);
    ASSERT_TRUE(rc == AT_LINEAR_O_ERR);

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

void ctest_register_test_at_linear_o(void) {
    const char *F = "test_at_linear_o.c";
    REG(F, test_at_linear_o_basic);
    REG(F, test_at_linear_o_with_bias);
    REG(F, test_at_linear_o_missing_weights_fails);
}
