// src/atomic_tasks/at_topk_topp_filter.c
#include "at_topk_topp_filter.h"

#include <stddef.h>
#include <string.h>
#include <math.h>

typedef struct Pair {
    float v;
    uint32_t idx;
} Pair;

static int mul_overflows_size_t(size_t a, size_t b) {
    if (a == 0 || b == 0) return 0;
    return a > (SIZE_MAX / b);
}

static void sort_desc(Pair *a, uint32_t n) {
    // simple O(n^2) sort for CPU reference (small vocab in tests)
    for (uint32_t i = 0; i < n; i++) {
        uint32_t best = i;
        for (uint32_t j = i + 1; j < n; j++) {
            if (a[j].v > a[best].v) best = j;
        }
        if (best != i) {
            Pair t = a[i];
            a[i] = a[best];
            a[best] = t;
        }
    }
}

int at_topk_topp_filter_run(epa_ghs_t *ghs, epa_ghs_handle_t h) {
    if (!ghs) return AT_TOPK_TOPP_ERR;

    void *base = NULL;
    epa_ghs_meta_t meta;
    if (epa_ghs_get_meta(ghs, h, &meta) != EPA_GHS_OK) return AT_TOPK_TOPP_ERR;
    if (epa_ghs_get_ptr(ghs, h, &base) != EPA_GHS_OK)  return AT_TOPK_TOPP_ERR;
    if (!base) return AT_TOPK_TOPP_ERR;

    if (meta.size_bytes < (uint32_t)sizeof(AtTopkToppHdr_v1)) return AT_TOPK_TOPP_ERR;

    uint8_t *p = (uint8_t *)base;
    AtTopkToppHdr_v1 *hdr = (AtTopkToppHdr_v1 *)p;

    const uint32_t flags = hdr->flags;
    const uint32_t vocab = hdr->vocab;

    const int in_logits = (flags & AT_TKTP_F_INPUT_LOGITS) != 0;
    const int in_probs  = (flags & AT_TKTP_F_INPUT_PROBS)  != 0;
    if ((in_logits + in_probs) != 1) return AT_TOPK_TOPP_ERR;

    if (vocab == 0) return AT_TOPK_TOPP_ERR;
    if (mul_overflows_size_t((size_t)vocab, sizeof(float))) return AT_TOPK_TOPP_ERR;

    const int do_topk = (flags & AT_TKTP_F_DO_TOPK) != 0;
    const int do_topp = (flags & AT_TKTP_F_DO_TOPP) != 0;
    const int do_minp = (flags & AT_TKTP_F_DO_MINP) != 0;

    uint32_t keep_at_least = 0;
    if (flags & AT_TKTP_F_KEEP_AT_LEAST) {
        keep_at_least = hdr->keep_at_least;
        if (keep_at_least == 0) keep_at_least = 1;
        if (keep_at_least > vocab) keep_at_least = vocab;
    }

    if (do_topk) {
        if (hdr->top_k == 0) return AT_TOPK_TOPP_ERR;
    }
    if (do_topp) {
        if (!(hdr->top_p > 0.0f && hdr->top_p <= 1.0f)) return AT_TOPK_TOPP_ERR;
    }
    if (do_minp) {
        if (!(hdr->min_p >= 0.0f && hdr->min_p <= 1.0f)) return AT_TOPK_TOPP_ERR;
    }

    p += sizeof(AtTopkToppHdr_v1);
    float *vals = (float *)p;

    size_t need = sizeof(AtTopkToppHdr_v1) + (size_t)vocab * sizeof(float);
    if ((size_t)meta.size_bytes < need) return AT_TOPK_TOPP_ERR;

    uint32_t *kept_out = NULL;
    if (flags & AT_TKTP_F_WRITE_KEPT) {
        // require space for vocab uint32 indices (max)
        size_t need2 = need + (size_t)vocab * sizeof(uint32_t);
        if ((size_t)meta.size_bytes < need2) return AT_TOPK_TOPP_ERR;
        kept_out = (uint32_t *)(void *)((uint8_t *)vals + (size_t)vocab * sizeof(float));
    }

    // Build pairs
    Pair pairs[vocab];
    for (uint32_t i = 0; i < vocab; i++) {
        float v = vals[i];

        if (in_probs) {
            // clamp negatives to 0 for safety
            if (!(v >= 0.0f)) v = 0.0f;
        } else {
            // logits: allow -inf etc; NaNs become -inf
            if (!isfinite(v)) {
                v = -INFINITY;
            }
        }

        pairs[i].v = v;
        pairs[i].idx = i;
    }

    // Sort descending (recommended for both logits/probs filtering)
    if (flags & AT_TKTP_F_SORT_DESC) {
        sort_desc(pairs, vocab);
    } else {
        // If not sorted, we can't do nucleus/top-k meaningfully.
        // Keep it strict.
        return AT_TOPK_TOPP_ERR;
    }

    // Determine initial keep mask (all false)
    uint8_t keep[vocab];
    memset(keep, 0, sizeof(keep));

    // If probs input, nucleus uses cumulative probability directly.
    // If logits input, nucleus is not well-defined without softmax, so:
    // - we still approximate nucleus by cumulative of exp(logits - max), but that implies a softmax.
    //   For now: only allow TOPP on probs mode.
    if (do_topp && in_logits) return AT_TOPK_TOPP_ERR;

    // Start with all candidates
    uint32_t n_keep = vocab;

    // Apply top-k
    if (do_topk) {
        uint32_t k = hdr->top_k;
        if (k > vocab) k = vocab;
        n_keep = k;
    }

    // Apply min_p (probs mode only; for logits, caller should do after softmax)
    // min_p is absolute threshold in probability space.
    if (do_minp) {
        if (in_logits) return AT_TOPK_TOPP_ERR;
        // We'll later enforce by zeroing anything below min_p, but ensure keep_at_least.
    }

    // Apply top-p (nucleus) (probs mode only)
    uint32_t topp_keep = vocab;
    if (do_topp) {
        double cum = 0.0;
        topp_keep = 0;
        for (uint32_t i = 0; i < vocab; i++) {
            cum += (double)pairs[i].v;
            topp_keep++;
            if (cum >= (double)hdr->top_p) break;
        }
        if (topp_keep == 0) topp_keep = 1;
        if (topp_keep > vocab) topp_keep = vocab;
    }

    // Combine keep count from top-k and top-p: keep the stricter (smaller) set
    uint32_t keep_count = vocab;
    if (do_topk) keep_count = n_keep;
    if (do_topp && topp_keep < keep_count) keep_count = topp_keep;

    // Enforce keep_at_least
    if (keep_at_least && keep_at_least > keep_count) keep_count = keep_at_least;

    // Mark kept indices from the sorted prefix
    for (uint32_t i = 0; i < keep_count; i++) {
        keep[pairs[i].idx] = 1;
    }

    // Apply min_p (drop entries < min_p, but not below keep_at_least)
    if (do_minp) {
        // First pass: drop below threshold
        uint32_t kept_now = 0;
        for (uint32_t i = 0; i < vocab; i++) {
            if (keep[pairs[i].idx]) {
                float pv = pairs[i].v;
                if (pv < hdr->min_p) {
                    keep[pairs[i].idx] = 0;
                } else {
                    kept_now++;
                }
            }
        }

        // If we dropped too many, re-add from the top sorted list until keep_at_least
        if (keep_at_least && kept_now < keep_at_least) {
            for (uint32_t i = 0; i < vocab && kept_now < keep_at_least; i++) {
                uint32_t idx = pairs[i].idx;
                if (!keep[idx]) {
                    keep[idx] = 1;
                    kept_now++;
                }
            }
        }
    }

    // Write back filtered values
    const float removed_val = in_logits ? -INFINITY : 0.0f;

    uint32_t final_kept = 0;
    if (kept_out) {
        // output kept indices in descending order (pairs order)
        for (uint32_t i = 0; i < vocab; i++) {
            uint32_t idx = pairs[i].idx;
            if (keep[idx]) {
                kept_out[final_kept++] = idx;
            }
        }
        hdr->kept_count = final_kept;
    }

    for (uint32_t i = 0; i < vocab; i++) {
        if (!keep[i]) vals[i] = removed_val;
    }

    return AT_TOPK_TOPP_OK;
}
