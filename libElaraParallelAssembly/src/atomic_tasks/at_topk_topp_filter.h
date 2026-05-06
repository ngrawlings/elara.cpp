// src/atomic_tasks/at_topk_topp_filter.h
#pragma once
#include <stdint.h>

#include "../memory/epa_ghs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AT_TOPK_TOPP_OK   0
#define AT_TOPK_TOPP_ERR -1

// Input mode
#define AT_TKTP_F_INPUT_LOGITS   (1u << 0)  // treat array as logits; removed -> -INF
#define AT_TKTP_F_INPUT_PROBS    (1u << 1)  // treat array as probabilities; removed -> 0

// Operations
#define AT_TKTP_F_DO_TOPK        (1u << 2)
#define AT_TKTP_F_DO_TOPP        (1u << 3)
#define AT_TKTP_F_DO_MINP        (1u << 4)

// Behavior
#define AT_TKTP_F_SORT_DESC      (1u << 5)  // always sort desc for filtering (recommended)
#define AT_TKTP_F_KEEP_AT_LEAST  (1u << 6)  // enforce keep_at_least >= 1

// Output
#define AT_TKTP_F_WRITE_KEPT     (1u << 7)  // write kept_count + kept_indices list

typedef struct AtTopkToppHdr_v1 {
    uint32_t flags;

    uint32_t vocab;

    uint32_t top_k;          // used if DO_TOPK
    float    top_p;          // used if DO_TOPP (0..1]
    float    min_p;          // used if DO_MINP (0..1], interpreted as absolute threshold on probs
                             // (If you run this on logits, you should set min_p=0 or do min_p later.)

    uint32_t keep_at_least;  // if KEEP_AT_LEAST set: keep >= this many, regardless of top_p/top_k/min_p
    uint32_t kept_count;     // output if WRITE_KEPT

    uint32_t rsv0;
    uint32_t rsv1;
} AtTopkToppHdr_v1;

// Layout (v1):
// [AtTopkToppHdr_v1]
// [values float32 vocab]         // logits or probs (in-place modified)
// [kept_indices uint32 kept]     // optional output buffer if WRITE_KEPT is set; must have capacity vocab

int at_topk_topp_filter_run(epa_ghs_t *ghs, epa_ghs_handle_t h);

#ifdef __cplusplus
}
#endif
