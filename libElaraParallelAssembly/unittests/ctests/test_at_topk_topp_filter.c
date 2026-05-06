// unittests/ctests/test_at_topk_topp_filter.c
#include "ctest.h"

#include "atomic_tasks/at_topk_topp_filter.h"
#include "memory/epa_ghs.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

static int is_neg_inf(float x) {
    return isinf(x) && (x < 0.0f);
}

static int feq(float a, float b) {
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d < 1e-6f;
}

CTEST(test_at_topk_logits_topk_write_kept)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t vocab = 6;
    const size_t bytes =
        sizeof(AtTopkToppHdr_v1) +
        (size_t)vocab * sizeof(float) +
        (size_t)vocab * sizeof(uint32_t); // kept indices buffer

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t*)base;
    AtTopkToppHdr_v1 *hdr = (AtTopkToppHdr_v1*)p;
    p += sizeof(*hdr);

    float *logits = (float*)p;
    p += (size_t)vocab * sizeof(float);

    uint32_t *kept = (uint32_t*)p;

    hdr->flags = AT_TKTP_F_INPUT_LOGITS |
                 AT_TKTP_F_DO_TOPK |
                 AT_TKTP_F_SORT_DESC |
                 AT_TKTP_F_WRITE_KEPT;
    hdr->vocab = vocab;
    hdr->top_k = 3;

    // logits: top-3 should be idx 1 (9), idx 4 (7), idx 2 (5)
    // others should become -INF
    logits[0] = 1.0f;
    logits[1] = 9.0f;
    logits[2] = 5.0f;
    logits[3] = -2.0f;
    logits[4] = 7.0f;
    logits[5] = 0.0f;

    ASSERT_TRUE(at_topk_topp_filter_run(ghs, h) == AT_TOPK_TOPP_OK);

    // Verify kept_count == 3
    ASSERT_TRUE(hdr->kept_count == 3);

    // Verify kept indices are in descending order: [1,4,2]
    ASSERT_TRUE(kept[0] == 1);
    ASSERT_TRUE(kept[1] == 4);
    ASSERT_TRUE(kept[2] == 2);

    // Verify in-place filtering: only those indices are NOT -INF
    for (uint32_t i = 0; i < vocab; i++) {
        if (i == 1 || i == 4 || i == 2) {
            ASSERT_TRUE(!is_neg_inf(logits[i]));
        } else {
            ASSERT_TRUE(is_neg_inf(logits[i]));
        }
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_topk_probs_topp)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t vocab = 6;
    const size_t bytes =
        sizeof(AtTopkToppHdr_v1) +
        (size_t)vocab * sizeof(float);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t*)base;
    AtTopkToppHdr_v1 *hdr = (AtTopkToppHdr_v1*)p;
    p += sizeof(*hdr);

    float *probs = (float*)p;

    hdr->flags = AT_TKTP_F_INPUT_PROBS |
                 AT_TKTP_F_DO_TOPP |
                 AT_TKTP_F_SORT_DESC;
    hdr->vocab = vocab;
    hdr->top_p = 0.70f;

    // probs sum to 1:
    // idx: 0    1    2    3    4    5
    //      .40 .20 .15 .10 .10 .05
    // sorted desc: 0(.40),1(.20),2(.15),3(.10),4(.10),5(.05)
    // minimal prefix reaching 0.70 is: .40+.20+.15 = .75 -> keep {0,1,2}
    probs[0] = 0.40f;
    probs[1] = 0.20f;
    probs[2] = 0.15f;
    probs[3] = 0.10f;
    probs[4] = 0.10f;
    probs[5] = 0.05f;

    ASSERT_TRUE(at_topk_topp_filter_run(ghs, h) == AT_TOPK_TOPP_OK);

    // Kept should be {0,1,2}, others zeroed
    ASSERT_TRUE(feq(probs[0], 0.40f));
    ASSERT_TRUE(feq(probs[1], 0.20f));
    ASSERT_TRUE(feq(probs[2], 0.15f));
    ASSERT_TRUE(feq(probs[3], 0.0f));
    ASSERT_TRUE(feq(probs[4], 0.0f));
    ASSERT_TRUE(feq(probs[5], 0.0f));

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_topk_probs_minp_keep_at_least_write_kept)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t vocab = 5;
    const size_t bytes =
        sizeof(AtTopkToppHdr_v1) +
        (size_t)vocab * sizeof(float) +
        (size_t)vocab * sizeof(uint32_t);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t*)base;
    AtTopkToppHdr_v1 *hdr = (AtTopkToppHdr_v1*)p;
    p += sizeof(*hdr);

    float *probs = (float*)p;
    p += (size_t)vocab * sizeof(float);

    uint32_t *kept = (uint32_t*)p;

    hdr->flags = AT_TKTP_F_INPUT_PROBS |
                 AT_TKTP_F_DO_MINP |
                 AT_TKTP_F_KEEP_AT_LEAST |
                 AT_TKTP_F_SORT_DESC |
                 AT_TKTP_F_WRITE_KEPT;
    hdr->vocab = vocab;
    hdr->min_p = 0.18f;
    hdr->keep_at_least = 2;

    // probs:
    // idx: 0    1    2    3    4
    //      .50 .20 .15 .10 .05
    // min_p=0.18 drops {2,3,4} but keep_at_least=2 ensures keep {0,1}
    probs[0] = 0.50f;
    probs[1] = 0.20f;
    probs[2] = 0.15f;
    probs[3] = 0.10f;
    probs[4] = 0.05f;

    ASSERT_TRUE(at_topk_topp_filter_run(ghs, h) == AT_TOPK_TOPP_OK);

    // Kept_count should be 2 and kept indices should be [0,1]
    ASSERT_TRUE(hdr->kept_count == 2);
    ASSERT_TRUE(kept[0] == 0);
    ASSERT_TRUE(kept[1] == 1);

    // In-place: {0,1} unchanged, others zeroed
    ASSERT_TRUE(feq(probs[0], 0.50f));
    ASSERT_TRUE(feq(probs[1], 0.20f));
    ASSERT_TRUE(feq(probs[2], 0.0f));
    ASSERT_TRUE(feq(probs[3], 0.0f));
    ASSERT_TRUE(feq(probs[4], 0.0f));

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

void ctest_register_test_at_topk(void) {
    const char *F = "test_at_topk.c";
    REG(F, test_at_topk_logits_topk_write_kept);
    REG(F, test_at_topk_probs_topp);
    REG(F, test_at_topk_probs_minp_keep_at_least_write_kept);
}
