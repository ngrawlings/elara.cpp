// src/atomic_tasks/at_sample_rng.h
#pragma once
#include <stdint.h>

#include "../memory/epa_ghs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AT_SAMPLE_RNG_OK   0
#define AT_SAMPLE_RNG_ERR -1

// Flags
#define AT_SAMPLER_F_NONE          0u
#define AT_SAMPLER_F_INPUT_LOGITS  (1u << 0)  // input array is logits (float32)
#define AT_SAMPLER_F_INPUT_PROBS   (1u << 1)  // input array is probabilities (float32), sum ~ 1

#define AT_SAMPLER_F_APPLY_TEMP    (1u << 2)  // apply temperature to logits (ignored if INPUT_PROBS)
#define AT_SAMPLER_F_TOPK          (1u << 3)  // restrict to top-k tokens

#define AT_SAMPLER_F_WRITE_PROB    (1u << 4)  // write chosen prob to output
#define AT_SAMPLER_F_WRITE_LOGPROB (1u << 5)  // write chosen log(prob) to output (natural log)

// Must be exactly one of INPUT_LOGITS / INPUT_PROBS
// If both or neither set -> error.

typedef struct AtSampleRngHdr_v1 {
    uint32_t flags;

    uint32_t vocab;        // number of entries in input array
    uint32_t top_k;        // used if TOPK flag set; 0 => error, >vocab clamps
    float    temperature;  // used if APPLY_TEMP (must be > 0)

    // RNG state (PCG32)
    // Keep in payload to be deterministic and portable.
    uint64_t rng_state;
    uint64_t rng_inc;

    // Output
    uint32_t out_token;    // selected token id
    uint32_t rsv0;

    float    out_prob;     // only valid if WRITE_PROB
    float    out_logprob;  // only valid if WRITE_LOGPROB

    uint32_t rsv1;
    uint32_t rsv2;
} AtSampleRngHdr_v1;

// Layout (v1):
// [AtSampleRngHdr_v1]
// [input float32 vocab]  (logits or probs)

int at_sample_rng_run(epa_ghs_t *ghs, epa_ghs_handle_t h);

#ifdef __cplusplus
}
#endif
