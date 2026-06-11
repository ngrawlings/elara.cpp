#include "vm/epa_worker_state.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "epa_kernel.h"
#include "epa_kernel_internal.h"
#include "epa_kernel_so.h"
#include "epa_kernel_hooks.h"
#include "opcodes/opcode_def.h"

// -------------------------------
// Scheduler context + flow hooks
// -------------------------------

void kdbg_emit(EpaKernel *k, EpaKernelDbgKind kind, uint8_t wid, uint32_t code, const EpaEip *at, const char *msg) {
  if (!k) return;
  if (k->dbg_cb) k->dbg_cb(k->dbg_user, kind, wid, code, at, msg);
}

void epa_print_fault_location(EpaKernel *k, uint32_t wid, const EpaEip *eip, const char *detail) {
  const char *kernel = k && k->kernel_id ? k->kernel_id : "(unnamed)";
  const char *block  = (eip && eip->block_type == EPA_BLOCK_ENTRY) ? "entry" :
                       (eip && eip->block_type == EPA_BLOCK_AT_ENTRY) ? "at_entry" : "func";
  uint32_t    bid    = eip ? (uint32_t)eip->block_id  : 0u;
  uint32_t    pc     = eip ? (uint32_t)eip->rel_pc    : 0u;

  // Resolve opcode at the fault PC.
  const char *op_name = "?";
  if (k && eip) {
    const uint8_t *code = NULL;
    size_t code_len = 0;
    if (epa_prog_resolve(&k->prog, eip->block_type, eip->block_id, &code, &code_len)) {
      uint16_t op = 0;
      size_t op_width = 0;
      if (epa_decode_opcode_at(code, code_len, (size_t)pc, &op, &op_width)) {
        (void)op_width;
        const EpaOpcodeDef *def = epa_find_opcode(op);
        if (def) op_name = def->name;
      }
    }
  }

  fprintf(stderr,
    "[EPA-FAULT] kernel=%s wid=%u %s[%u] pc=%u op=%s  (%s.epaasm)\n"
    "            detail: %s\n",
    kernel, (unsigned)wid, block, (unsigned)bid, (unsigned)pc, op_name,
    kernel,
    detail ? detail : "(no detail)");
}

int hook_entry_exec(void *user, uint8_t wid, char err[EPA_MAX_ERR]) {
  EpaKernel *kernel = (EpaKernel*)user;
  KernelImpl *k = kernel ? &kernel->impl : NULL;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_entry_exec: impl null"); return 0; }
  if (wid >= EPA_MAX_WORKERS || !k->workers[wid].inited) return 1; // ignore if missing
  if (k->workers[wid].retired) return 1;

  k->workers[wid].inited = 1;
  k->workers[wid].halted = 0;
  k->workers[wid].blocked = 0;
  if (kernel->sched_vt && kernel->sched_vt->wake) {
    kernel->sched_vt->wake(kernel, &kernel->sched_state);
  }
  return 1;
}

int hook_entry_halt(void *user, uint8_t wid, char err[EPA_MAX_ERR]) {
  EpaKernel *kernel = (EpaKernel*)user;
  KernelImpl *k = kernel ? &kernel->impl : NULL;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_entry_halt: impl null"); return 0; }
  if (wid >= EPA_MAX_WORKERS || !k->workers[wid].inited) return 1;
  k->workers[wid].halted = 1;
  k->workers[wid].blocked = 1;
  return 1;
}

static void unlink_active_worker(KernelImpl *k, uint8_t wid) {
  uint32_t prev = EPA_MAX_WORKERS;
  uint32_t cur;
  if (!k) return;
  cur = k->worker_head;
  while (cur < EPA_MAX_WORKERS) {
    uint32_t next = k->worker_next[cur];
    if (cur == (uint32_t)wid) {
      if (prev >= EPA_MAX_WORKERS) {
        k->worker_head = next;
      } else {
        k->worker_next[prev] = next;
      }
      k->worker_next[cur] = EPA_MAX_WORKERS;
      if (k->n_workers > 0u) k->n_workers--;
      if (k->rr_cursor == cur) k->rr_cursor = next < EPA_MAX_WORKERS ? next : k->worker_head;
      return;
    }
    prev = cur;
    cur = next;
  }
}

