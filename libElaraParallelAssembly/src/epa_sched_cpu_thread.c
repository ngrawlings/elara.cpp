// src/epa_sched_cpu_thread.c
#include "epa_kernel.h"
#include "epa_kernel_internal.h"
#include "epa_kernel_hooks.h"
#include "platform/epa_platform_wait.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

// ------------------------------------------------------------
// CPU thread scheduler profile (first draft)
//
// Semantics:
//  - One OS thread is bound to at most one worker at a time.
//  - When a worker becomes runnable (ENTRY_EXEC), it may acquire a pool thread.
//  - When a worker blocks/waits, its bound thread is returned to the pool.
//  - When the worker wakes again, it can acquire a (possibly different) thread.
//
// IMPORTANT: This is intentionally a minimal first draft.
//  - It preserves the existing single-threaded worker semantics.
//  - It keeps kernel/worker interaction unidirectional via ring buffers.
//  - It does not attempt to implement CUDA hot-idle; only CPU sleep.
// ------------------------------------------------------------

typedef struct CpuThreadState CpuThreadState;

typedef struct {
  EpaKernel *k;
  CpuThreadState *st;
  int32_t tid;
} CpuThreadArg;

typedef struct CpuThreadState {
  pthread_mutex_t mu;
  pthread_cond_t  cv;

  uint32_t n_threads;
  uint32_t cap_threads;
  pthread_t *threads;

  // tid->wid binding; -1 means idle thread
  int32_t *tid_to_wid;

  // wid->tid binding; -1 means no thread currently assigned
  int32_t wid_to_tid[EPA_MAX_WORKERS];

  // stop all worker threads
  int stop;

  // interrupt barrier:
  // - when set, worker threads finish at most one tick and then park (unbind)
  // - scheduler returns 2 once all threads are idle
  _Atomic int interrupt;

} CpuThreadState;

static void* cpu_worker_thread_main(void *argp);

static int cpu_spawn_thread(EpaKernel *k, CpuThreadState *st, uint32_t tid, char err[EPA_MAX_ERR]) {
  CpuThreadArg *a = (CpuThreadArg*)calloc(1, sizeof(CpuThreadArg));
  if (!a) {
    if (err) snprintf(err, EPA_MAX_ERR, "cpu_thread: OOM (arg)");
    return 0;
  }

  a->k = k;
  a->st = st;
  a->tid = (int32_t)tid;

  if (pthread_create(&st->threads[tid], NULL, cpu_worker_thread_main, a) != 0) {
    free(a);
    if (err) snprintf(err, EPA_MAX_ERR, "cpu_thread: pthread_create failed");
    return 0;
  }
  return 1;
}

static inline int worker_runnable(const EpaWorkerState *w) {
  return w && w->inited && !w->retired && !w->halted && !w->faulted && !w->blocked && !w->waiting_for_data;
}

static int any_ignore_max_ticks_runnable(const EpaKernel *k) {
  if (!k) return 0;
  for (uint32_t wid = 0; wid < EPA_MAX_WORKERS; wid++) {
    const EpaWorkerState *w = &k->impl.workers[wid];
    if (worker_runnable(w) && w->ignore_max_ticks) return 1;
  }
  return 0;
}

static int debug_player_avatar_worker(const EpaKernel *k, uint32_t wid) {
  (void)k;
  (void)wid;
  return 0;
}

static void unbind_locked(EpaKernel *k, CpuThreadState *st, int32_t tid) {
  int32_t wid = st->tid_to_wid[tid];
  if (wid >= 0 && wid < (int32_t)EPA_MAX_WORKERS) {
    st->wid_to_tid[wid] = -1;
  }
  st->tid_to_wid[tid] = -1;
}

