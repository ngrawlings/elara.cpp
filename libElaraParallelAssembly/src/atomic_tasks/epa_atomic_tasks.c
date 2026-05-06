// atomic_tasks/epa_atomic_tasks.c
#include "epa_atomic_tasks.h"
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

#ifndef EPA_AT_MAX_TASKS
#define EPA_AT_MAX_TASKS 65536u
#endif

// Task registry (helper)
static EpaAtomicTaskFn g_at_table[EPA_AT_MAX_TASKS];

// Embed registry (keep tiny for now; expand later)
#ifndef EPA_AT_MAX_EMBED_TABLES
#define EPA_AT_MAX_EMBED_TABLES 64u
#endif

typedef struct EmbedSlot {
  uint32_t used;
  uint32_t id;
  EpaAtEmbedTable t;
} EmbedSlot;

static EmbedSlot g_embed[EPA_AT_MAX_EMBED_TABLES];

EpaAtRc epa_at_register(uint16_t at_id, EpaAtomicTaskFn fn) {
  g_at_table[at_id] = fn;
  return EPA_AT_OK;
}

EpaAtomicTaskFn epa_at_get(uint16_t at_id) {
  return g_at_table[at_id];
}

EpaAtRc epa_at_run(uint16_t at_id, epa_ghs_t* ghs, epa_ghs_handle_t h) {
  EpaAtomicTaskFn fn = epa_at_get(at_id);
  if (!fn) return EPA_AT_ERR_UNSUPPORTED;
  return fn(ghs, h);
}

EpaAtRc epa_at_register_embed_table(uint32_t table_id,
                                    const float* data,
                                    uint32_t vocab,
                                    uint32_t d_model) {
  if (!data || vocab == 0 || d_model == 0) return EPA_AT_ERR_NULL;

  // update if exists
  for (uint32_t i = 0; i < EPA_AT_MAX_EMBED_TABLES; i++) {
    if (g_embed[i].used && g_embed[i].id == table_id) {
      g_embed[i].t.data = data;
      g_embed[i].t.vocab = vocab;
      g_embed[i].t.d_model = d_model;
      return EPA_AT_OK;
    }
  }

  // insert
  for (uint32_t i = 0; i < EPA_AT_MAX_EMBED_TABLES; i++) {
    if (!g_embed[i].used) {
      g_embed[i].used = 1;
      g_embed[i].id = table_id;
      g_embed[i].t.data = data;
      g_embed[i].t.vocab = vocab;
      g_embed[i].t.d_model = d_model;
      return EPA_AT_OK;
    }
  }

  return EPA_AT_ERR_UNSUPPORTED;
}

EpaAtRc epa_at_get_embed_table(uint32_t table_id, EpaAtEmbedTable* out) {
  if (!out) return EPA_AT_ERR_NULL;
  for (uint32_t i = 0; i < EPA_AT_MAX_EMBED_TABLES; i++) {
    if (g_embed[i].used && g_embed[i].id == table_id) {
      *out = g_embed[i].t;
      return EPA_AT_OK;
    }
  }
  return EPA_AT_ERR_UNSUPPORTED;
}

static _Atomic uint32_t g_batch_id = 1;

EpaAtRc epa_at_batch_create(uint16_t at_id,
                            uint32_t block_count,
                            EpaAtBatch **out_batch) {
  if (!out_batch) return EPA_AT_ERR_NULL;
  *out_batch = NULL;

  if (block_count == 0) return EPA_AT_ERR_BAD_DESC;

  EpaAtBatch *b = (EpaAtBatch*)calloc(1, sizeof(EpaAtBatch));
  if (!b) return EPA_AT_ERR_UNSUPPORTED;

  b->batch_id = atomic_fetch_add(&g_batch_id, 1);
  b->at_id = at_id;
  b->block_count = block_count;

  atomic_store(&b->done_count, 0);
  atomic_store(&b->fault_count, 0);
  atomic_store(&b->scan_cursor, 0);

  b->state = (_Atomic uint8_t*)calloc(block_count, sizeof(_Atomic uint8_t));
  b->owner_tid = (_Atomic int32_t*)calloc(block_count, sizeof(_Atomic int32_t));
  if (!b->state || !b->owner_tid) {
    epa_at_batch_destroy(b);
    return EPA_AT_ERR_UNSUPPORTED;
  }

  for (uint32_t i = 0; i < block_count; i++) {
    atomic_store(&b->state[i], (uint8_t)EPA_AT_BLOCK_TODO);
    atomic_store(&b->owner_tid[i], -1);
  }

  *out_batch = b;
  return EPA_AT_OK;
}

void epa_at_batch_destroy(EpaAtBatch *b) {
  if (!b) return;
  free((void*)b->state);
  free((void*)b->owner_tid);
  free(b);
}

EpaAtRc epa_at_batch_claim(EpaAtBatch *b,
                           int32_t tid,
                           uint32_t *out_block_index) {
  if (!b || !out_block_index) return EPA_AT_ERR_NULL;

  uint32_t n = b->block_count;
  if (n == 0) return EPA_AT_ERR_BAD_DESC;

  // If already done, nothing to claim
  if (atomic_load(&b->done_count) >= n) return EPA_AT_ERR_OOB;

  // Scan starting from a moving cursor to reduce contention.
  uint32_t start = atomic_fetch_add(&b->scan_cursor, 1) % n;

  for (uint32_t probe = 0; probe < n; probe++) {
    uint32_t idx = (start + probe) % n;

    uint8_t expect = (uint8_t)EPA_AT_BLOCK_TODO;
    if (atomic_compare_exchange_strong(&b->state[idx], &expect,
                                       (uint8_t)EPA_AT_BLOCK_INFLIGHT)) {
      // we successfully claimed it
      atomic_store(&b->owner_tid[idx], tid);
      *out_block_index = idx;
      return EPA_AT_OK;
    }
  }

  // Nothing runnable right now (all inflight/done/fault)
  return EPA_AT_ERR_OOB;
}

EpaAtRc epa_at_batch_complete(EpaAtBatch *b,
                              int32_t tid,
                              uint32_t block_index,
                              int ok) {
  if (!b) return EPA_AT_ERR_NULL;
  if (block_index >= b->block_count) return EPA_AT_ERR_OOB;

  int32_t owner = atomic_load(&b->owner_tid[block_index]);
  if (owner != tid) return EPA_AT_ERR_OWNERSHIP;

  // Only INFLIGHT blocks can complete
  uint8_t st = atomic_load(&b->state[block_index]);
  if (st != (uint8_t)EPA_AT_BLOCK_INFLIGHT) return EPA_AT_ERR_BAD_DESC;

  if (ok) {
    atomic_store(&b->state[block_index], (uint8_t)EPA_AT_BLOCK_DONE);
    atomic_fetch_add(&b->done_count, 1);
  } else {
    atomic_store(&b->state[block_index], (uint8_t)EPA_AT_BLOCK_FAULT);
    atomic_fetch_add(&b->fault_count, 1);
  }

  atomic_store(&b->owner_tid[block_index], -1);
  return EPA_AT_OK;
}

int epa_at_batch_exec_block(EpaAtBatch *b, uint32_t vtid, int32_t tid) {
  if (!b || !b->exec_fn) return 0;
  return b->exec_fn(b, vtid, tid, b->user) ? 1 : 0;
}
