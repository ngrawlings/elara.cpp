#include "vm/epa_worker_state.h"

#include <stdio.h>
#include <stdint.h>

#include "epa_kernel.h"
#include "epa_kernel_so.h"
#include "epa_kernel_hooks.h"

// -------------------------------
// Scheduler context + flow hooks
// -------------------------------

void kdbg_emit(EpaKernel *k, EpaKernelDbgKind kind, uint8_t wid, uint32_t code, const EpaEip *at, const char *msg) {
  if (!k) return;
  if (k->dbg_cb) k->dbg_cb(k->dbg_user, kind, wid, code, at, msg);
}

int hook_entry_exec(void *user, uint8_t wid, char err[EPA_MAX_ERR]) {
  KernelImpl *k = (KernelImpl*)user;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_entry_exec: impl null"); return 0; }
  if (wid >= EPA_MAX_WORKERS || !k->workers[wid].inited) return 1; // ignore if missing

  k->workers[wid].inited = 1;
  k->workers[wid].halted = 0;
  k->workers[wid].blocked = 0;
  return 1;
}

int hook_entry_halt(void *user, uint8_t wid, char err[EPA_MAX_ERR]) {
  KernelImpl *k = (KernelImpl*)user;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_entry_halt: impl null"); return 0; }
  if (wid >= EPA_MAX_WORKERS || !k->workers[wid].inited) return 1;
  k->workers[wid].halted = 1;
  k->workers[wid].blocked = 1;
  return 1;
}

int hook_sync(void *user, char err[EPA_MAX_ERR]) {
  KernelImpl *k = (KernelImpl*)user;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_sync: impl null"); return 0; }

  // Barrier notification only: record which worker hit SYNC.
  if (!epa_ring_push(&k->syncq, k->cur_wid, 1 /*soft*/, err)) return 0;

  // Workers (non-kernel) park until kernel decides to re-activate them.
  if (k->cur_wid < EPA_MAX_WORKERS && k->workers[k->cur_wid].inited) {
    EpaWorkerState *w = &k->workers[k->cur_wid];
    if (w->id != 0) w->blocked = 1;
  }

  // Unblock kernel slot 0 immediately so it can service syncs.
  if (k->workers[0].inited) k->workers[0].blocked = 0;

  return 1;
}

int hook_wait_on_sync(void *user, char err[EPA_MAX_ERR]) {
  KernelImpl *k = (KernelImpl*)user;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_wait_on_sync: impl null"); return 0; }

  uint32_t wid = 0;
  if (!epa_ring_pop(&k->syncq, &wid)) {
    // Nothing ready => kernel blocks itself. Flow should yield without advancing EIP.
    if (k->cur_wid == 0 && k->workers[0].inited) k->workers[0].blocked = 1;
    return 1;
  }

  // Push wid onto kernel stack so kernel code can decide what to do next.
  EpaWorkerState *kw = &k->workers[0];
  if (kw->inited) {
    if (!epa_stack_push(&kw->vm.stack, wid)) {
      snprintf(err, EPA_MAX_ERR, "WAIT_ON_SYNC: kernel stack overflow");
      return 0;
    }
  }

  return 1;
}

EpaWorkerState* hook_get_worker(void *user, uint8_t wid) {
  KernelImpl *k = (KernelImpl*)user;
  if (!k) return NULL;
  if (wid >= EPA_MAX_WORKERS) return NULL;
  if (!k->workers[wid].inited) return NULL;
  return &k->workers[wid];
}

// Interrupt / debug hooks
int hook_break(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]) {
  EpaKernel *k = (EpaKernel*)user;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_break: impl null"); return 0; }
  if (wid < EPA_MAX_WORKERS && k->impl.workers[wid].inited) {
    k->impl.workers[wid].blocked = 1; // pause this worker until IDE/test harness resumes
  }
  fprintf(stderr, "[BREAK] wid=%u code=%u at=%u/%u pc=%u\n",
          (unsigned)wid, (unsigned)code,
          (unsigned)(at ? at->block_type : 0u), (unsigned)(at ? at->block_id : 0u),
          (unsigned)(at ? at->rel_pc : 0u));

  kdbg_emit(k, EPA_KDBG_BREAK, wid, code, at, NULL);
  return 1;
}

int hook_trap(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]) {
  EpaKernel *k = (EpaKernel*)user;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_trap: kernel null"); return 0; }

  if (wid < EPA_MAX_WORKERS && k->impl.workers[wid].inited) {
    k->impl.workers[wid].faulted = 1;
    k->impl.workers[wid].blocked = 1;
  }

  kdbg_emit(k, EPA_KDBG_TRAP, wid, code, at, NULL);
  return 1;
}

int hook_except(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]) {
  EpaKernel *k = (EpaKernel*)user;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_except: kernel null"); return 0; }

  // Decide your policy: often EXCEPT is fatal to the entry.
  if (wid < EPA_MAX_WORKERS && k->impl.workers[wid].inited) {
    k->impl.workers[wid].faulted = 1;
    k->impl.workers[wid].blocked = 1;
  }

  kdbg_emit(k, EPA_KDBG_EXCEPT, wid, code, at, NULL);
  return 1;
}

int hook_signal(void *user, uint8_t wid, char err[EPA_MAX_ERR]) {
	  EpaKernel *k = (EpaKernel*)user;
	  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_signal: kernel null"); return 0; }

	  // Decide your policy: often EXCEPT is fatal to the entry.
	  if (wid >= EPA_MAX_WORKERS || !k->impl.workers[wid].inited) {
	    return 0;
	  }

	  if (k->signal_cb) {
		  epa_kernel_request_interrupt(k);
		  if (!k->signal_cb(wid, (const char*)k->impl.workers[wid].signal_mailbox, k->prog.signal_mailbox_size[wid])) {
			  k->impl.workers[wid].blocked = 1;
		  }
	  }

	  return 1;
}
