#include "epa_worker_state.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "epa_vm.h"

int epa_worker_init(EpaWorkerState *w, uint32_t block_id,
                    uint32_t body_start_pc, uint32_t body_end_pc,
                    uint32_t in_words, uint32_t out_words, uint32_t signal_mailbox_size,
                    char err[EPA_MAX_ERR]) {
  if (err) err[0] = 0;
  if (!w) return 0;

  memset(w, 0, sizeof(*w));

  w->id = block_id;

  // Reinterpreted for the new descriptor-based execution:
  // - body_start_pc: absolute blob base where this descriptor begins (debug only)
  // - body_end_pc:   descriptor length in bytes
  w->abs_base = body_start_pc;
  w->code_len = body_end_pc;

  // EIP starts at entry slot block_id, relative pc=0
  w->vm.eip.block_type = EPA_BLOCK_ENTRY;
  w->vm.eip.block_id   = (uint16_t)block_id;
  w->vm.eip.rel_pc     = 0;

  // VM reset (VM holds the real stack/locals now)
  epa_vm_init(&w->vm);

  w->vm.lbytes_cap = 64 * 1024;              // start simple: 64KB (tune later)
  w->vm.lbytes = malloc(w->vm.lbytes_cap);
  w->vm.lbytes_top = 0;

  // Scheduling policy: kernel runs, others sleep until ENTRY_EXEC
  w->blocked = (block_id == 0) ? 0 : 1;
  w->faulted = 0;
  w->halted  = 0;

  if (!epa_ring_init(&w->inq, in_words ? in_words : 1)) {
    snprintf(err, EPA_MAX_ERR, "worker %u: in ring init failed", (unsigned)block_id);
    return 0;
  }
  if (!epa_ring_init(&w->outq, out_words ? out_words : 1)) {
    epa_ring_free(&w->inq);
    snprintf(err, EPA_MAX_ERR, "worker %u: out ring init failed", (unsigned)block_id);
    return 0;
  }

  w->signal_mailbox = malloc(signal_mailbox_size);

  w->inited = 1;

  return 1;
}

void epa_worker_free(EpaWorkerState *w) {
  if (!w || !w->inited) return;
  epa_ring_free(&w->inq);
  epa_ring_free(&w->outq);
  epa_vm_free(&w->vm);

  free(w->vm.lbytes);
  w->vm.lbytes = NULL;
  w->vm.lbytes_cap = 0;
  w->vm.lbytes_top = 0;

  free(w->signal_mailbox);

  memset(w, 0, sizeof(*w));
}

void epa_worker_reset(EpaWorkerState *w) {
  if (!w || !w->inited) return;

  epa_vm_free(&w->vm);
  epa_vm_init(&w->vm);

  // Reset Common State Control registers (r0..r3)
  memset(w->vm.csc, 0, sizeof(w->vm.csc));

  // Reset EIP to start of its current descriptor
  w->vm.eip.rel_pc = 0;

  if (w->vm.lbytes)
	  free(w->vm.lbytes);

  w->vm.lbytes_cap = 64 * 1024;              // start simple: 64KB (tune later)
  w->vm.lbytes = malloc(w->vm.lbytes_cap);
  w->vm.lbytes_top = 0;

  epa_ring_clear(&w->inq);
  epa_ring_clear(&w->outq);

  w->faulted = 0;
  w->halted  = 0;

  // Keep blocked policy unchanged: kernel unblocked, others blocked
  w->blocked = (w->id == 0) ? 0 : 1;
}