int hook_entry_retire(void *user, uint8_t wid, char err[EPA_MAX_ERR]) {
  EpaKernel *kernel = (EpaKernel*)user;
  KernelImpl *k = kernel ? &kernel->impl : NULL;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_entry_retire: impl null"); return 0; }
  if (wid >= EPA_MAX_WORKERS || !k->workers[wid].inited) return 1;
  k->workers[wid].retired = 1;
  k->workers[wid].halted = 1;
  k->workers[wid].blocked = 1;
  unlink_active_worker(k, wid);
  return 1;
}

int hook_kernel_retire(void *user, uint64_t kernel_uid, char err[EPA_MAX_ERR]) {
  EpaKernel *kernel = (EpaKernel*)user;
  kernel_uid = epa_kernel_resolve_uid_for_sender(kernel, kernel_uid);
  return epa_kernel_retire_by_uid(kernel_uid, err);
}

int hook_entry_privilege(void *user, uint8_t wid, uint32_t privilege, char err[EPA_MAX_ERR]) {
  EpaKernel *kernel = (EpaKernel*)user;
  KernelImpl *k = kernel ? &kernel->impl : NULL;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_entry_privilege: impl null"); return 0; }
  if (k->privilege_locked) {
    snprintf(err, EPA_MAX_ERR, "ENTRY_PRIVILEGE rejected: privilege table locked");
    return 0;
  }
  if (wid >= EPA_MAX_WORKERS || !k->workers[wid].inited) {
    snprintf(err, EPA_MAX_ERR, "ENTRY_PRIVILEGE bad worker id %u", (unsigned)wid);
    return 0;
  }
  k->workers[wid].privilege = privilege;
  return 1;
}

int hook_privilege_lock(void *user, char err[EPA_MAX_ERR]) {
  EpaKernel *kernel = (EpaKernel*)user;
  KernelImpl *k = kernel ? &kernel->impl : NULL;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_privilege_lock: impl null"); return 0; }
  k->privilege_locked = 1u;
  return 1;
}

int hook_acl_grant(void *user, uint8_t wid, uint64_t target_kernel_uid, uint64_t remote_kernel_uid, uint8_t local_wid, char err[EPA_MAX_ERR]) {
  EpaKernel *kernel = (EpaKernel*)user;
  target_kernel_uid = epa_kernel_resolve_uid_for_sender(kernel, target_kernel_uid);
  remote_kernel_uid = epa_kernel_resolve_uid_for_sender(kernel, remote_kernel_uid);
  return epa_kernel_acl_grant_by_uid(kernel, wid, target_kernel_uid, remote_kernel_uid, local_wid, err);
}

int hook_acl_revoke(void *user, uint8_t wid, uint64_t target_kernel_uid, uint64_t remote_kernel_uid, uint8_t local_wid, char err[EPA_MAX_ERR]) {
  EpaKernel *kernel = (EpaKernel*)user;
  target_kernel_uid = epa_kernel_resolve_uid_for_sender(kernel, target_kernel_uid);
  remote_kernel_uid = epa_kernel_resolve_uid_for_sender(kernel, remote_kernel_uid);
  return epa_kernel_acl_revoke_by_uid(kernel, wid, target_kernel_uid, remote_kernel_uid, local_wid, err);
}

int hook_acl_revoke_all(void *user, uint8_t wid, uint64_t target_kernel_uid, uint64_t remote_kernel_uid, char err[EPA_MAX_ERR]) {
  EpaKernel *kernel = (EpaKernel*)user;
  target_kernel_uid = epa_kernel_resolve_uid_for_sender(kernel, target_kernel_uid);
  remote_kernel_uid = epa_kernel_resolve_uid_for_sender(kernel, remote_kernel_uid);
  return epa_kernel_acl_revoke_all_by_uid(kernel, wid, target_kernel_uid, remote_kernel_uid, err);
}

