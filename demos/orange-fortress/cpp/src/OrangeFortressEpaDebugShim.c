#include "OrangeFortressEpaDebugShim.h"

#include <string.h>
#include <libelaraparallelassembly/epa_kernel.h>
#include <libelaraparallelassembly/vm/epa_worker_state.h>
#include <libelaraparallelassembly/memory/epa_stack.h>
#include <libelaraparallelassembly/memory/epa_ring_buffer.h>

int OrangeFortress_epa_debug_capture_kernel(EpaKernel *kernel, OrangeFortressEpaDebugKernelSnapshot *out_snapshot) {
  uint32_t wid;
  if (!kernel || !out_snapshot) return 0;
  memset(out_snapshot, 0, sizeof(*out_snapshot));
  out_snapshot->prog_loaded = (uint32_t)kernel->prog_loaded;
  out_snapshot->rr_cursor = kernel->impl.rr_cursor;
  out_snapshot->current_wid = kernel->impl.cur_wid;
  out_snapshot->interrupt_requested = (uint32_t)kernel->impl.interrupt_requested;
  for (wid = 0; wid < EPA_MAX_WORKERS; wid++) {
    if (kernel->impl.workers[wid].inited) out_snapshot->worker_count++;
  }
  return 1;
}

size_t OrangeFortress_epa_debug_capture_workers(EpaKernel *kernel, OrangeFortressEpaDebugWorkerSnapshot *out_workers, size_t max_workers) {
  size_t count = 0;
  uint32_t wid;
  if (!kernel || !out_workers || !max_workers) return 0;
  memset(out_workers, 0, sizeof(*out_workers) * max_workers);
  for (wid = 0; wid < EPA_MAX_WORKERS && count < max_workers; wid++) {
    EpaWorkerState *w = &kernel->impl.workers[wid];
    OrangeFortressEpaDebugWorkerSnapshot *dst;
    size_t i;
    if (!w->inited) continue;
    dst = &out_workers[count++];
    memset(dst, 0, sizeof(*dst));
    dst->wid = wid;
    dst->active = 1u;
    dst->inited = (uint32_t)w->inited;
    dst->halted = (uint32_t)w->halted;
    dst->blocked = (uint32_t)w->blocked;
    dst->faulted = (uint32_t)w->faulted;
    dst->waiting_for_data = (uint32_t)w->waiting_for_data;
    dst->at_running = (uint32_t)w->at_running;
    dst->has_current_ghs = (uint32_t)w->has_current_ghs;
    memcpy(dst->csc, w->vm.csc, sizeof(dst->csc));
    dst->stack_depth = (uint32_t)w->vm.stack.sp;
    dst->stack_preview_count = (uint32_t)((w->vm.stack.sp < ORANGEFORTRESS_EPA_DEBUG_STACK_PREVIEW) ? w->vm.stack.sp : ORANGEFORTRESS_EPA_DEBUG_STACK_PREVIEW);
    for (i = 0; i < dst->stack_preview_count; i++) {
      size_t src_index = w->vm.stack.sp - dst->stack_preview_count + i;
      dst->stack_preview[i] = w->vm.stack.words ? w->vm.stack.words[src_index] : 0u;
    }
    dst->inq_count = epa_ring_count(&w->inq);
    dst->outq_count = epa_ring_count(&w->outq);
    memcpy(dst->locals, w->vm.locals, sizeof(dst->locals));
    dst->lbytes_top = w->vm.lbytes_top;
    dst->lbytes_cap = w->vm.lbytes_cap;
    dst->lscope_depth = w->vm.lscope_depth;
    dst->current_ghs = (uint64_t)w->current_ghs;
    dst->eip.block_type = w->vm.eip.block_type;
    dst->eip.block_id = w->vm.eip.block_id;
    dst->eip.rel_pc = w->vm.eip.rel_pc;
  }
  return count;
}

int OrangeFortress_epa_debug_any_worker_at(EpaKernel *kernel, uint8_t block_type, uint16_t block_id, uint32_t rel_pc, uint32_t *out_wid) {
  uint32_t wid;
  if (!kernel) return 0;
  for (wid = 0; wid < EPA_MAX_WORKERS; wid++) {
    EpaWorkerState *w = &kernel->impl.workers[wid];
    if (!w->inited || w->halted || w->faulted) continue;
    if (w->vm.eip.block_type == block_type && w->vm.eip.block_id == block_id && w->vm.eip.rel_pc == rel_pc) {
      if (out_wid) *out_wid = wid;
      return 1;
    }
  }
  return 0;
}
