// src/atomic_tasks/at_sample_rng.c
#include "at_sample_rng.h"

#include <stddef.h>
#include <string.h>
#include <math.h>

// ---- PCG32 (portable, deterministic) ----
static inline uint32_t pcg32_random(uint64_t *state, uint64_t inc) {
    uint64_t oldstate = *state;
    // Advance internal state
    *state = oldstate * 6364136223846793005ULL + (inc | 1ULL);
    // Calculate output function (XSH RR), uses old state
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = (uint32_t)(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-(int32_t)rot) & 31));
}

static inline float pcg32_float01(uint64_t *state, uint64_t inc) {
    // Uniform in [0,1)
    uint32_t r = pcg32_random(state, inc);
    // 24-bit mantissa float
    return (r >> 8) * (1.0f / 16777216.0f);
}

static int mul_overflows_size_t(size_t a, size_t b) {
    if (a == 0 || b == 0) return 0;
    return a > (SIZE_MAX / b);
}

typedef struct Pair {
    float v;
    uint32_t idx;
} Pair;

static void partial_topk(Pair *arr, uint32_t n, uint32_t k) {
    // Very simple O(n*k) selection (fine for CPU reference / tests).
    // Later CUDA/ASIC can use radix/topk kernels.
    for (uint32_t i = 0; i < k; i++) {
        uint32_t best = i;
        for (uint32_t j = i + 1; j < n; j++) {
            if (arr[j].v > arr[best].v) best = j;
        }
        if (best != i) {
            Pair tmp = arr[i];
            arr[i] = arr[best];
            arr[best] = tmp;
        }
    }
}

int at_sample_rng_run(epa_ghs_t *ghs, epa_ghs_handle_t h) {
    if (!ghs) return AT_SAMPLE_RNG_ERR;

    void *base = NULL;
    epa_ghs_meta_t meta;
    if (epa_ghs_get_meta(ghs, h, &meta) != EPA_GHS_OK) return AT_SAMPLE_RNG_ERR;
    if (epa_ghs_get_ptr(ghs, h, &base) != EPA_GHS_OK)  return AT_SAMPLE_RNG_ERR;
    if (!base) return AT_SAMPLE_RNG_ERR;

    if (meta.size_bytes < (uint32_t)sizeof(AtSampleRngHdr_v1)) return AT_SAMPLE_RNG_ERR;

    uint8_t *p = (uint8_t *)base;
    AtSampleRngHdr_v1 *hdr = (AtSampleRngHdr_v1 *)p;

    const uint32_t flags = hdr->flags;
    const uint32_t vocab = hdr->vocab;

    const int in_logits = (flags & AT_SAMPLER_F_INPUT_LOGITS) != 0;
    const int in_probs  = (flags & AT_SAMPLER_F_INPUT_PROBS)  != 0;

    if ((in_logits + in_probs) != 1) return AT_SAMPLE_RNG_ERR;
    if (vocab == 0) return AT_SAMPLE_RNG_ERR;

    if (mul_overflows_size_t((size_t)vocab, sizeof(float))) return AT_SAMPLE_RNG_ERR;

    size_t needed = sizeof(AtSampleRngHdr_v1) + (size_t)vocab * sizeof(float);
    if ((size_t)meta.size_bytes < needed) return AT_SAMPLE_RNG_ERR;

    p += sizeof(AtSampleRngHdr_v1);
    const float *in = (const float *)p;

    uint64_t st  = hdr->rng_state;
    uint64_t inc = hdr->rng_inc;

    // Default inc if user didn't set it (must be odd)
    if ((inc & 1ULL) == 0ULL) inc |= 1ULL;

    // Allocate temp arrays on stack for reference (vocab can be big in real use,
    // but for CPU bringup tests it will be small; later versions will stream / scratch in GHS).
    // If you want safety, you can reject very large vocab here.
    // We'll handle top-k by building a Pair array.
    // NOTE: C99 VLA used here for simplicity.
    Pair pairs[vocab];

    if (in_probs) {
        for (uint32_t i = 0; i < vocab; i++) {
            float v = in[i];
            if (!(v >= 0.0f)) v = 0.0f; // clamp negatives to 0
            pairs[i].v = v;
            pairs[i].idx = i;
        }
    } else {
        // logits
        float temp = 1.0f;
        if (flags & AT_SAMPLER_F_APPLY_TEMP) {
            if (!(hdr->temperature > 0.0f)) return AT_SAMPLE_RNG_ERR;
            temp = hdr->temperature;
        }

        // We'll softmax in a numerically stable way (with optional temp scaling).
        // 1) find max(logit/temp)
        float maxv = -INFINITY;
        for (uint32_t i = 0; i < vocab; i++) {
            float v = in[i] / temp;
            if (v > maxv) maxv = v;
        }
        if (!isfinite(maxv)) return AT_SAMPLE_RNG_ERR;

        // 2) exp and sum
        double sum = 0.0;
        for (uint32_t i = 0; i < vocab; i++) {
            float e = expf((in[i] / temp) - maxv);
            pairs[i].v = e;
            pairs[i].idx = i;
            sum += (double)e;
        }
        if (!(sum > 0.0)) return AT_SAMPLE_RNG_ERR;

        // normalize to probs
        float inv_sum = (float)(1.0 / sum);
        for (uint32_t i = 0; i < vocab; i++) pairs[i].v *= inv_sum;
    }

    // Top-k restriction
    uint32_t k = vocab;
    if (flags & AT_SAMPLER_F_TOPK) {
        if (hdr->top_k == 0) return AT_SAMPLE_RNG_ERR;
        k = hdr->top_k;
        if (k > vocab) k = vocab;
        partial_topk(pairs, vocab, k);
        // zero out everything outside k
        for (uint32_t i = k; i < vocab; i++) pairs[i].v = 0.0f;
    }

    // Sample from categorical distribution over pairs[*].v
    // (If top-k used, only first k items have mass)
    double total = 0.0;
    for (uint32_t i = 0; i < vocab; i++) total += (double)pairs[i].v;
    if (!(total > 0.0)) return AT_SAMPLE_RNG_ERR;

    float u = pcg32_float01(&st, inc);
    double threshold = (double)u * total;

    double acc = 0.0;
    uint32_t chosen = pairs[0].idx;
    float chosen_p = pairs[0].v;

    for (uint32_t i = 0; i < vocab; i++) {
        acc += (double)pairs[i].v;
        if (acc >= threshold) {
            chosen = pairs[i].idx;
            chosen_p = pairs[i].v;
            break;
        }
    }

    hdr->out_token = chosen;

    if (flags & AT_SAMPLER_F_WRITE_PROB) {
        hdr->out_prob = chosen_p;
    }
    if (flags & AT_SAMPLER_F_WRITE_LOGPROB) {
        // avoid log(0)
        float pval = (chosen_p > 0.0f) ? chosen_p : 1e-30f;
        hdr->out_logprob = logf(pval);
    }

    // persist RNG state
    hdr->rng_state = st;
    hdr->rng_inc   = inc;

    return AT_SAMPLE_RNG_OK;
}