int hook_pid_self(void *user, uint8_t wid, uint32_t *out_pid, char err[EPA_MAX_ERR]) {
  EpaKernel *kernel = (EpaKernel*)user;
  (void)wid;
  if (!kernel || !out_pid) {
    if (err) snprintf(err, EPA_MAX_ERR, "PID kind=1: bad args");
    return 0;
  }
  *out_pid = epa_kernel_get_pid(kernel);
  return 1;
}

int hook_pid_retire(void *user, uint8_t wid, uint32_t pid, char err[EPA_MAX_ERR]) {
  EpaKernel *kernel = (EpaKernel*)user;
  return epa_kernel_pid_retire(kernel, wid, pid, err);
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
		  EpaWorkerState *w = &k->impl.workers[wid];
		  uint32_t mailbox_len = (uint32_t)w->vm.csc[3];
		  uint32_t mailbox_cap = k->prog.signal_mailbox_size[wid];
		  if (mailbox_len > mailbox_cap) mailbox_len = mailbox_cap;
		  epa_kernel_request_interrupt(k);
		  if (!k->signal_cb(wid, (const char*)w->signal_mailbox, (int)mailbox_len)) {
			  k->impl.workers[wid].blocked = 1;
		  }
	  }

	  return 1;
}

int hook_host_signal(void *user, uint8_t wid, char err[EPA_MAX_ERR]) {
  EpaKernel *k = (EpaKernel*)user;
  if (k && wid < EPA_MAX_WORKERS && k->impl.workers[wid].inited) {
    EpaWorkerState *w = &k->impl.workers[wid];
    uint32_t mailbox_cap = k->prog.signal_mailbox_size[wid];
    uint32_t mailbox_len = (uint32_t)w->vm.csc[3];
    if (mailbox_len > mailbox_cap) mailbox_len = mailbox_cap;
    if (!epa_kernel_store_last_host_signal(k, wid, w->signal_mailbox, mailbox_len, err)) {
      return 0;
    }
    if (mailbox_len > 0u) {
      char msg[256];
      size_t pos = 0;
      uint32_t preview_len = mailbox_len < 16u ? mailbox_len : 16u;
      int n = snprintf(msg, sizeof(msg), "mailbox_bytes=%u preview=", (unsigned)mailbox_len);
      if (n < 0) n = 0;
      if ((size_t)n >= sizeof(msg)) n = (int)sizeof(msg) - 1;
      pos = (size_t)n;
      for (uint32_t i = 0; i < preview_len && pos + 2u < sizeof(msg); i++) {
        n = snprintf(msg + pos, sizeof(msg) - pos, "%02X", (unsigned)w->signal_mailbox[i]);
        if (n < 0) break;
        if ((size_t)n >= sizeof(msg) - pos) {
          pos = sizeof(msg) - 1;
          break;
        }
        pos += (size_t)n;
      }
      if (mailbox_len > preview_len && pos + 4u < sizeof(msg)) {
        snprintf(msg + pos, sizeof(msg) - pos, "...");
      }
      kdbg_emit(k, EPA_KDBG_EGRESS, wid, mailbox_len, &w->vm.eip, msg);
    } else {
      kdbg_emit(k, EPA_KDBG_SIGNAL, wid, 0, &w->vm.eip, "host_signal");
    }
  }
  return hook_signal(user, wid, err);
}

int hook_request_threads(void *user, uint8_t wid, uint32_t desired_total, char err[EPA_MAX_ERR]) {
  EpaKernel *k = (EpaKernel*)user;
  uint32_t current;
  uint32_t add_count;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_request_threads: kernel null"); return 0; }
  if (wid != 0u) {
    snprintf(err, EPA_MAX_ERR, "hook_request_threads: only valid in kernel");
    return 0;
  }
  if (desired_total <= 1u) {
    return 1;
  }
  if (epa_kernel_get_scheduler(k) == EPA_SCHED_DEBUG) {
    return 1;
  }
  current = epa_kernel_thread_count(k);
  if (current >= desired_total) {
    return 1;
  }
  add_count = desired_total - current;
  return epa_kernel_add_threads(k, add_count, err);
}

