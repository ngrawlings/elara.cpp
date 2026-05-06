// unittests/ctests/test_at_sample_rng.c
#include "ctest.h"

#include "atomic_tasks/at_sample_rng.h"
#include "memory/epa_ghs.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

static int feq(float a, float b) {
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d < 1e-4f;
}

static void softmax_ref(const float *logits, float *probs, uint32_t n, float temp) {
    // stable softmax(logits/temp)
    float maxv = logits[0] / temp;
    for (uint32_t i = 1; i < n; i++) {
        float v = logits[i] / temp;
        if (v > maxv) maxv = v;
    }

    double sum = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        float e = expf((logits[i] / temp) - maxv);
        probs[i] = e;
        sum += (double)e;
    }

    float inv = (sum > 0.0) ? (float)(1.0 / sum) : 0.0f;
    for (uint32_t i = 0; i < n; i++) probs[i] *= inv;
}

CTEST(test_at_sample_rng_probs_basic)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t vocab = 4;
    const size_t bytes = sizeof(AtSampleRngHdr_v1) + (size_t)vocab * sizeof(float);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t *)base;
    AtSampleRngHdr_v1 *hdr = (AtSampleRngHdr_v1 *)p;
    p += sizeof(*hdr);

    float *in = (float *)p;

    hdr->flags = AT_SAMPLER_F_INPUT_PROBS | AT_SAMPLER_F_WRITE_PROB | AT_SAMPLER_F_WRITE_LOGPROB;
    hdr->vocab = vocab;

    // deterministic RNG seed
    hdr->rng_state = 0x123456789abcdef0ULL;
    hdr->rng_inc   = 0xdeadbeefULL; // will be forced odd if needed by impl

    // probs sum to 1
    in[0] = 0.10f;
    in[1] = 0.20f;
    in[2] = 0.30f;
    in[3] = 0.40f;

    uint64_t st0 = hdr->rng_state;
    uint64_t inc0 = hdr->rng_inc;

    ASSERT_TRUE(at_sample_rng_run(ghs, h) == AT_SAMPLE_RNG_OK);

    // token valid
    ASSERT_TRUE(hdr->out_token < vocab);

    // out_prob must equal the selected probability (or 0 if something broke)
    float expected_p = in[hdr->out_token];
    ASSERT_TRUE(feq(hdr->out_prob, expected_p));

    // logprob must match log(prob)
    ASSERT_TRUE(feq(hdr->out_logprob, logf(expected_p)));

    // RNG should advance (or at least change state) unless pathological
    ASSERT_TRUE(hdr->rng_state != st0 || hdr->rng_inc != inc0);

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_sample_rng_topk_probs)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t vocab = 6;
    const size_t bytes = sizeof(AtSampleRngHdr_v1) + (size_t)vocab * sizeof(float);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t *)base;
    AtSampleRngHdr_v1 *hdr = (AtSampleRngHdr_v1 *)p;
    p += sizeof(*hdr);

    float *in = (float *)p;

    hdr->flags = AT_SAMPLER_F_INPUT_PROBS | AT_SAMPLER_F_TOPK | AT_SAMPLER_F_WRITE_PROB;
    hdr->vocab = vocab;
    hdr->top_k = 2;

    // deterministic seed
    hdr->rng_state = 0x1111111111111111ULL;
    hdr->rng_inc   = 0x2222222222222223ULL;

    // probs (top2 indices should be 4 and 2)
    // idx: 0    1    2    3    4    5
    //      .05 .10 .30 .05 .45 .05   sum=1
    in[0] = 0.05f;
    in[1] = 0.10f;
    in[2] = 0.30f;
    in[3] = 0.05f;
    in[4] = 0.45f;
    in[5] = 0.05f;

    ASSERT_TRUE(at_sample_rng_run(ghs, h) == AT_SAMPLE_RNG_OK);

    // must be in top-2: {4,2}
    ASSERT_TRUE(hdr->out_token == 4 || hdr->out_token == 2);

    // out_prob must match original in-prob for that token (implementation keeps original mass)
    ASSERT_TRUE(feq(hdr->out_prob, in[hdr->out_token]));

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_sample_rng_logits_temp)
{
	epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
	ASSERT_TRUE(ghs != NULL);

    const uint32_t vocab = 5;
    const size_t bytes = sizeof(AtSampleRngHdr_v1) + (size_t)vocab * sizeof(float);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    memset(base, 0, bytes);

    uint8_t *p = (uint8_t *)base;
    AtSampleRngHdr_v1 *hdr = (AtSampleRngHdr_v1 *)p;
    p += sizeof(*hdr);

    float *logits = (float *)p;

    hdr->flags = AT_SAMPLER_F_INPUT_LOGITS | AT_SAMPLER_F_APPLY_TEMP | AT_SAMPLER_F_WRITE_PROB;
    hdr->vocab = vocab;
    hdr->temperature = 0.7f;

    // deterministic seed
    hdr->rng_state = 0xabcdef0123456789ULL;
    hdr->rng_inc   = 0x13579bdf2468ace1ULL;

    // logits: make one clearly best but not infinite
    logits[0] = -1.0f;
    logits[1] =  0.0f;
    logits[2] =  1.0f;
    logits[3] =  2.0f;
    logits[4] = -0.5f;

    // reference probs
    float probs_ref[5];
    softmax_ref(logits, probs_ref, vocab, hdr->temperature);

    ASSERT_TRUE(at_sample_rng_run(ghs, h) == AT_SAMPLE_RNG_OK);
    ASSERT_TRUE(hdr->out_token < vocab);

    // out_prob should match reference probability for the chosen token (within eps)
    ASSERT_TRUE(feq(hdr->out_prob, probs_ref[hdr->out_token]));

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

void ctest_register_test_at_sample_rng(void) {
    const char *F = "test_at_sample_rng.c";
    REG(F, test_at_sample_rng_probs_basic);
    REG(F, test_at_sample_rng_topk_probs);
    REG(F, test_at_sample_rng_logits_temp);
}