// Execute exactly one worker tick (same semantics as wave scheduler).
// Returns:
//   0 = error/faulted
//   1 = ok (continue)
//   2 = kernel ended (worker 0 halted)
static int exec_one_tick(EpaKernel *k, uint32_t wid, char err[EPA_MAX_ERR]) {
  EpaWorkerState *w = &k->impl.workers[wid];

  if (debug_player_avatar_worker(k, wid)) {
    fprintf(stderr,
            "[EPA-CPU-TICK] kernel=%s wid=%u begin blocked=%u waiting=%u inq=%u outq=%u pc=%u type=%u id=%u\n",
            k->kernel_id ? k->kernel_id : "(unnamed)",
            (unsigned)wid,
            (unsigned)w->blocked,
            (unsigned)w->waiting_for_data,
            (unsigned)epa_ring_count(&w->inq),
            (unsigned)epa_ring_count(&w->outq),
            (unsigned)w->vm.eip.rel_pc,
            (unsigned)w->vm.eip.block_type,
            (unsigned)w->vm.eip.block_id);
  }

  k->impl.cur_wid = wid;

  EpaFlowRc frc = epa_flow_step(
      k,
      &k->flow,
      w,
      (EpaStack*)&w->vm.stack,
      err
  );

  if (k->boot_reset_pending) {
    if (!epa_kernel_commit_pending_boot_reset(k, err)) {
      return 0;
    }
    return 2;
  }

  if (frc == EPA_FLOW_ERR) {
    w->faulted = 1;
    if (err && err[0]) {
      strncpy(w->fault_message, err, sizeof(w->fault_message) - 1u);
      w->fault_message[sizeof(w->fault_message) - 1u] = 0;
    }
    epa_print_fault_location(k, wid, &w->vm.eip, err && err[0] ? err : "(no message)");
    kdbg_emit(k, EPA_KDBG_EXCEPT, (uint8_t)wid, 0xFFFF0002u, &w->vm.eip, err);
    return 0;
  }

  if (frc == EPA_FLOW_OK) {
    w->halted = 1;
    return 1;
  }

  if (frc == EPA_FLOW_NOT_FLOW) {
    EpaNonFlowRc nrc = k->nf.vt->exec_one(k->nf.impl, &k->prog, w, &w->vm.eip, err);

    if (nrc == EPA_NF_EXEC_ERR) {
      w->faulted = 1;
      if (err && err[0]) {
        strncpy(w->fault_message, err, sizeof(w->fault_message) - 1u);
        w->fault_message[sizeof(w->fault_message) - 1u] = 0;
      }
      epa_print_fault_location(k, wid, &w->vm.eip, err && err[0] ? err : "(no message)");
      kdbg_emit(k, EPA_KDBG_EXCEPT, (uint8_t)wid, 0xFFFF0003u, &w->vm.eip, err);
      return 0;
    }

    if (nrc == EPA_NF_EXEC_HALT) {
      w->halted = 1;
      return 1;
    }
  }

  if (debug_player_avatar_worker(k, wid)) {
    fprintf(stderr,
            "[EPA-CPU-TICK] kernel=%s wid=%u end blocked=%u waiting=%u inq=%u outq=%u pc=%u type=%u id=%u frc=%d\n",
            k->kernel_id ? k->kernel_id : "(unnamed)",
            (unsigned)wid,
            (unsigned)w->blocked,
            (unsigned)w->waiting_for_data,
            (unsigned)epa_ring_count(&w->inq),
            (unsigned)epa_ring_count(&w->outq),
            (unsigned)w->vm.eip.rel_pc,
            (unsigned)w->vm.eip.block_type,
            (unsigned)w->vm.eip.block_id,
            (int)frc);
  }

  // Yielded or ran one step.
  return 1;
}

