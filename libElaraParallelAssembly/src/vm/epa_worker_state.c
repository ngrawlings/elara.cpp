#include "epa_worker_state.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "epa_vm.h"

static void worker_free_dynamic_pools(EpaWorkerState *w) {
  uint32_t i;
  if (!w || !w->dynamic_pools) return;
  for (i = 0; i < w->dynamic_pool_count; i++) {
    epa_dynamic_pool_free(&w->dynamic_pools[i]);
  }
  free(w->dynamic_pools);
  w->dynamic_pools = NULL;
  w->dynamic_pool_count = 0u;
}

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

  // Current E worker model: all entries start immediately and workers
  // block themselves explicitly when they reach WAIT_FOR_DATA.
  w->blocked = 0;
  w->faulted = 0;
  w->halted  = 0;
  w->waiting_for_data = 0;
  w->at_running = 0;
  w->has_current_ghs = 0;
  w->current_ghs = 0;

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
  worker_free_dynamic_pools(w);

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
  w->waiting_for_data = 0;
  w->at_running = 0;
  w->has_current_ghs = 0;
  w->current_ghs = 0;

  // Preserve current E worker model on reset as well.
  w->blocked = 0;
}

int epa_worker_configure_dynamic_pools(EpaWorkerState *w,
                                       const EpaDynamicPoolConfig *configs,
                                       uint32_t config_count,
                                       char err[EPA_MAX_ERR]) {
  uint32_t i;
  EpaDynamicPool *pools = NULL;
  if (err) err[0] = 0;
  if (!w || !w->inited) {
    snprintf(err, EPA_MAX_ERR, "worker dynamic config: worker not initialized");
    return 0;
  }

  worker_free_dynamic_pools(w);
  if (config_count == 0u) return 1;
  if (!configs) {
    snprintf(err, EPA_MAX_ERR, "worker dynamic config: null configs");
    return 0;
  }

  pools = (EpaDynamicPool*)calloc(config_count, sizeof(EpaDynamicPool));
  if (!pools) {
    snprintf(err, EPA_MAX_ERR, "worker dynamic config: OOM");
    return 0;
  }

  for (i = 0; i < config_count; i++) {
    if (!epa_dynamic_pool_init(&pools[i],
                               configs[i].min_free,
                               configs[i].max_free,
                               configs[i].grow_by,
                               configs[i].element_size,
                               err)) {
      uint32_t j;
      for (j = 0; j < i; j++) epa_dynamic_pool_free(&pools[j]);
      free(pools);
      return 0;
    }
  }

  w->dynamic_pools = pools;
  w->dynamic_pool_count = config_count;
  return 1;
}

int epa_worker_round_enter(EpaWorkerState *w, char err[EPA_MAX_ERR]) {
  uint32_t i;
  if (err) err[0] = 0;
  if (!w || !w->inited) {
    snprintf(err, EPA_MAX_ERR, "worker round enter: worker not initialized");
    return 0;
  }
  for (i = 0; i < w->dynamic_pool_count; i++) {
    if (!epa_dynamic_pool_round_enter(&w->dynamic_pools[i], err)) {
      if (err && err[0]) {
        char local[EPA_MAX_ERR];
        snprintf(local, sizeof(local), "worker %u dynamic pool %u: %s",
                 (unsigned)w->id, (unsigned)i, err);
        strncpy(err, local, EPA_MAX_ERR - 1u);
        err[EPA_MAX_ERR - 1u] = 0;
      }
      return 0;
    }
  }
  return 1;
}
