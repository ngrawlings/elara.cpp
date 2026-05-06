#include "epa_thread_pool.h"

#include "../atomic_tasks/epa_atomic_tasks.h"   // must define EpaAtBatch + batch APIs
#include "../vm/epa_vm.h"                       // EpaVM init/reset/free

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>




static void ring_init(BatchRing *r) { memset(r, 0, sizeof(*r)); }

static int ring_push(BatchRing *r, EpaAtBatch *b) {
  if (r->count >= EPA_TP_MAX_BATCHES) return 0;
  r->items[r->tail] = b;
  r->tail = (r->tail + 1u) % EPA_TP_MAX_BATCHES;
  r->count++;
  return 1;
}

static EpaAtBatch* ring_pop(BatchRing *r) {
  if (r->count == 0) return NULL;
  EpaAtBatch *b = r->items[r->head];
  r->head = (r->head + 1u) % EPA_TP_MAX_BATCHES;
  r->count--;
  return b;
}

// --------------------
// TLS: expose current thread's VM to AT code without threading plumbing yet
// --------------------
static pthread_key_t  g_vm_key;
static pthread_once_t g_vm_key_once = PTHREAD_ONCE_INIT;

static void make_vm_key(void) {
  (void)pthread_key_create(&g_vm_key, NULL);
}

static void vm_tls_set(EpaVM *vm) {
  pthread_once(&g_vm_key_once, make_vm_key);
  (void)pthread_setspecific(g_vm_key, (void*)vm);
}

EpaVM *epa_thread_pool_tls_vm(void) {
  pthread_once(&g_vm_key_once, make_vm_key);
  return (EpaVM*)pthread_getspecific(g_vm_key);
}

// --------------------
// Thread entry loop
// --------------------
static void* tp_thread_main(void *argp) {
  TpThreadCtx *ctx = (TpThreadCtx*)argp;
  EpaThreadPool *p = ctx->p;
  const uint32_t tid = ctx->tid;

  // idle by default
  p->vtid[tid] = -1;

  // Init VM once for the lifetime of this OS thread
  if (epa_vm_init_with_lbytes(&ctx->vm, EPA_TP_THREAD_LBYTES) != 0) {
    // fatal: thread cannot participate
    return NULL;
  }
  ctx->vm_inited = 1;

  // Publish VM into TLS so AT code can find it.
  vm_tls_set(&ctx->vm);

  for (;;) {
    pthread_mutex_lock(&p->mu);

    while (!p->stop && p->q.count == 0) {
      atomic_fetch_add(&p->waiting, 1);
      pthread_cond_wait(&p->cv, &p->mu);
      atomic_fetch_sub(&p->waiting, 1);
    }

    if (p->stop) {
      pthread_mutex_unlock(&p->mu);
      break;
    }

    EpaAtBatch *b = ring_pop(&p->q);
    pthread_mutex_unlock(&p->mu);

    if (!b) continue;

    // Drop completed batches
    if (epa_at_batch_all_finished(b)) continue;

    // Try claim a subtask; returned v is VTID
    uint32_t v = 0;
    EpaAtRc rc = epa_at_batch_claim(b, (int32_t)tid, &v);

    if (rc == EPA_AT_OK) {
      p->vtid[tid] = (int32_t)v;

      // Reset VM to a known state at the start of each block
      epa_vm_reset(&ctx->vm);

      // Execute one subtask via batch-defined handler (threads remain generic)
      // AT code can pull the VM from TLS: epa_thread_pool_tls_vm().
      int ok = epa_at_batch_exec_block(b, v, (int32_t)tid) ? 1 : 0;

      (void)epa_at_batch_complete(b, (int32_t)tid, v, ok);

      p->vtid[tid] = -1;

      if (epa_at_batch_all_finished(b) && b->on_finish) {
        b->on_finish(b, b->finish_user);
      }

      // Requeue if more work remains
      pthread_mutex_lock(&p->mu);
      if (!epa_at_batch_all_finished(b)) {
        (void)ring_push(&p->q, b);
      }
      pthread_cond_broadcast(&p->cv);
      pthread_mutex_unlock(&p->mu);

      continue;
    }

    // No claimable blocks right now; requeue if not done.
    if (!epa_at_batch_all_finished(b)) {
      pthread_mutex_lock(&p->mu);
      (void)ring_push(&p->q, b);
      pthread_mutex_unlock(&p->mu);
    }
  }

  p->vtid[tid] = -1;

  if (ctx->vm_inited) {
    // clear TLS slot (optional hygiene)
    vm_tls_set(NULL);
    epa_vm_free(&ctx->vm);
    ctx->vm_inited = 0;
  }

  return NULL;
}