static void* cpu_worker_thread_main(void *argp) {
  CpuThreadArg *a = (CpuThreadArg*)argp;
  EpaKernel *k = a->k;
  CpuThreadState *st = a->st;
  int32_t tid = a->tid;
  free(a);

  char err[EPA_MAX_ERR];
  err[0] = 0;

  for (;;) {
    // Wait until assigned a worker, interrupted, or stopping.
    pthread_mutex_lock(&st->mu);

    while (!st->stop &&
           atomic_load(&st->interrupt) == 0 &&
           st->tid_to_wid[tid] < 0) {
      epa_platform_cond_wait(&st->cv, &st->mu);
    }

    if (st->stop) {
      pthread_mutex_unlock(&st->mu);
      break;
    }

    // If interrupted, park (ensure unbound) and keep waiting.
    if (atomic_load(&st->interrupt) != 0) {
      if (st->tid_to_wid[tid] >= 0) {
        unbind_locked(k, st, tid);
        epa_platform_cond_broadcast(&st->cv);
      }
      pthread_mutex_unlock(&st->mu);
      continue;
    }

    int32_t wid = st->tid_to_wid[tid];
    pthread_mutex_unlock(&st->mu);

    if (wid < 0 || wid >= (int32_t)EPA_MAX_WORKERS) {
      continue;
    }

    // Execute ticks for this worker until it blocks/halts/faults or interrupt requested.
    for (;;) {
      if (atomic_load(&st->interrupt) != 0 || k->impl.interrupt_requested) {
        // Stop at safe boundary (after a tick).
        pthread_mutex_lock(&st->mu);
        if (st->tid_to_wid[tid] == wid) {
          if (debug_player_avatar_worker(k, (uint32_t)wid)) {
            EpaWorkerState *w = &k->impl.workers[wid];
            fprintf(stderr,
                    "[EPA-CPU-UNBIND] kernel=%s tid=%d wid=%d reason=interrupt blocked=%u waiting=%u inq=%u outq=%u pc=%u\n",
                    k->kernel_id ? k->kernel_id : "(unnamed)",
                    (int)tid,
                    (int)wid,
                    (unsigned)w->blocked,
                    (unsigned)w->waiting_for_data,
                    (unsigned)epa_ring_count(&w->inq),
                    (unsigned)epa_ring_count(&w->outq),
                    (unsigned)w->vm.eip.rel_pc);
          }
          unbind_locked(k, st, tid);
          epa_platform_cond_broadcast(&st->cv);
        }
        pthread_mutex_unlock(&st->mu);
        break;
      }

      EpaWorkerState *w = &k->impl.workers[wid];
      if (!worker_runnable(w)) {
        // worker entered wait/halt/fault; return thread to pool
        pthread_mutex_lock(&st->mu);
        if (st->tid_to_wid[tid] == wid) {
          if (debug_player_avatar_worker(k, (uint32_t)wid)) {
            fprintf(stderr,
                    "[EPA-CPU-UNBIND] kernel=%s tid=%d wid=%d reason=not_runnable blocked=%u waiting=%u halted=%u faulted=%u inq=%u outq=%u pc=%u\n",
                    k->kernel_id ? k->kernel_id : "(unnamed)",
                    (int)tid,
                    (int)wid,
                    (unsigned)w->blocked,
                    (unsigned)w->waiting_for_data,
                    (unsigned)w->halted,
                    (unsigned)w->faulted,
                    (unsigned)epa_ring_count(&w->inq),
                    (unsigned)epa_ring_count(&w->outq),
                    (unsigned)w->vm.eip.rel_pc);
          }
          unbind_locked(k, st, tid);
          epa_platform_cond_broadcast(&st->cv);
        }
        pthread_mutex_unlock(&st->mu);
        break;
      }

      int rc = exec_one_tick(k, (uint32_t)wid, err);
      if (rc == 0) {
        // fault; unbind and park
        pthread_mutex_lock(&st->mu);
        if (st->tid_to_wid[tid] == wid) {
          unbind_locked(k, st, tid);
          epa_platform_cond_broadcast(&st->cv);
        }
        pthread_mutex_unlock(&st->mu);
        break;
      }

      if (rc == 2) {
        // kernel ended (worker 0 halted)
        pthread_mutex_lock(&st->mu);
        st->stop = 1;
        epa_platform_cond_broadcast(&st->cv);
        pthread_mutex_unlock(&st->mu);
        break;
      }

      // Continue; worker may block on next iteration.
    }
  }

  return NULL;
}

static int cpu_init(EpaKernel *k, EpaSchedState *s, char err[EPA_MAX_ERR]) {
  if (err) err[0] = 0;
  if (!k || !s) {
    if (err) snprintf(err, EPA_MAX_ERR, "cpu_thread: null kernel/state");
    return 0;
  }

  CpuThreadState *st = (CpuThreadState*)calloc(1, sizeof(CpuThreadState));
  if (!st) {
    if (err) snprintf(err, EPA_MAX_ERR, "cpu_thread: OOM");
    return 0;
  }

  st->n_threads = 1u;
  st->cap_threads = EPA_MAX_WORKERS;

  st->threads = (pthread_t*)calloc(st->cap_threads, sizeof(pthread_t));
  st->tid_to_wid = (int32_t*)malloc(sizeof(int32_t) * st->cap_threads);
  if (!st->threads || !st->tid_to_wid) {
    if (err) snprintf(err, EPA_MAX_ERR, "cpu_thread: OOM (threads/map)");
    free(st->threads);
    free(st->tid_to_wid);
    free(st);
    return 0;
  }

  for (uint32_t i = 0; i < st->cap_threads; i++) st->tid_to_wid[i] = -1;
  for (uint32_t i = 0; i < EPA_MAX_WORKERS; i++) st->wid_to_tid[i] = -1;

  pthread_mutex_init(&st->mu, NULL);
  pthread_cond_init(&st->cv, NULL);

  atomic_store(&st->interrupt, 0);
  st->stop = 0;

  for (uint32_t i = 0; i < st->n_threads; i++) {
    if (!cpu_spawn_thread(k, st, i, err)) {
      if (err) snprintf(err, EPA_MAX_ERR, "cpu_thread: pthread_create failed");
      st->stop = 1;
      epa_platform_cond_broadcast(&st->cv);
      for (uint32_t j = 0; j < i; j++) pthread_join(st->threads[j], NULL);
      pthread_cond_destroy(&st->cv);
      pthread_mutex_destroy(&st->mu);
      free(st->threads);
      free(st->tid_to_wid);
      free(st);
      return 0;
    }
  }

  s->opaque = st;
  return 1;
}

