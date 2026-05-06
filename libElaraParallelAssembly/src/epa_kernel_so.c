// epa_kernel_so.c
#include "epa_kernel_so.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epa_asm_compiler.h"
#include "log.h"
#include "epa_program_loader.h"
#include "epa_flow_glue.h"
#include "epa_backend_nonflow.h"
#include "epa_instruct_common.h"
#include "gui/viewport.h"

#include "memory/epa_ring_buffer.h"
#include "vm/epa_worker_state.h"

#include "epa_kernel_hooks.h"

#ifndef EPA_AT_POOL_THREADS
#define EPA_AT_POOL_THREADS 8u
#endif

int epa_kernel_set_scheduler(EpaKernel *k,
                             EpaSchedProfile profile,
                             char err[EPA_MAX_ERR]);

void epa_kernel_set_debug_callback(EpaKernel *k, EpaKernelDbgCallback cb, void *cb_user) {
  if (!k) return;
  k->dbg_cb = cb;
  k->dbg_user = cb_user;
}

EpaKernel* epa_kernel_create(char err[EPA_MAX_ERR]) {
  if (err) err[0] = 0;

  EpaKernel *k = (EpaKernel*)calloc(1, sizeof(EpaKernel));
  if (!k) { if (err) snprintf(err, EPA_MAX_ERR, "OOM creating kernel"); return NULL; }

  // init rings
  if (!epa_ring_init(&k->impl.syncq, 256)) {
    if (err) snprintf(err, EPA_MAX_ERR, "sync ring init failed");
    free(k);
    return NULL;
  }

  k->impl.ghs = epa_ghs_create(65536, NULL, NULL, NULL);
  if (!k->impl.ghs) {
     TRACE("GHS init failed");
     return 0; // or your init fail path
  }

  k->impl.tp = (EpaThreadPool*)calloc(1, sizeof(EpaThreadPool));
  if (!k->impl.tp) {
    if (err) snprintf(err, EPA_MAX_ERR, "OOM creating AT thread pool");
    epa_ghs_destroy(k->impl.ghs);
    free(k);
    return NULL;
  }
  if (!epa_thread_pool_init(k->impl.tp, EPA_AT_POOL_THREADS)) {
    if (err) snprintf(err, EPA_MAX_ERR, "AT thread pool init failed");
    free(k->impl.tp);
    epa_ghs_destroy(k->impl.ghs);
    free(k);
    return NULL;
  }

  epa_kernel_set_scheduler(k, EPA_SCHED_WAVE, err);

  return k;
}

void epa_kernel_destroy(EpaKernel *k) {
  if (!k) return;

  // free workers
  for (uint32_t i = 0; i < EPA_MAX_WORKERS; i++) {
    if (k->impl.workers[i].inited) epa_worker_free(&k->impl.workers[i]);
    memset(&k->impl.workers[i], 0, sizeof(k->impl.workers[i]));
  }

  if (k->impl.ghs) {
  	  epa_ghs_destroy(k->impl.ghs);
  	  k->impl.ghs = NULL;
  }

  if (k->impl.tp) {
    epa_thread_pool_shutdown(k->impl.tp);
    free(k->impl.tp);
    k->impl.tp = NULL;
  }

  epa_ring_free(&k->impl.syncq);

  if (k->vp) vp_destroy(k->vp);

  if (k->prog_loaded) epa_program_free(&k->prog);

  free(k);
}

static int init_workers_from_prog(KernelImpl *k, const EpaProgramDesc *prog, char err[EPA_MAX_ERR]) {
  // wipe old
  for (uint32_t i = 0; i < EPA_MAX_WORKERS; i++) {
    if (k->workers[i].inited) epa_worker_free(&k->workers[i]);
    memset(&k->workers[i], 0, sizeof(k->workers[i]));
  }

  for (uint32_t id = 0; id < 256; id++) {
    if (!prog->entry_present[id]) continue;

    // We treat the descriptor's code view as the entire entry body.
    // body_start_pc/body_end_pc are relative to the *original blob* in legacy,
    // but now flow resolves via descriptors, so worker just needs "inited + vm reset".
    // Still initialize worker so rings/vm/locals exist.
    uint32_t body_start = 0;
    uint32_t body_end   = (uint32_t)prog->entries[id].code_len;

    uint32_t in_words  = (uint32_t)prog->entry_in_words[id];
    uint32_t out_words = (uint32_t)prog->entry_out_words[id];
    uint32_t signal_mailbox_size = (uint32_t)prog->signal_mailbox_size[id];

    if (!epa_worker_init(&k->workers[id], id, body_start, body_end, in_words, out_words, signal_mailbox_size, err)) {
      return 0;
    }

    // Scheduling policy: kernel (0) runs, others sleep until ENTRY_EXEC
    k->workers[id].blocked = (id == 0) ? 0 : 1;

    // Store EIP in worker state if you've added it there.
    // If you haven't yet: add `EpaEip eip;` into EpaWorkerState.
    k->workers[id].vm.eip.block_type = EPA_BLOCK_ENTRY;
    k->workers[id].vm.eip.block_id   = (uint16_t)id;
    k->workers[id].vm.eip.rel_pc     = 0;
  }

  if (!k->workers[0].inited) {
    snprintf(err, EPA_MAX_ERR, "no entry 0 (kernel) found");
    return 0;
  }

  return 1;
}