// --------------------
// Public API
// --------------------
int epa_thread_pool_init(EpaThreadPool *p, uint32_t n_threads) {
  if (!p || n_threads == 0) return 0;
  memset(p, 0, sizeof(*p));

  p->n_threads = n_threads;

  p->threads = (pthread_t*)calloc(n_threads, sizeof(pthread_t));
  p->vtid    = (int32_t*)calloc(n_threads, sizeof(int32_t));
  p->tctx    = (TpThreadCtx*)calloc(n_threads, sizeof(TpThreadCtx));

  if (!p->threads || !p->vtid || !p->tctx) {
    free(p->threads);
    free(p->vtid);
    free(p->tctx);
    memset(p, 0, sizeof(*p));
    return 0;
  }

  for (uint32_t i = 0; i < n_threads; i++) p->vtid[i] = -1;

  pthread_mutex_init(&p->mu, NULL);
  pthread_cond_init(&p->cv, NULL);
  ring_init(&p->q);
  atomic_store(&p->waiting, 0);

  for (uint32_t i = 0; i < n_threads; i++) {
    TpThreadCtx *ctx = &p->tctx[i];
    ctx->p = p;
    ctx->tid = i;
    ctx->vm_inited = 0;
    memset(&ctx->vm, 0, sizeof(ctx->vm));

    if (pthread_create(&p->threads[i], NULL, tp_thread_main, ctx) != 0) {
      // stop what we started, join those already created
      pthread_mutex_lock(&p->mu);
      p->stop = 1;
      pthread_cond_broadcast(&p->cv);
      pthread_mutex_unlock(&p->mu);

      for (uint32_t j = 0; j < i; j++) {
        pthread_join(p->threads[j], NULL);
      }

      pthread_cond_destroy(&p->cv);
      pthread_mutex_destroy(&p->mu);

      free(p->threads);
      free(p->vtid);
      free(p->tctx);
      memset(p, 0, sizeof(*p));
      return 0;
    }
  }

  return 1;
}

void epa_thread_pool_shutdown(EpaThreadPool *p) {
  if (!p) return;

  pthread_mutex_lock(&p->mu);
  p->stop = 1;
  pthread_cond_broadcast(&p->cv);
  pthread_mutex_unlock(&p->mu);

  for (uint32_t i = 0; i < p->n_threads; i++) {
    if (p->threads[i]) pthread_join(p->threads[i], NULL);
  }

  pthread_cond_destroy(&p->cv);
  pthread_mutex_destroy(&p->mu);

  free(p->threads);
  free(p->vtid);
  free(p->tctx);

  memset(p, 0, sizeof(*p));
}

int epa_thread_pool_submit_batch(EpaThreadPool *p, EpaAtBatch *batch) {
  if (!p || !batch) return 0;

  pthread_mutex_lock(&p->mu);
  int ok = ring_push(&p->q, batch);
  if (ok) pthread_cond_broadcast(&p->cv);
  pthread_mutex_unlock(&p->mu);

  return ok;
}

void epa_thread_pool_wake(EpaThreadPool *p) {
  if (!p) return;
  pthread_mutex_lock(&p->mu);
  pthread_cond_broadcast(&p->cv);
  pthread_mutex_unlock(&p->mu);
}

uint32_t epa_thread_pool_thread_count(EpaThreadPool *p) {
  return p ? p->n_threads : 0;
}

uint32_t epa_thread_pool_waiting_count(EpaThreadPool *p) {
  return p ? atomic_load(&p->waiting) : 0;
}

int32_t epa_thread_pool_get_vtid(EpaThreadPool *p, uint32_t tid) {
  if (!p || tid >= p->n_threads) return -1;
  return p->vtid[tid];
}
