// unittests/ctests/test_at_flash_attn.c
#include "../ctest.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

// Core memory primitives
#include "../../src/memory/epa_ghs.h"

// Your atomic task header (adjust if needed)
#include "../../src/atomic_tasks/at_flash_attn.h"

#include "memory/epa_ghs.h"
#include "atomic_tasks/at_flash_attn.h"

static float softmax2_w0(float a0, float a1) {
    float m = (a0 > a1) ? a0 : a1;
    float e0 = expf(a0 - m);
    float e1 = expf(a1 - m);
    return e0 / (e0 + e1);
}

CTEST(test_at_flash_attn_basic)
{
    // Tiny deterministic case:
    // n_heads=1, head_dim=2, seqlen_q=1, seqlen_k=2
    // q  = [1,0]
    // k0 = [1,0], k1=[0,1]
    // v0 = [10,0], v1=[0,20]
    // scores=[1,0] => out = softmax(scores) * V

    const uint32_t n_heads  = 1;
    const uint32_t head_dim = 2;
    const uint32_t seqlen_q = 1;
    const uint32_t seqlen_k = 2;

    // Create GHS (pick a capacity; 1024 is fine for tests)
    epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const size_t q_count   = (size_t)seqlen_q * n_heads * head_dim;
    const size_t k_count   = (size_t)seqlen_k * n_heads * head_dim;
    const size_t v_count   = (size_t)seqlen_k * n_heads * head_dim;
    const size_t out_count = (size_t)seqlen_q * n_heads * head_dim;

    const size_t bytes =
        sizeof(AtFlashAttnHdr_v1) +
        (q_count + k_count + v_count + out_count) * sizeof(float);

    epa_ghs_handle_t h = 0;

    // owner=0 for kernel (matches your usual convention)
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    ASSERT_TRUE(base != NULL);

    memset(base, 0, bytes);

    uint8_t *p = (uint8_t *)base;

    AtFlashAttnHdr_v1 *hdr = (AtFlashAttnHdr_v1 *)p;
    hdr->flags    = 0;              // no causal, no scale (for this test)
    hdr->n_heads  = n_heads;
    hdr->head_dim = head_dim;
    hdr->seqlen_q = seqlen_q;
    hdr->seqlen_k = seqlen_k;

    p += sizeof(*hdr);

    float *Q   = (float *)p; p += q_count * sizeof(float);
    float *K   = (float *)p; p += k_count * sizeof(float);
    float *V   = (float *)p; p += v_count * sizeof(float);
    float *Out = (float *)p;

    // Q
    Q[0] = 1.0f; Q[1] = 0.0f;

    // K0=[1,0], K1=[0,1]
    K[0] = 1.0f; K[1] = 0.0f;
    K[2] = 0.0f; K[3] = 1.0f;

    // V0=[10,0], V1=[0,20]
    V[0] = 10.0f; V[1] = 0.0f;
    V[2] = 0.0f;  V[3] = 20.0f;

    int rc = at_flash_attn_run(ghs, h);
    ASSERT_TRUE(rc == AT_FLASH_ATTN_OK);

    float w0 = softmax2_w0(1.0f, 0.0f);
    float w1 = 1.0f - w0;

    float exp0 = w0 * 10.0f + w1 * 0.0f;
    float exp1 = w0 * 0.0f  + w1 * 20.0f;

    const float eps = 1e-5f;
    ASSERT_TRUE(fabsf(Out[0] - exp0) < eps);
    ASSERT_TRUE(fabsf(Out[1] - exp1) < eps);

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

void ctest_register_test_at_flash_attn(void) {
    const char *F = "test_at_flash_attn.c";
    REG(F, test_at_flash_attn_basic);
}