int epa_sched_cpu_thread_add_threads(EpaKernel *k,
                                     EpaSchedState *s,
                                     uint32_t add_count,
                                     char err[EPA_MAX_ERR]) {
  CpuThreadState *st;
  uint32_t start_tid;
  uint32_t tid;

  if (err) err[0] = 0;
  if (!k || !s || !s->opaque) {
    if (err) snprintf(err, EPA_MAX_ERR, "cpu_thread: scheduler not initialised");
    return 0;
  }
  if (add_count == 0u) return 1;

  st = (CpuThreadState*)s->opaque;
  pthread_mutex_lock(&st->mu);
  if (st->n_threads + add_count > st->cap_threads) {
    pthread_mutex_unlock(&st->mu);
    if (err) snprintf(err, EPA_MAX_ERR, "cpu_thread: thread budget exceeds max %u", (unsigned)st->cap_threads);
    return 0;
  }
  start_tid = st->n_threads;
  st->n_threads += add_count;
  pthread_mutex_unlock(&st->mu);

  for (tid = start_tid; tid < start_tid + add_count; tid++) {
    if (!cpu_spawn_thread(k, st, tid, err)) {
      pthread_mutex_lock(&st->mu);
      st->n_threads = tid;
      pthread_mutex_unlock(&st->mu);
      return 0;
    }
  }

  pthread_mutex_lock(&st->mu);
  epa_platform_cond_broadcast(&st->cv);
  pthread_mutex_unlock(&st->mu);
  return 1;
}

uint32_t epa_sched_cpu_thread_thread_count(EpaSchedState *s) {
  CpuThreadState *st;
  uint32_t n;
  if (!s || !s->opaque) return 0u;
  st = (CpuThreadState*)s->opaque;
  pthread_mutex_lock(&st->mu);
  n = st->n_threads;
  pthread_mutex_unlock(&st->mu);
  return n;
}

static void cpu_destroy(EpaKernel *k, EpaSchedState *s) {
  (void)k;
  if (!s || !s->opaque) return;

  CpuThreadState *st = (CpuThreadState*)s->opaque;

  pthread_mutex_lock(&st->mu);
  st->stop = 1;
  epa_platform_cond_broadcast(&st->cv);
  pthread_mutex_unlock(&st->mu);

  for (uint32_t i = 0; i < st->n_threads; i++) {
    if (st->threads[i]) pthread_join(st->threads[i], NULL);
  }

  pthread_cond_destroy(&st->cv);
  pthread_mutex_destroy(&st->mu);

  free(st->threads);
  free(st->tid_to_wid);
  free(st);

  s->opaque = NULL;
}

static void cpu_request_interrupt(EpaKernel *k, EpaSchedState *s) {
  if (!k) return;

  // keep flag in kernel impl to match wave profile behavior
  k->impl.interrupt_requested = 1;

  // also force worker threads to park at safe boundary
  if (s && s->opaque) {
    CpuThreadState *st = (CpuThreadState*)s->opaque;
    atomic_store(&st->interrupt, 1);
    pthread_mutex_lock(&st->mu);
    epa_platform_cond_broadcast(&st->cv);
    pthread_mutex_unlock(&st->mu);
  }
}

static void cpu_wake(EpaKernel *k, EpaSchedState *s) {
  (void)k;
  if (!s || !s->opaque) return;
  CpuThreadState *st = (CpuThreadState*)s->opaque;
  pthread_mutex_lock(&st->mu);
  epa_platform_cond_broadcast(&st->cv);
  pthread_mutex_unlock(&st->mu);
}

// Bind free pool threads to runnable workers.
static void cpu_bind_runnable(EpaKernel *k, CpuThreadState *st) {
  // NOTE: called under st->mu.
  if (atomic_load(&st->interrupt) != 0 || k->impl.interrupt_requested) return;

  for (uint32_t wid = 0; wid < EPA_MAX_WORKERS; wid++) {
    EpaWorkerState *w = &k->impl.workers[wid];
    if (!worker_runnable(w)) continue;
    if (st->wid_to_tid[wid] >= 0) continue; // already bound

    // Find a free thread
    int32_t free_tid = -1;
    for (uint32_t tid = 0; tid < st->n_threads; tid++) {
      if (st->tid_to_wid[tid] < 0) { free_tid = (int32_t)tid; break; }
    }
    if (free_tid < 0) return; // no free threads right now

    st->tid_to_wid[free_tid] = (int32_t)wid;
    st->wid_to_tid[wid] = free_tid;
    epa_platform_cond_broadcast(&st->cv);
  }

  epa_platform_cond_broadcast(&st->cv);
}