int epa_kernel_load_asm(EpaKernel *k, const char *asm_path, char err[EPA_MAX_ERR]) {
  if (!k || !asm_path) { snprintf(err, EPA_MAX_ERR, "load_asm: bad args"); return 0; }

  size_t blob_len = 0;
  uint8_t *blob = epa_asm_compile_file(asm_path, &blob_len, err);
  if (!blob) return 0;

  // parse takes ownership on success (per your comment)
  if (k->prog_loaded) {
    epa_program_free(&k->prog);
    k->prog_loaded = 0;
  }

  if (!epa_program_parse(&k->prog, blob, blob_len, err)) {
    free(blob);
    return 0;
  }
  k->prog_loaded = 1;

  if (!init_workers_from_prog(&k->impl, &k->prog, err)) return 0;

  // Build flow ctx + hooks
  memset(&k->hooks, 0, sizeof(k->hooks));
  k->hooks.on_entry_exec   = hook_entry_exec;
  k->hooks.on_entry_halt   = hook_entry_halt;
  k->hooks.on_sync         = hook_sync;
  k->hooks.on_wait_on_sync = hook_wait_on_sync;
  k->hooks.get_worker      = hook_get_worker;

  k->hooks.on_break        = hook_break;
  k->hooks.on_trap         = hook_trap;
  k->hooks.on_except       = hook_except;
  k->hooks.on_signal	   = hook_signal;

  k->flow = epa_flow_ctx_make(&k->prog, k->hooks, k);

  // non-flow backend
  extern EpaNonFlowBackend epa_opengl_nonflow_backend(void *impl);
  k->nf = epa_opengl_nonflow_backend(&k->impl);

  return 1;
}

int epa_kernel_open_viewport(EpaKernel *k, int w, int h, const char *title, int enable_cuda, char err[EPA_MAX_ERR]) {
  if (!k) { snprintf(err, EPA_MAX_ERR, "open_viewport: kernel null"); return 0; }
  if (k->vp) vp_destroy(k->vp);
  k->vp = vp_create(w, h, title ? title : "Elara", enable_cuda);
  if (!k->vp) { snprintf(err, EPA_MAX_ERR, "viewport init failed"); return 0; }
  return 1;
}

void epa_kernel_close_viewport(EpaKernel *k) {
  if (!k) return;
  if (k->vp) vp_destroy(k->vp);
  k->vp = NULL;
}

static uint32_t pad4(uint32_t n) { return (n + 3u) & ~3u; }

int epa_kernel_ingress_push(EpaKernel *k, uint32_t wid, const void *data, uint32_t len) {
	if (!k || wid >= EPA_MAX_WORKERS) return 0;

	EpaIngressQ *q = &k->ingress.inq[wid];
	if (q->count >= EPA_INGRESS_QMAX) return 0;

	uint32_t plen = pad4(len);

	uint8_t *b = (uint8_t*)malloc(plen);
	if (!b) return 0;

	memcpy(b, data, len);

	if (plen > len)
		memset(b + len, 0, plen - len);

	q->q[q->tail].buf = b;
	q->q[q->tail].len = plen;
	q->tail = (q->tail + 1) % EPA_INGRESS_QMAX;
	q->count++;

	return 0;
}

static void ingress_free_msg(EpaIngressMsg *m) {
  free(m->buf);
  m->buf = NULL;
  m->len = 0;
}

// Ring message kinds (keep tiny + fixed-size)
#ifndef EPA_RMSG_GHS_PAYLOAD
#define EPA_RMSG_GHS_PAYLOAD  1u
#endif

// Layout for EPA_RMSG_GHS_PAYLOAD (u32 words pushed into dst->inq):
//   word0 = EPA_RMSG_GHS_PAYLOAD
//   word1 = handle_idx
//   word2 = handle_gen
//   word3 = payload_len_bytes

