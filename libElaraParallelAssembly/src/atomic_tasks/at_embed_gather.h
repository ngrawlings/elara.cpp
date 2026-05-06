// atomic_tasks/at_embed_gather.h
#pragma once
#include <stdint.h>
#include "../memory/epa_ghs.h"
#include "epa_atomic_tasks.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pick any stable ID you like (later you’ll map opcode->AT_ID)
#define AT_EMBED_GATHER  0x0001u

// Descriptor lives inside GHS payload at EPA_AT_DESC_OFFSET (v0).
// All offsets are byte offsets from start of GHS payload.
typedef struct AtEmbedGatherDesc {
  uint32_t embed_table_id;

  uint32_t token_count;     // N tokens
  uint32_t tokens_off;      // u32[N]
  uint32_t out_off;         // f32[N * d_model]

  // Optional sanity fields (task validates these match registered table)
  uint32_t d_model;         // expected d_model
  uint32_t reserved;
} AtEmbedGatherDesc;

// Task entrypoint
EpaAtRc at_embed_gather_cpu(epa_ghs_t* ghs, epa_ghs_handle_t h);

// Helper to register into the AT table
static inline void at_embed_gather_register(void) {
  (void)epa_at_register(AT_EMBED_GATHER, at_embed_gather_cpu);
}

#ifdef __cplusplus
}
#endif
