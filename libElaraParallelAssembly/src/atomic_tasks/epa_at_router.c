#include "epa_at_router.h"
#include "../epa_kernel.h"
#include "../threads/epa_thread_pool.h"

#include <string.h>
#include <stdio.h>

#ifndef EPA_AT_ROUTER_MAX
#define EPA_AT_ROUTER_MAX 65536u
#endif

typedef struct {
  EpaKernel *k;
  EpaWorkerState *w;
  uint32_t sub_id;
  epa_ghs_t *ghs;
  epa_ghs_handle_t h;
} AtUserCtx;

static EpaAtExecEntry g_at_entries[EPA_AT_ROUTER_MAX];

// One batch + userctx per worker (safe because each worker runs only one AT at a time)
static EpaAtBatch *g_batches[EPA_MAX_WORKERS];
static AtUserCtx   g_users[EPA_MAX_WORKERS];

int epa_at_router_register(uint32_t sub_id, EpaAtExecEntry fn) {
  if (sub_id >= EPA_AT_ROUTER_MAX) return 0;
  g_at_entries[sub_id] = fn;
  return 1;
}

// Pool callback: translate from generic batch callback into AT entry call
static int at_batch_exec_thunk(void *batch, uint32_t vtid, int32_t tid, void *user) {
  EpaAtBatch *b = (EpaAtBatch*)batch;
  AtUserCtx *u = (AtUserCtx*)user;
  if (!u || u->sub_id >= EPA_AT_ROUTER_MAX) return 0;

  EpaAtExecEntry fn = g_at_entries[u->sub_id];
  if (!fn) return 0;

  return fn(b, vtid, tid, u->ghs, u->h) ? 1 : 0;
}

static void at_batch_finish_wake(EpaAtBatch *b, void *user) {
  (void)b;
  AtUserCtx *u = (AtUserCtx*)user;
  if (!u || !u->w) return;

  u->w->blocked = 0;
  if (u->k && u->k->sched_vt && u->k->sched_vt->wake) {
    u->k->sched_vt->wake((struct EpaKernel*)u->k, &u->k->sched_state);
  }
}

int epa_at_router_launch_parallel(
	EpaKernel      *k,
    EpaWorkerState *w,
    uint32_t        sub_id,
    epa_ghs_t      *ghs,
    epa_ghs_handle_t h,
    uint32_t        thread_count,
    char            err[EPA_MAX_ERR]
) {
  if (err) err[0] = 0;
  if (!k || !w) { snprintf(err, EPA_MAX_ERR, "at_router: null k/w"); return 0; }
  if (!k->impl.tp) { snprintf(err, EPA_MAX_ERR, "at_router: thread pool not configured"); return 0; }
  if (!ghs) { snprintf(err, EPA_MAX_ERR, "at_router: null ghs"); return 0; }
  if (thread_count == 0) { snprintf(err, EPA_MAX_ERR, "at_router: thread_count=0"); return 0; }
  if (sub_id >= EPA_AT_ROUTER_MAX || !g_at_entries[sub_id]) {
    snprintf(err, EPA_MAX_ERR, "at_router: unknown sub_id=%u", (unsigned)sub_id);
    return 0;
  }
  if (w->at_running) {
    snprintf(err, EPA_MAX_ERR, "at_router: worker already running AT");
    return 0;
  }

  uint32_t wid = w->id;
  if (wid >= EPA_MAX_WORKERS) {
    snprintf(err, EPA_MAX_ERR, "at_router: bad wid=%u", (unsigned)wid);
    return 0;
  }

  // Prepare per-worker user context
  g_users[wid].k = k;
  g_users[wid].w = w;
  g_users[wid].sub_id = sub_id;
  g_users[wid].ghs = ghs;
  g_users[wid].h = h;

  // Prepare batch
  EpaAtBatch *batch = NULL;
  if (epa_at_batch_create((uint16_t)sub_id, thread_count, &batch) != EPA_AT_OK || !batch) {
    snprintf(err, EPA_MAX_ERR, "at_router: batch create failed");
    return 0;
  }
  batch->exec_fn = at_batch_exec_thunk;
  batch->user = &g_users[wid];
  batch->on_finish = at_batch_finish_wake;
  batch->finish_user = &g_users[wid];
  g_batches[wid] = batch;

  // Record minimal info in worker (debug + WAIT_FOR_AT join logic)
  w->at.active = 1;
  w->at.fn = sub_id;
  w->at.init_h = h;
  w->at.want_threads = thread_count;
  w->at.got_threads = thread_count;
  w->at.done_threads = 0;
  w->at_running = 1;

  // Submit to pool
  if (!epa_thread_pool_submit_batch(k->impl.tp, batch)) {
    w->at_running = 0;
    w->at.active = 0;
    epa_at_batch_destroy(batch);
    g_batches[wid] = NULL;
    snprintf(err, EPA_MAX_ERR, "at_router: submit_batch failed");
    return 0;
  }

  return 1;
}

int epa_at_router_update_worker(EpaWorkerState *w, char err[EPA_MAX_ERR]) {
  if (err) err[0] = 0;
  if (!w) {
    if (err) snprintf(err, EPA_MAX_ERR, "at_router: null worker");
    return 0;
  }
  if (!w->at_running) return 1;
  if (w->id >= EPA_MAX_WORKERS) {
    if (err) snprintf(err, EPA_MAX_ERR, "at_router: bad wid=%u", (unsigned)w->id);
    return 0;
  }

  EpaAtBatch *batch = g_batches[w->id];
  if (!batch) {
    if (err) snprintf(err, EPA_MAX_ERR, "at_router: missing batch for wid=%u", (unsigned)w->id);
    return 0;
  }

  w->at.done_threads = atomic_load(&batch->done_count);
  if (!epa_at_batch_all_finished(batch)) return 1;

  if (epa_at_batch_has_fault(batch)) {
    epa_at_batch_destroy(batch);
    g_batches[w->id] = NULL;
    w->at_running = 0;
    w->at.active = 0;
    if (err) snprintf(err, EPA_MAX_ERR, "at_router: AT subtask failed");
    return 0;
  }

  w->at.done_threads = w->at.got_threads;
  w->at_running = 0;
  w->at.active = 0;
  epa_at_batch_destroy(batch);
  g_batches[w->id] = NULL;
  return 1;
}
