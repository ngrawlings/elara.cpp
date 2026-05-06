#include "at_parallel.h"

typedef struct {
  uint8_t *bits;
  uint32_t base;   // for windowing later; v0 base=0
} AtParallelCtx;

int at_parallel_exec(EpaAtBatch *b, uint32_t vtid, int32_t tid, epa_ghs_t *ghs, epa_ghs_handle_t h) {
  (void)b; (void)tid;
//  AtParallelCtx *ctx = (AtParallelCtx*)user;
//
//  uint32_t gid = ctx->base + vtid;      // global subtask id
//  uint32_t byte = gid >> 3;
//  uint32_t bit  = gid & 7u;
//
//  ctx->bits[byte] |= (uint8_t)(1u << bit);  // v0: non-atomic ok if bits are disjoint
  return 1;
}
