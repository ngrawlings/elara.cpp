// atomic_tasks/epa_atomic_tasks.h
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>

#include "../memory/epa_ghs.h"   // adjust include path to your tree

#ifdef __cplusplus
extern "C" {
#endif

// --------------------
// AtomicTask return codes
// --------------------
typedef enum EpaAtRc {
  EPA_AT_OK = 0,
  EPA_AT_ERR_NULL = -1,
  EPA_AT_ERR_BAD_HANDLE = -2,
  EPA_AT_ERR_BAD_DESC = -3,
  EPA_AT_ERR_OOB = -4,
  EPA_AT_ERR_UNSUPPORTED = -5,
  EPA_AT_ERR_OWNERSHIP = -6
} EpaAtRc;

// --------------------
// Descriptor conventions
// --------------------
// v0: descriptor starts at offset 0 inside the GHS payload.
// Later you can move this behind routing headers and keep a constant macro.
#ifndef EPA_AT_DESC_OFFSET
#define EPA_AT_DESC_OFFSET 0u
#endif

// A single AtomicTask function signature (CPU reference today, CUDA later)
typedef EpaAtRc (*EpaAtomicTaskFn)(epa_ghs_t* ghs, epa_ghs_handle_t h);

// Registry for tasks (helper only; opcode bridge later)
EpaAtRc epa_at_register(uint16_t at_id, EpaAtomicTaskFn fn);
EpaAtomicTaskFn epa_at_get(uint16_t at_id);

// Convenience: run a task by id (still not opcode-bridge)
EpaAtRc epa_at_run(uint16_t at_id, epa_ghs_t* ghs, epa_ghs_handle_t h);

// --------------------
// Weight registries (v0)
// --------------------
// Embedding table registry (global RO weights)
typedef struct EpaAtEmbedTable {
  const float* data;     // row-major: [vocab][d_model]
  uint32_t vocab;
  uint32_t d_model;
} EpaAtEmbedTable;

EpaAtRc epa_at_register_embed_table(uint32_t table_id,
                                    const float* data,
                                    uint32_t vocab,
                                    uint32_t d_model);

EpaAtRc epa_at_get_embed_table(uint32_t table_id, EpaAtEmbedTable* out);

// --------------------
// Batch task tracking (v0)
// --------------------
//
// A batch represents one AT invocation split into N sub-tasks ("blocks").
// Thread IDs are stable (pool identity). VTID is the sub-task index currently executed.
// Scheduler/thread-pool claims unfinished sub-tasks and sets VTID = sub_index for that run.

typedef enum EpaAtBlockState {
  EPA_AT_BLOCK_TODO = 0,
  EPA_AT_BLOCK_INFLIGHT = 1,
  EPA_AT_BLOCK_DONE = 2,
  EPA_AT_BLOCK_FAULT = 3,
} EpaAtBlockState;

typedef int (*EpaAtSubtaskFn)(void *b, uint32_t vtid, int32_t tid, void *user);

typedef struct EpaAtBatch {
  uint32_t batch_id;
  uint16_t at_id;

  uint32_t block_count;

  // fast completion test
  _Atomic uint32_t done_count;
  _Atomic uint32_t fault_count;

  // simple scan cursor (helps avoid always starting at 0)
  _Atomic uint32_t scan_cursor;

  // per-block state machine
  _Atomic uint8_t *state;         // length = block_count, values = EpaAtBlockState
  _Atomic int32_t *owner_tid;     // length = block_count, -1 if none

  EpaAtSubtaskFn exec_fn;  // set by the AT opcode entry point
  void *user;              // AT-defined per-batch context (dims, handles, base, etc.)
  void (*on_finish)(struct EpaAtBatch *b, void *user);
  void *finish_user;
} EpaAtBatch;

// Create/destroy a batch instance for an AT invocation
EpaAtRc epa_at_batch_create(uint16_t at_id,
                            uint32_t block_count,
                            EpaAtBatch **out_batch);

void epa_at_batch_destroy(EpaAtBatch *b);

// Claim a TODO block for a given physical thread id (tid).
// On success: returns EPA_AT_OK and writes *out_block_index.
// Scheduler should set VTID = out_block_index for that run.
EpaAtRc epa_at_batch_claim(EpaAtBatch *b,
                           int32_t tid,
                           uint32_t *out_block_index);

// Complete a block previously claimed by this tid.
// state becomes DONE or FAULT. DONE increments done_count.
// Returns EPA_AT_OK or an error (including ownership mismatch).
EpaAtRc epa_at_batch_complete(EpaAtBatch *b,
                              int32_t tid,
                              uint32_t block_index,
                              int ok);

// Query: batch finished when done_count == block_count
static inline int epa_at_batch_is_done(const EpaAtBatch *b) {
  return b && atomic_load(&b->done_count) == b->block_count;
}

static inline int epa_at_batch_all_finished(const EpaAtBatch *b) {
  return b &&
         (atomic_load(&b->done_count) + atomic_load(&b->fault_count)) == b->block_count;
}

static inline int epa_at_batch_has_fault(const EpaAtBatch *b) {
  return b && atomic_load(&b->fault_count) != 0;
}

int epa_at_batch_exec_block(struct EpaAtBatch *b, uint32_t vtid, int32_t tid);


#ifdef __cplusplus
}
#endif