static int epa_kernel_deliver_ingress_msg(EpaKernel *k,
                                         uint32_t dst_wid,
                                         const uint8_t *bytes,
                                         uint32_t len_bytes,
                                         char err[EPA_MAX_ERR]) {
  if (!k || !bytes || len_bytes == 0) {
    snprintf(err, EPA_MAX_ERR, "ingress: bad args");
    return 0;
  }
  if (dst_wid >= EPA_MAX_WORKERS) {
    snprintf(err, EPA_MAX_ERR, "ingress: bad wid %u", dst_wid);
    return 0;
  }

  // GHS is shared; convention: kernel (owner=0) allocs first.
  epa_ghs_t *ghs = k->impl.ghs;
  if (!ghs) {
    snprintf(err, EPA_MAX_ERR, "ingress: GHS not initialized");
    return 0;
  }

  // 1) Allocate GHS object of type BYTES, owned by kernel (0)
  epa_ghs_handle_t h = 0;
  epa_ghs_err_t ge = epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, /*owner=*/0, len_bytes, &h);
  if (ge != EPA_GHS_OK) {
    snprintf(err, EPA_MAX_ERR, "ingress: epa_ghs_alloc failed (%d)", (int)ge);
    return 0;
  }

  // 2) Copy payload into GHS storage
  void *ptr = NULL;
  ge = epa_ghs_get_ptr(ghs, h, &ptr);
  if (ge != EPA_GHS_OK || !ptr) {
    snprintf(err, EPA_MAX_ERR, "ingress: epa_ghs_get_ptr failed (%d)", (int)ge);
    (void)epa_ghs_free(ghs, h);
    return 0;
  }
  memcpy(ptr, bytes, (size_t)len_bytes);

  // 3) Transfer ownership to dst worker
  ge = epa_ghs_transfer(ghs, h, dst_wid);
  if (ge != EPA_GHS_OK) {
    snprintf(err, EPA_MAX_ERR, "ingress: epa_ghs_transfer failed (%d)", (int)ge);
    (void)epa_ghs_free(ghs, h);
    return 0;
  }

  // 4) Ring notify: push 4 words to dst worker input ring
  EpaWorkerState *dst = &k->impl.workers[dst_wid];

  if (epa_ring_space(&dst->inq) < 4) {
    snprintf(err, EPA_MAX_ERR, "ingress: wid=%u inq full (need 4 words)", dst_wid);
    return 0;
  }

  uint32_t idx = epa_ghs_handle_index(h);
  uint32_t gen2 = epa_ghs_handle_gen(h);

  // epa_ring_push takes err[256]; EPA_MAX_ERR is 256 in your project.
  if (!epa_ring_push(&dst->inq, 1 /* Always one because there is only one GHS handle*/, 0, err)) return 0;
  if (!epa_ring_push(&dst->inq, idx,               0, err)) return 0;
  if (!epa_ring_push(&dst->inq, gen2,              0, err)) return 0;

  // Wake worker ONLY if it was explicitly waiting for data
  if (dst->waiting_for_data) {
    dst->waiting_for_data = 0;
    dst->blocked = 0;
  }

  return 1;
}

int epa_kernel_deliver_ghs_handles(EpaKernel *k,
                                         uint32_t dst_wid,
                                         const uint64_t *ghs_handles,
                                         uint32_t ghs_handle_count,
                                         char err[EPA_MAX_ERR]) {
  if (!k || !ghs_handles || ghs_handle_count == 0) {
    snprintf(err, EPA_MAX_ERR, "ingress: bad args");
    return 1;
  }
  if (dst_wid >= EPA_MAX_WORKERS) {
    snprintf(err, EPA_MAX_ERR, "ingress: bad wid %u", dst_wid);
    return 1;
  }

  // GHS is shared; convention: kernel (owner=0).
  epa_ghs_t *ghs = k->impl.ghs;
  if (!ghs) {
    snprintf(err, EPA_MAX_ERR, "ingress: GHS not initialized");
    return 1;
  }

  // 3) Transfer ownership to dst worker
  for (uint32_t i=0; i<ghs_handle_count; i++) {
	  // Should only transfer ownership if flags are not special cases,
	  // special cases are not transfered here they can only be transfered manually

	  uint8_t flags;
	  if (epa_ghs_get_flags(ghs, ghs_handles[i], &flags) == EPA_GHS_OK) {
		  if (!flags) {
			  int ge = epa_ghs_transfer(ghs, ghs_handles[i], dst_wid);
			  if (ge != EPA_GHS_OK) {
				snprintf(err, EPA_MAX_ERR, "ingress: epa_ghs_transfer failed (%d)", (int)ge);
				(void)epa_ghs_free(ghs, ghs_handles[i]);
				return 1;
			  }
		  }
	  }
  }

  // 4) Ring notify: push 4 words to dst worker input ring
  EpaWorkerState *dst = &k->impl.workers[dst_wid];

  if (epa_ring_space(&dst->inq) < 4) {
    snprintf(err, EPA_MAX_ERR, "ingress: wid=%u inq full (need 4 words)", dst_wid);
    return 1;
  }

  // epa_ring_push takes err[256]; EPA_MAX_ERR is 256 in your project.
  if (!epa_ring_push(&dst->inq, ghs_handle_count, 0, err)) return 1;

  for (int i=0; i<ghs_handle_count; i++) {
	  uint32_t idx = epa_ghs_handle_index(ghs_handles[i]);
	  uint32_t gen2 = epa_ghs_handle_gen(ghs_handles[i]);

	  if (!epa_ring_push(&dst->inq, idx,               0, err)) return 1;
	  if (!epa_ring_push(&dst->inq, gen2,              0, err)) return 1;
  }

  // Wake worker ONLY if it was explicitly waiting for data
  if (dst->waiting_for_data) {
    dst->waiting_for_data = 0;
    dst->blocked = 0;
  }

  return 0;
}