int hook_request_at(void *user, uint8_t wid, const uint32_t *descriptor_words, uint32_t descriptor_word_count, uint32_t *out_request_id, char err[EPA_MAX_ERR]) {
  EpaKernel *k = (EpaKernel*)user;
  EpaWorkerState *w;
  EpaSystemAtRequestRecord *slot;
  uint32_t *owned_words;
  uint64_t request_id;
  uint32_t at_id;
  uint32_t at_index;
  uint32_t requested_threads;
  uint32_t real_threads;
  uint32_t param_words;
  uint16_t frame_words;

  if (err) err[0] = 0;
  if (out_request_id) *out_request_id = 0u;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_request_at: kernel null"); return 0; }
  if (wid >= EPA_MAX_WORKERS || !k->impl.workers[wid].inited) {
    snprintf(err, EPA_MAX_ERR, "hook_request_at: only valid in initialized entries/workers");
    return 0;
  }
  if (!descriptor_words || descriptor_word_count < 6u) {
    snprintf(err, EPA_MAX_ERR, "hook_request_at: descriptor too small");
    return 0;
  }
  if (descriptor_word_count > 1024u) {
    snprintf(err, EPA_MAX_ERR, "hook_request_at: descriptor too large");
    return 0;
  }

  at_id = descriptor_words[0];
  requested_threads = descriptor_words[2];
  real_threads = descriptor_words[3];
  param_words = descriptor_words[4];
  at_index = 0u;
  frame_words = 0u;
  if (!epa_prog_find_at_entry(&k->prog, at_id, &at_index, &frame_words)) {
    snprintf(err, EPA_MAX_ERR, "hook_request_at: unknown AT entry at_id=%u", (unsigned)at_id);
    return 0;
  }
  if (requested_threads == 0u) {
    snprintf(err, EPA_MAX_ERR, "hook_request_at: requested_threads=0");
    return 0;
  }
  if (real_threads == 0u) {
    snprintf(err, EPA_MAX_ERR, "hook_request_at: real_threads=0");
    return 0;
  }
  if ((uint64_t)6u + (uint64_t)param_words > (uint64_t)descriptor_word_count) {
    snprintf(err, EPA_MAX_ERR, "hook_request_at: param_words exceeds descriptor");
    return 0;
  }
  if (param_words < 2u) {
    snprintf(err, EPA_MAX_ERR, "hook_request_at: descriptor param_words=%u below shared GHS handle words",
             (unsigned)param_words);
    return 0;
  }
  (void)frame_words;

  owned_words = (uint32_t*)malloc((size_t)descriptor_word_count * sizeof(uint32_t));
  if (!owned_words) {
    snprintf(err, EPA_MAX_ERR, "hook_request_at: OOM copying descriptor");
    return 0;
  }
  memcpy(owned_words, descriptor_words, (size_t)descriptor_word_count * sizeof(uint32_t));

  pthread_mutex_lock(&k->impl.atq_mu);
  if (k->impl.atq.count >= EPA_SYSTEM_AT_QMAX) {
    pthread_mutex_unlock(&k->impl.atq_mu);
    free(owned_words);
    if (err) err[0] = 0;
    return 2;
  }

  request_id = k->impl.atq.next_request_id++;
  if (k->impl.atq.next_request_id == 0u) k->impl.atq.next_request_id = 1u;
  slot = &k->impl.atq.q[k->impl.atq.tail];
  free(slot->descriptor_words);
  slot->request_id = request_id;
  slot->wid = wid;
  slot->at_entry_index = at_index;
  slot->descriptor_word_count = descriptor_word_count;
  slot->descriptor_words = owned_words;
  k->impl.atq.tail = (k->impl.atq.tail + 1u) % EPA_SYSTEM_AT_QMAX;
  k->impl.atq.count++;
  pthread_mutex_unlock(&k->impl.atq_mu);

  w = &k->impl.workers[wid];
  if (out_request_id) *out_request_id = (uint32_t)(request_id & 0xffffffffu);
  kdbg_emit(k, EPA_KDBG_SIGNAL, wid, (uint32_t)(request_id & 0xffffffffu), &w->vm.eip, "request_at");
  return epa_kernel_dispatch_at_requests_cpu(k, err);
}