static int all_threads_idle_locked(CpuThreadState *st) {
  for (uint32_t tid = 0; tid < st->n_threads; tid++) {
    if (st->tid_to_wid[tid] >= 0) return 0;
  }
  return 1;
}

static int cpu_run(EpaKernel *k,
                   EpaSchedState *s,
                   uint32_t max_ticks,
                   int debug,
                   char err[EPA_MAX_ERR]) {
  (void)debug;

  if (err) err[0] = 0;
  if (!k || !k->prog_loaded) {
    snprintf(err, EPA_MAX_ERR, "run: program not loaded");
    return 0;
  }
  if (!s || !s->opaque) {
    snprintf(err, EPA_MAX_ERR, "run: cpu_thread scheduler not initialised");
    return 0;
  }

  CpuThreadState *st = (CpuThreadState*)s->opaque;

  // Clear interrupt pause at the start of a run invocation.
  atomic_store(&st->interrupt, 0);
  k->impl.interrupt_requested = 0;

  // Drain ingress up front (same as wave)
  if (!epa_kernel_drain_ingress(k, err)) return 0;
  uint64_t ingress_seen = k->impl.ingress_deliveries;

  uint32_t ticks = 0;

  for (;;) {
    if (max_ticks && ticks >= max_ticks && !any_ignore_max_ticks_runnable(k)) {
      // In this profile, max_ticks is a management loop budget.
      if (debug) {
        snprintf(err, EPA_MAX_ERR, "run: step complete returning to host after %u ticks", ticks);
      } else {
        snprintf(err, EPA_MAX_ERR, "run: cpu_thread budget exhausted after %u ticks", ticks);
      }
      return 0;
    }

    if (k->impl.workers[0].faulted) {
      snprintf(err, EPA_MAX_ERR, "run: worker[0] faulted");
      return 0;
    }

    // Drain ingress each management cycle (may wake workers)
    if (!epa_kernel_drain_ingress(k, err)) return 0;
    if (ingress_seen != k->impl.ingress_deliveries) {
      ingress_seen = k->impl.ingress_deliveries;
      ticks = 0;
    }
    if (!any_ignore_max_ticks_runnable(k)) ticks++;

    pthread_mutex_lock(&st->mu);

    // Bind threads to runnable workers
    cpu_bind_runnable(k, st);

    // Interrupt/yield handling:
    if (k->impl.interrupt_requested) {
      atomic_store(&st->interrupt, 1);
      epa_platform_cond_broadcast(&st->cv);

      while (!st->stop && !all_threads_idle_locked(st)) {
        epa_platform_cond_wait(&st->cv, &st->mu);
      }

      // Clear and yield
      k->impl.interrupt_requested = 0;
      atomic_store(&st->interrupt, 0);

      pthread_mutex_unlock(&st->mu);
      return 2;
    }

    // Determine if any runnable worker is currently unbound (needs a thread)
    int any_runnable_unbound = 0;
    for (uint32_t wid = 0; wid < EPA_MAX_WORKERS; wid++) {
      EpaWorkerState *w = &k->impl.workers[wid];
      if (!worker_runnable(w)) continue;
      if (st->wid_to_tid[wid] < 0) { any_runnable_unbound = 1; break; }
    }

    // Headless artifact-driven execution: sleep until woken by ingress,
    // worker unbind, or explicit wake().
    if (!any_runnable_unbound) {
      if (max_ticks && all_threads_idle_locked(st)) {
        pthread_mutex_unlock(&st->mu);
        return 1;
      }
      epa_platform_cond_wait(&st->cv, &st->mu);
      pthread_mutex_unlock(&st->mu);
    } else {
      // Runnable work exists but no free threads; wait for a thread to return.
      epa_platform_cond_wait(&st->cv, &st->mu);
      pthread_mutex_unlock(&st->mu);
    }
  }
}

const EpaSchedulerVt EPA_SCHED_CPU_THREAD_VT = {
  .name = "cpu_thread",
  .init = cpu_init,
  .destroy = cpu_destroy,
  .request_interrupt = cpu_request_interrupt,
  .wake = cpu_wake,
  .run = cpu_run,
};