// Called at the start of epa_kernel_run() (i.e., "reentry boundary")
int epa_kernel_drain_ingress(EpaKernel *k, char err[EPA_MAX_ERR]) {
  if (err) err[0] = 0;
  if (!k) { snprintf(err, EPA_MAX_ERR, "ingress: kernel NULL"); return 0; }

  for (uint32_t wid = 0; wid < EPA_MAX_WORKERS; wid++) {
    EpaIngressQ *q = &k->ingress.inq[wid];

    while (q->count) {
      EpaIngressMsg *m = &q->q[q->head];

      // Your msg fields might be (m->data, m->len_bytes). Adjust if needed.
      const uint8_t *buf = (const uint8_t*)m->buf;
      uint32_t len = (uint32_t)m->len;

      if (!buf || len == 0) {
        // consume malformed msg so we don't deadlock the queue
        ingress_free_msg(m);
        q->head = (q->head + 1) % EPA_INGRESS_QMAX;
        q->count--;
        continue;
      }

      if (!epa_kernel_deliver_ingress_msg(k, wid, buf, len, err)) {
        // do NOT consume the message if delivery failed: keeps behavior deterministic
        // (host can retry / expand rings / etc)
        return 0;
      }

      // consume on success
      ingress_free_msg(m);
      q->head = (q->head + 1) % EPA_INGRESS_QMAX;
      q->count--;
    }
  }

  return 1;
}

extern const EpaSchedulerVt EPA_SCHED_WAVE_VT;
extern const EpaSchedulerVt EPA_SCHED_CPU_THREAD_VT;

static const EpaSchedulerVt *epa_sched_vt_for(EpaSchedProfile p) {
  switch (p) {
    case EPA_SCHED_WAVE:
      return &EPA_SCHED_WAVE_VT;
    case EPA_SCHED_CPU_THREAD:
      return &EPA_SCHED_CPU_THREAD_VT;
    default:
      return NULL;
  }
}

static const EpaSchedulerVt *epa_sched_vt_for(EpaSchedProfile p);

/* Select scheduler */
int epa_kernel_set_scheduler(EpaKernel *k,
                             EpaSchedProfile profile,
                             char err[EPA_MAX_ERR]) {
  if (!k) {
    if (err) snprintf(err, EPA_MAX_ERR, "kernel null");
    return 0;
  }

  const EpaSchedulerVt *vt = epa_sched_vt_for(profile);
  if (!vt) {
    if (err) snprintf(err, EPA_MAX_ERR, "unknown scheduler");
    return 0;
  }

  if (k->sched_vt && k->sched_vt->destroy)
    k->sched_vt->destroy(k, &k->sched_state);

  k->sched_vt = vt;
  k->sched_profile = profile;
  k->sched_state.interrupt_requested = 0;
  k->sched_state.opaque = NULL;

  if (vt->init)
    return vt->init(k, &k->sched_state, err);

  return 1;
}

/* Public run entry */
int epa_kernel_run(EpaKernel *k,
                   uint32_t max_ticks,
                   int debug,
                   char err[EPA_MAX_ERR]) {
  if (!k || !k->sched_vt) {
    if (err) snprintf(err, EPA_MAX_ERR, "scheduler not set");
    return 0;
  }
  return k->sched_vt->run(k, &k->sched_state, max_ticks, debug, err);
}

/* Interrupt request */
void epa_kernel_request_interrupt(EpaKernel *k) {
  if (k && k->sched_vt && k->sched_vt->request_interrupt)
    k->sched_vt->request_interrupt(k, &k->sched_state);
}