int epa_kernel_dispatch_memory_requests_cpu(EpaKernel *k, char err[EPA_MAX_ERR]) {
  EpaSystemMemoryRequestRecord req;
  EpaWorkerState *w;
  EpaDynamicPool *pool;

  if (err) err[0] = 0;
  if (!k) {
    if (err) snprintf(err, EPA_MAX_ERR, "memory_dispatch_cpu: kernel null");
    return 0;
  }

  for (;;) {
    memset(&req, 0, sizeof(req));
    pthread_mutex_lock(&k->impl.memq_mu);
    if (k->impl.memq.count == 0u) {
      pthread_mutex_unlock(&k->impl.memq_mu);
      return 1;
    }
    req = k->impl.memq.q[k->impl.memq.head];
    k->impl.memq.q[k->impl.memq.head].request_id = 0u;
    k->impl.memq.head = (k->impl.memq.head + 1u) % EPA_SYSTEM_MEM_QMAX;
    k->impl.memq.count--;
    pthread_mutex_unlock(&k->impl.memq_mu);

    if (req.kind != EPA_SYSTEM_MEM_REQ_DYNAMIC_POOL_CAPACITY) {
      if (err) snprintf(err, EPA_MAX_ERR, "memory_dispatch_cpu: unknown request kind=%u", (unsigned)req.kind);
      return 0;
    }

    if (req.wid >= EPA_MAX_WORKERS || !k->impl.workers[req.wid].inited) {
      if (err) snprintf(err, EPA_MAX_ERR, "dynamic pool capacity request bad wid=%u", (unsigned)req.wid);
      return 0;
    }
    w = &k->impl.workers[req.wid];
    if (req.pool_id >= w->dynamic_pool_count || !w->dynamic_pools) {
      if (err) snprintf(err, EPA_MAX_ERR, "dynamic pool capacity request bad pool_id=%u", (unsigned)req.pool_id);
      return 0;
    }
    pool = &w->dynamic_pools[req.pool_id];

    /*
      Dynamic allocation remains invisible to E. Growth requests are hard
      orders because forward progress depends on them. Shrink requests are
      suggestions: the host may keep the capacity if that is cheaper.
    */
    if (!epa_dynamic_pool_request_capacity(pool, req.requested_capacity, req.hard_order ? 1 : 0, err)) {
      if (err && !err[0]) {
        snprintf(err, EPA_MAX_ERR, "dynamic pool capacity request failed pool_id=%u requested=%u",
                 (unsigned)req.pool_id, (unsigned)req.requested_capacity);
      }
      return 0;
    }
  }
}

