// unittests/ctests/test_at_lm_head_linear.c
#include "ctest.h"

#include "memory/epa_ghs.h"
#include "atomic_tasks/at_lm_head_linear.h"

#include <string.h>
#include <math.h>

CTEST(test_at_lm_head_linear_basic)
{
    // Create GHS (pick a capacity; 1024 is fine for tests)
    epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_tokens   = 2;
    const uint32_t d_model    = 3;
    const uint32_t vocab_size = 4;

    const size_t x_count   = n_tokens * d_model;
    const size_t w_count   = vocab_size * d_model;
    const size_t b_count   = vocab_size;
    const size_t out_count = n_tokens * vocab_size;

    const size_t bytes =
        sizeof(AtLmHeadHdr_v1) +
        x_count   * sizeof(float) +
        out_count * sizeof(float) +
        w_count   * sizeof(float) +
        b_count   * sizeof(float);

    epa_ghs_handle_t h;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);

    uint8_t *p = (uint8_t *)base;

    AtLmHeadHdr_v1 *hdr = (AtLmHeadHdr_v1 *)p;
    memset(hdr, 0, sizeof(*hdr));
    hdr->flags = AT_LM_HEAD_F_INLINE_W | AT_LM_HEAD_F_INLINE_B | AT_LM_HEAD_F_HAS_BIAS;
    hdr->n_tokens   = n_tokens;
    hdr->d_model    = d_model;
    hdr->vocab_size = vocab_size;
    p += sizeof(*hdr);

    float *X = (float *)p;
    p += x_count * sizeof(float);

    float *LOGITS = (float *)p;
    p += out_count * sizeof(float);

    float *W = (float *)p;
    p += w_count * sizeof(float);

    float *B = (float *)p;

    // ---- Initialize deterministic data ----

    // X[t,k] = t*10 + k
    for (uint32_t t = 0; t < n_tokens; t++) {
        for (uint32_t k = 0; k < d_model; k++) {
            X[t*d_model + k] = (float)(t * 10 + k);
        }
    }

    // W[v,k] = v*100 + k
    for (uint32_t v = 0; v < vocab_size; v++) {
        for (uint32_t k = 0; k < d_model; k++) {
            W[v*d_model + k] = (float)(v * 100 + k);
        }
    }

    // B[v] = v
    for (uint32_t v = 0; v < vocab_size; v++) {
        B[v] = (float)v;
    }

    // ---- Run AT ----
    ASSERT_TRUE(at_lm_head_linear_run(ghs, h) == AT_LM_HEAD_OK);

    // ---- Verify reference ----
    for (uint32_t t = 0; t < n_tokens; t++) {
        for (uint32_t v = 0; v < vocab_size; v++) {
            float acc = 0.0f;
            for (uint32_t k = 0; k < d_model; k++) {
                acc += X[t*d_model + k] * W[v*d_model + k];
            }
            acc += B[v];

            float got = LOGITS[t*vocab_size + v];
            ASSERT_TRUE(fabsf(got - acc) < 1e-4f);
        }
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

void ctest_register_test_at_lm_head_linear(void) {
    const char *F = "test_at_lm_head_linear.c";
    REG(F, test_at_lm_head_linear_basic);
}