int hook_request_dynamic_pool_capacity(void *user, uint8_t wid, uint32_t pool_id, uint32_t requested_capacity, int hard_order, char err[EPA_MAX_ERR]) {
  EpaKernel *k = (EpaKernel*)user;
  EpaSystemMemoryRequestRecord *slot;
  EpaWorkerState *w;
  uint64_t request_id;

  if (err) err[0] = 0;
  if (!k) {
    snprintf(err, EPA_MAX_ERR, "hook_request_dynamic_pool_capacity: kernel null");
    return 0;
  }
  if (wid >= EPA_MAX_WORKERS || !k->impl.workers[wid].inited) {
    snprintf(err, EPA_MAX_ERR, "hook_request_dynamic_pool_capacity: bad wid %u", (unsigned)wid);
    return 0;
  }
  w = &k->impl.workers[wid];
  if (pool_id >= w->dynamic_pool_count || !w->dynamic_pools) {
    snprintf(err, EPA_MAX_ERR, "hook_request_dynamic_pool_capacity: bad pool_id %u", (unsigned)pool_id);
    return 0;
  }
  if (hard_order && requested_capacity < w->dynamic_pools[pool_id].count) {
    snprintf(err, EPA_MAX_ERR, "hook_request_dynamic_pool_capacity: hard request below live count");
    return 0;
  }

  pthread_mutex_lock(&k->impl.memq_mu);
  if (k->impl.memq.count >= EPA_SYSTEM_MEM_QMAX) {
    pthread_mutex_unlock(&k->impl.memq_mu);
    if (err) err[0] = 0;
    return 2;
  }

  request_id = k->impl.memq.next_request_id++;
  if (k->impl.memq.next_request_id == 0u) k->impl.memq.next_request_id = 1u;
  slot = &k->impl.memq.q[k->impl.memq.tail];
  memset(slot, 0, sizeof(*slot));
  slot->request_id = request_id;
  slot->wid = wid;
  slot->kind = EPA_SYSTEM_MEM_REQ_DYNAMIC_POOL_CAPACITY;
  slot->pool_id = pool_id;
  slot->requested_capacity = requested_capacity;
  slot->hard_order = hard_order ? 1u : 0u;
  k->impl.memq.tail = (k->impl.memq.tail + 1u) % EPA_SYSTEM_MEM_QMAX;
  k->impl.memq.count++;
  pthread_mutex_unlock(&k->impl.memq_mu);

  kdbg_emit(k, EPA_KDBG_SIGNAL, wid, (uint32_t)(request_id & 0xffffffffu), &w->vm.eip, "request_dynamic_pool_capacity");
  return epa_kernel_dispatch_memory_requests_cpu(k, err);
}

int hook_dynlib_import(void *user, uint8_t wid, uint64_t ghs_handle, uint32_t byte_count,
                       uint64_t local_name_uid, uint32_t *out_module_count,
                       char err[EPA_MAX_ERR]) {
  EpaKernel *k = (EpaKernel*)user;
  return epa_kernel_process_import_dynamic_library_ghs(k, wid, ghs_handle, byte_count,
                                                       local_name_uid, out_module_count, err);
}

int hook_far_signal(void *user, uint8_t wid, char err[EPA_MAX_ERR]) {
  EpaKernel *k = (EpaKernel*)user;
  EpaWorkerState *w;
  uint32_t target_uid_lo;
  uint32_t target_uid_hi;
  uint32_t payload_off;
  uint32_t payload_size;
  uint32_t payload_tag;
  uint32_t target_wid;
  uint64_t target_uid;
  if (!k) { snprintf(err, EPA_MAX_ERR, "hook_far_signal: kernel null"); return 0; }
  if (wid >= EPA_MAX_WORKERS || !k->impl.workers[wid].inited) {
    snprintf(err, EPA_MAX_ERR, "hook_far_signal: bad wid %u", (unsigned)wid);
    return 0;
  }

  w = &k->impl.workers[wid];
  target_uid_lo = (uint32_t)w->vm.csc[0];
  target_uid_hi = (uint32_t)w->vm.csc[1];
  payload_off = (uint32_t)w->vm.csc[3];

  if (!epa_stack_pop(&w->vm.stack, &target_wid) ||
      !epa_stack_pop(&w->vm.stack, &payload_size) ||
      !epa_stack_pop(&w->vm.stack, &payload_tag)) {
    snprintf(err, EPA_MAX_ERR, "hook_far_signal: expected target_wid/payload size/tag on stack");
    return 0;
  }

  target_uid = ((uint64_t)target_uid_hi << 32) | (uint64_t)target_uid_lo;
  target_uid = epa_kernel_resolve_uid_for_sender(k, target_uid);
  if (target_uid == 0u) {
    snprintf(err, EPA_MAX_ERR, "hook_far_signal: empty target kernel uid");
    return 0;
  }
  if (!w->vm.lbytes || payload_off + payload_size > w->vm.lbytes_top) {
    snprintf(err, EPA_MAX_ERR, "hook_far_signal: payload local block out of bounds");
    return 0;
  }

  if (!epa_kernel_far_signal_by_uid(k, wid, target_uid, target_wid, w->vm.lbytes + payload_off, payload_size, payload_tag, err)) {
    return 0;
  }
  return 1;
}
