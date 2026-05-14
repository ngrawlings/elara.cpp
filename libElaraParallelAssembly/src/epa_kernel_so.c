// epa_kernel_so.c
#include "epa_kernel_so.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

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

#define EPA_BUNDLE_MAGIC "EPABNDL1"
#define EPA_BUNDLE_VERSION 1u
#define EPA_BUNDLE_FLAG_ROOT    0x00000001u
#define EPA_BUNDLE_FLAG_STARTED 0x00000002u

typedef struct {
  char *path_id;
  uint32_t flags;
  uint8_t *blob;
  size_t blob_len;
  EpaKernel *kernel;
  pthread_t thread;
  int thread_started;
  int stop_requested;
  char last_error[EPA_MAX_ERR];
} EpaKernelModuleEntry;

struct EpaKernelModule {
  size_t count;
  EpaKernelModuleEntry *entries;
};

typedef struct KernelRegistryNode {
  char *kernel_id;
  EpaKernel *kernel;
  struct KernelRegistryNode *next;
} KernelRegistryNode;

static pthread_mutex_t g_kernel_registry_mu = PTHREAD_MUTEX_INITIALIZER;
static KernelRegistryNode *g_kernel_registry = NULL;

static char *kernel_strdup(const char *s) {
  size_t n;
  char *p;
  if (!s) return NULL;
  n = strlen(s);
  p = (char*)malloc(n + 1u);
  if (!p) return NULL;
  memcpy(p, s, n + 1u);
  return p;
}

static void registry_remove_kernel_locked(EpaKernel *k) {
  KernelRegistryNode **pp = &g_kernel_registry;
  while (*pp) {
    KernelRegistryNode *node = *pp;
    if (node->kernel == k) {
      *pp = node->next;
      free(node->kernel_id);
      free(node);
      continue;
    }
    pp = &node->next;
  }
}

static EpaKernel *registry_find_kernel_locked(const char *kernel_id) {
  KernelRegistryNode *node = g_kernel_registry;
  while (node) {
    if (strcmp(node->kernel_id, kernel_id) == 0) return node->kernel;
    node = node->next;
  }
  return NULL;
}

static uint32_t kernel_default_ingress_wid(const EpaKernel *k) {
  uint32_t wid;
  if (!k || !k->prog_loaded) return 0u;
  for (wid = 1u; wid < EPA_MAX_WORKERS; wid++) {
    if (k->prog.entry_present[wid]) return wid;
  }
  return 0u;
}

static void epa_kernel_set_status_text(EpaKernel *k, EpaKernelStatus status, const char *detail) {
  if (!k) return;
  pthread_mutex_lock(&k->state_mu);
  k->runtime_status = (int)status;
  if (detail) {
    snprintf(k->last_error, sizeof(k->last_error), "%s", detail);
  } else {
    k->last_error[0] = 0;
  }
  pthread_mutex_unlock(&k->state_mu);
}

static void epa_kernel_set_status_only(EpaKernel *k, EpaKernelStatus status) {
  if (!k) return;
  pthread_mutex_lock(&k->state_mu);
  k->runtime_status = (int)status;
  pthread_mutex_unlock(&k->state_mu);
}

const char* epa_kernel_status_name(EpaKernelStatus status) {
  switch (status) {
    case EPA_KERNEL_STATUS_UNLOADED: return "unloaded";
    case EPA_KERNEL_STATUS_LOADED: return "loaded";
    case EPA_KERNEL_STATUS_RUNNING: return "running";
    case EPA_KERNEL_STATUS_STOPPED: return "stopped";
    case EPA_KERNEL_STATUS_HALTED: return "halted";
    case EPA_KERNEL_STATUS_FAULTED: return "faulted";
    case EPA_KERNEL_STATUS_ERROR: return "error";
    default: return "unknown";
  }
}

EpaKernelStatus epa_kernel_get_status(const EpaKernel *k) {
  EpaKernelStatus status;
  if (!k) return EPA_KERNEL_STATUS_ERROR;
  pthread_mutex_lock((pthread_mutex_t*)&k->state_mu);
  status = (EpaKernelStatus)k->runtime_status;
  pthread_mutex_unlock((pthread_mutex_t*)&k->state_mu);
  return status;
}

const char* epa_kernel_get_last_error(const EpaKernel *k) {
  return k ? k->last_error : NULL;
}

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
  pthread_mutex_init(&k->impl.syncq_mu, NULL);
  pthread_mutex_init(&k->state_mu, NULL);
  k->runtime_status = EPA_KERNEL_STATUS_UNLOADED;
  k->last_error[0] = 0;

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

  pthread_mutex_lock(&g_kernel_registry_mu);
  registry_remove_kernel_locked(k);
  pthread_mutex_unlock(&g_kernel_registry_mu);

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
  pthread_mutex_destroy(&k->impl.syncq_mu);
  pthread_mutex_destroy(&k->state_mu);

  if (k->vp) vp_destroy(k->vp);

  if (k->prog_loaded) epa_program_free(&k->prog);
  free(k->owned_blob);
  k->owned_blob = NULL;
  k->owned_blob_len = 0;

  free(k->kernel_id);
  k->kernel_id = NULL;

  free(k);
}

int epa_kernel_set_id(EpaKernel *k, const char *kernel_id, char err[EPA_MAX_ERR]) {
  char *copy;
  KernelRegistryNode *node;
  if (err) err[0] = 0;
  if (!k || !kernel_id || !kernel_id[0]) {
    if (err) snprintf(err, EPA_MAX_ERR, "kernel_set_id: bad args");
    return 0;
  }
  copy = kernel_strdup(kernel_id);
  if (!copy) {
    if (err) snprintf(err, EPA_MAX_ERR, "kernel_set_id: OOM");
    return 0;
  }

  pthread_mutex_lock(&g_kernel_registry_mu);
  node = g_kernel_registry;
  while (node) {
    if (strcmp(node->kernel_id, kernel_id) == 0 && node->kernel != k) {
      pthread_mutex_unlock(&g_kernel_registry_mu);
      free(copy);
      if (err) snprintf(err, EPA_MAX_ERR, "kernel_set_id: duplicate id '%s'", kernel_id);
      return 0;
    }
    node = node->next;
  }

  registry_remove_kernel_locked(k);
  node = (KernelRegistryNode*)calloc(1, sizeof(*node));
  if (!node) {
    pthread_mutex_unlock(&g_kernel_registry_mu);
    free(copy);
    if (err) snprintf(err, EPA_MAX_ERR, "kernel_set_id: OOM");
    return 0;
  }
  node->kernel_id = copy;
  node->kernel = k;
  node->next = g_kernel_registry;
  g_kernel_registry = node;
  pthread_mutex_unlock(&g_kernel_registry_mu);

  free(k->kernel_id);
  k->kernel_id = kernel_strdup(kernel_id);
  if (!k->kernel_id) {
    if (err) snprintf(err, EPA_MAX_ERR, "kernel_set_id: OOM");
    return 0;
  }
  return 1;
}

const char* epa_kernel_get_id(const EpaKernel *k) {
  return k ? k->kernel_id : NULL;
}

EpaKernel* epa_kernel_find_by_id(const char *kernel_id) {
  EpaKernel *k = NULL;
  if (!kernel_id || !kernel_id[0]) return NULL;
  pthread_mutex_lock(&g_kernel_registry_mu);
  k = registry_find_kernel_locked(kernel_id);
  pthread_mutex_unlock(&g_kernel_registry_mu);
  return k;
}

int epa_kernel_far_signal_by_id(EpaKernel *sender, uint32_t source_wid, const char *target_kernel_id,
                                const void *payload, uint32_t payload_len, uint32_t payload_tag,
                                char err[EPA_MAX_ERR]) {
  EpaKernel *target;
  uint32_t target_wid;
  if (err) err[0] = 0;
  if (!sender || !target_kernel_id || !target_kernel_id[0] || !payload || payload_len == 0u) {
    if (err) snprintf(err, EPA_MAX_ERR, "far_signal_by_id: bad args");
    return 0;
  }
  if (source_wid >= EPA_MAX_WORKERS || !sender->impl.workers[source_wid].inited) {
    if (err) snprintf(err, EPA_MAX_ERR, "far_signal_by_id: bad source wid %u", (unsigned)source_wid);
    return 0;
  }

  target = epa_kernel_find_by_id(target_kernel_id);
  if (!target) {
    if (err) snprintf(err, EPA_MAX_ERR, "far_signal_by_id: target kernel '%s' not found", target_kernel_id);
    return 0;
  }
  target_wid = kernel_default_ingress_wid(target);
  if (target_wid == 0u) {
    if (err) snprintf(err, EPA_MAX_ERR, "far_signal_by_id: target kernel '%s' has no default ingress worker", target_kernel_id);
    return 0;
  }

  if (!epa_kernel_ingress_push_tagged(target, target_wid, payload_tag, payload, payload_len)) {
    if (err) snprintf(err, EPA_MAX_ERR, "far_signal_by_id: target ingress queue full for '%s'", target_kernel_id);
    return 0;
  }
  epa_kernel_request_interrupt(target);
  return 1;
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
  free(k->owned_blob);
  k->owned_blob = NULL;
  k->owned_blob_len = 0;

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
  k->hooks.on_far_signal   = hook_far_signal;
  k->hooks.on_host_signal  = hook_host_signal;

  k->flow = epa_flow_ctx_make(&k->prog, k->hooks, k);

  // non-flow backend
  extern EpaNonFlowBackend epa_opengl_nonflow_backend(void *impl);
  k->nf = epa_opengl_nonflow_backend(&k->impl);

  k->owned_blob = blob;
  k->owned_blob_len = blob_len;
  epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_LOADED, NULL);
  return 1;
}

int epa_kernel_load_blob(EpaKernel *k, const uint8_t *blob, size_t blob_len, char err[EPA_MAX_ERR]) {
  uint8_t *owned;
  if (!k || !blob || blob_len == 0u) {
    if (err) snprintf(err, EPA_MAX_ERR, "load_blob: bad args");
    return 0;
  }

  owned = (uint8_t*)malloc(blob_len);
  if (!owned) {
    if (err) snprintf(err, EPA_MAX_ERR, "load_blob: OOM");
    return 0;
  }
  memcpy(owned, blob, blob_len);

  if (k->prog_loaded) {
    epa_program_free(&k->prog);
    k->prog_loaded = 0;
  }
  free(k->owned_blob);
  k->owned_blob = NULL;
  k->owned_blob_len = 0;

  if (!epa_program_parse(&k->prog, owned, blob_len, err)) {
    free(owned);
    epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_ERROR, err);
    return 0;
  }
  k->prog_loaded = 1;

  if (!init_workers_from_prog(&k->impl, &k->prog, err)) {
    epa_program_free(&k->prog);
    k->prog_loaded = 0;
    free(owned);
    epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_ERROR, err);
    return 0;
  }

  memset(&k->hooks, 0, sizeof(k->hooks));
  k->hooks.on_entry_exec   = hook_entry_exec;
  k->hooks.on_entry_halt   = hook_entry_halt;
  k->hooks.on_sync         = hook_sync;
  k->hooks.on_wait_on_sync = hook_wait_on_sync;
  k->hooks.get_worker      = hook_get_worker;
  k->hooks.on_break        = hook_break;
  k->hooks.on_trap         = hook_trap;
  k->hooks.on_except       = hook_except;
  k->hooks.on_signal       = hook_signal;
  k->hooks.on_far_signal   = hook_far_signal;
  k->hooks.on_host_signal  = hook_host_signal;
  k->flow = epa_flow_ctx_make(&k->prog, k->hooks, k);
  extern EpaNonFlowBackend epa_opengl_nonflow_backend(void *impl);
  k->nf = epa_opengl_nonflow_backend(&k->impl);

  k->owned_blob = owned;
  k->owned_blob_len = blob_len;
  epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_LOADED, NULL);
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

int epa_kernel_ingress_push_tagged(EpaKernel *k, uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
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
	q->q[q->tail].tag = tag;
	q->tail = (q->tail + 1) % EPA_INGRESS_QMAX;
	q->count++;

	return 1;
}

int epa_kernel_ingress_push(EpaKernel *k, uint32_t wid, const void *data, uint32_t len) {
  return epa_kernel_ingress_push_tagged(k, wid, 0u, data, len);
}

static void ingress_free_msg(EpaIngressMsg *m) {
  free(m->buf);
  m->buf = NULL;
  m->len = 0;
  m->tag = 0u;
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
                                         uint32_t tag,
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
  epa_ghs_err_t ge = epa_ghs_alloc_tagged(ghs, EPA_GHS_T_BYTES, /*owner=*/0, len_bytes, tag, &h);
  if (ge != EPA_GHS_OK) {
    snprintf(err, EPA_MAX_ERR, "ingress: epa_ghs_alloc_tagged failed (%d)", (int)ge);
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
      uint32_t tag = m->tag;

      if (!buf || len == 0) {
        // consume malformed msg so we don't deadlock the queue
        ingress_free_msg(m);
        q->head = (q->head + 1) % EPA_INGRESS_QMAX;
        q->count--;
        continue;
      }

      if (!epa_kernel_deliver_ingress_msg(k, wid, buf, tag, len, err)) {
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
  int rc;
  if (!k || !k->sched_vt) {
    if (err) snprintf(err, EPA_MAX_ERR, "scheduler not set");
    return 0;
  }
  epa_kernel_set_status_only(k, EPA_KERNEL_STATUS_RUNNING);
  rc = k->sched_vt->run(k, &k->sched_state, max_ticks, debug, err);
  if (rc == 1) {
    if (k->impl.workers[0].faulted) {
      epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_FAULTED, err && err[0] ? err : NULL);
    } else {
      epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_HALTED, NULL);
    }
  } else if (rc == 2) {
    epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_STOPPED, NULL);
  } else {
    if (k->impl.workers[0].faulted) {
      epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_FAULTED, err && err[0] ? err : NULL);
    } else {
      epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_ERROR, err && err[0] ? err : NULL);
    }
  }
  return rc;
}

/* Interrupt request */
void epa_kernel_request_interrupt(EpaKernel *k) {
  if (k && k->sched_vt && k->sched_vt->request_interrupt)
    k->sched_vt->request_interrupt(k, &k->sched_state);
}

EpaSchedProfile epa_kernel_get_scheduler(const EpaKernel *k) {
  return k ? k->sched_profile : 0;
}

int epa_kernel_add_threads(EpaKernel *k, uint32_t add_count, char err[EPA_MAX_ERR]) {
  if (err) err[0] = 0;
  if (!k) {
    if (err) snprintf(err, EPA_MAX_ERR, "kernel_add_threads: kernel null");
    return 0;
  }
  if (k->sched_profile != EPA_SCHED_CPU_THREAD) {
    if (err) snprintf(err, EPA_MAX_ERR, "kernel_add_threads: scheduler is not cpu_thread");
    return 0;
  }
  return epa_sched_cpu_thread_add_threads(k, &k->sched_state, add_count, err);
}

uint32_t epa_kernel_thread_count(const EpaKernel *k) {
  if (!k || k->sched_profile != EPA_SCHED_CPU_THREAD) return 0u;
  return epa_sched_cpu_thread_thread_count((EpaSchedState*)&k->sched_state);
}

static uint32_t read_u32_le(const uint8_t *p) {
  return ((uint32_t)p[0]) |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void module_free_entries(EpaKernelModule *module) {
  size_t i;
  if (!module) return;
  for (i = 0; i < module->count; i++) {
    if (module->entries[i].thread_started) {
      module->entries[i].stop_requested = 1;
      if (module->entries[i].kernel) epa_kernel_request_interrupt(module->entries[i].kernel);
      pthread_join(module->entries[i].thread, NULL);
      module->entries[i].thread_started = 0;
    }
    if (module->entries[i].kernel) epa_kernel_destroy(module->entries[i].kernel);
    free(module->entries[i].path_id);
    free(module->entries[i].blob);
  }
  free(module->entries);
  module->entries = NULL;
  module->count = 0;
}

static void* module_kernel_thread_main(void *arg) {
  EpaKernelModuleEntry *entry = (EpaKernelModuleEntry*)arg;
  char err[EPA_MAX_ERR];
  for (;;) {
    int rc;
    if (entry->stop_requested) break;
    err[0] = 0;
    rc = epa_kernel_run(entry->kernel, 0u, 0, err);
    if (rc == 2) {
      if (entry->stop_requested) break;
      continue;
    }
    if (rc == 1) {
      break;
    }
    snprintf(entry->last_error, sizeof(entry->last_error), "%s", err[0] ? err : "kernel run failed");
    break;
  }
  entry->thread_started = 0;
  return NULL;
}

EpaKernelModule* epa_kernel_module_load_bundle(const char *bundle_path, char err[EPA_MAX_ERR]) {
  FILE *f;
  long file_len_long;
  size_t file_len;
  uint8_t *buf = NULL;
  EpaKernelModule *module = NULL;
  uint32_t version;
  uint32_t count;
  uint32_t i;

  if (err) err[0] = 0;
  if (!bundle_path || !bundle_path[0]) {
    if (err) snprintf(err, EPA_MAX_ERR, "bundle load: missing path");
    return NULL;
  }

  f = fopen(bundle_path, "rb");
  if (!f) {
    if (err) snprintf(err, EPA_MAX_ERR, "bundle load: cannot open %s", bundle_path);
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); if (err) snprintf(err, EPA_MAX_ERR, "bundle load: seek failed"); return NULL; }
  file_len_long = ftell(f);
  if (file_len_long < 0) { fclose(f); if (err) snprintf(err, EPA_MAX_ERR, "bundle load: ftell failed"); return NULL; }
  file_len = (size_t)file_len_long;
  if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); if (err) snprintf(err, EPA_MAX_ERR, "bundle load: seek reset failed"); return NULL; }
  buf = (uint8_t*)malloc(file_len ? file_len : 1u);
  if (!buf) { fclose(f); if (err) snprintf(err, EPA_MAX_ERR, "bundle load: OOM"); return NULL; }
  if (file_len && fread(buf, 1, file_len, f) != file_len) { fclose(f); free(buf); if (err) snprintf(err, EPA_MAX_ERR, "bundle load: read failed"); return NULL; }
  fclose(f);

  if (file_len < 16u || memcmp(buf, EPA_BUNDLE_MAGIC, 8u) != 0) {
    free(buf);
    if (err) snprintf(err, EPA_MAX_ERR, "bundle load: invalid magic");
    return NULL;
  }
  version = read_u32_le(buf + 8u);
  count = read_u32_le(buf + 12u);
  if (version != EPA_BUNDLE_VERSION) {
    free(buf);
    if (err) snprintf(err, EPA_MAX_ERR, "bundle load: unsupported version %u", (unsigned)version);
    return NULL;
  }
  if (file_len < 16u + (size_t)count * 24u) {
    free(buf);
    if (err) snprintf(err, EPA_MAX_ERR, "bundle load: truncated index");
    return NULL;
  }

  module = (EpaKernelModule*)calloc(1, sizeof(*module));
  if (!module) {
    free(buf);
    if (err) snprintf(err, EPA_MAX_ERR, "bundle load: OOM");
    return NULL;
  }
  module->count = (size_t)count;
  module->entries = (EpaKernelModuleEntry*)calloc(module->count, sizeof(EpaKernelModuleEntry));
  if (!module->entries) {
    free(buf);
    free(module);
    if (err) snprintf(err, EPA_MAX_ERR, "bundle load: OOM");
    return NULL;
  }

  for (i = 0; i < count; i++) {
    const uint8_t *ent = buf + 16u + (size_t)i * 24u;
    uint32_t path_off = read_u32_le(ent + 0u);
    uint32_t path_len = read_u32_le(ent + 4u);
    uint32_t blob_off = read_u32_le(ent + 8u);
    uint32_t blob_len = read_u32_le(ent + 12u);
    uint32_t flags = read_u32_le(ent + 16u);
    EpaKernelModuleEntry *dst = &module->entries[i];
    char *path_id;
    EpaKernel *kernel;

    if ((size_t)path_off + (size_t)path_len > file_len || (size_t)blob_off + (size_t)blob_len > file_len) {
      if (err) snprintf(err, EPA_MAX_ERR, "bundle load: entry %u out of range", (unsigned)i);
      module_free_entries(module);
      free(module);
      free(buf);
      return NULL;
    }

    path_id = (char*)malloc((size_t)path_len + 1u);
    if (!path_id) {
      if (err) snprintf(err, EPA_MAX_ERR, "bundle load: OOM");
      module_free_entries(module);
      free(module);
      free(buf);
      return NULL;
    }
    memcpy(path_id, buf + path_off, path_len);
    path_id[path_len] = 0;

    dst->blob = (uint8_t*)malloc(blob_len ? (size_t)blob_len : 1u);
    if (!dst->blob) {
      free(path_id);
      if (err) snprintf(err, EPA_MAX_ERR, "bundle load: OOM");
      module_free_entries(module);
      free(module);
      free(buf);
      return NULL;
    }
    if (blob_len) memcpy(dst->blob, buf + blob_off, blob_len);
    dst->blob_len = blob_len;
    dst->path_id = path_id;
    dst->flags = flags;

    kernel = epa_kernel_create(err);
    if (!kernel) {
      module_free_entries(module);
      free(module);
      free(buf);
      return NULL;
    }
    if (!epa_kernel_set_id(kernel, path_id, err)) {
      epa_kernel_destroy(kernel);
      module_free_entries(module);
      free(module);
      free(buf);
      return NULL;
    }
    if (!epa_kernel_load_blob(kernel, dst->blob, dst->blob_len, err)) {
      epa_kernel_destroy(kernel);
      module_free_entries(module);
      free(module);
      free(buf);
      return NULL;
    }
    if (!epa_kernel_set_scheduler(kernel, EPA_SCHED_CPU_THREAD, err)) {
      epa_kernel_destroy(kernel);
      module_free_entries(module);
      free(module);
      free(buf);
      return NULL;
    }
    dst->kernel = kernel;
    dst->last_error[0] = 0;
  }

  free(buf);
  return module;
}

void epa_kernel_module_destroy(EpaKernelModule *module) {
  if (!module) return;
  module_free_entries(module);
  free(module);
}

size_t epa_kernel_module_count(const EpaKernelModule *module) {
  return module ? module->count : 0u;
}

const char* epa_kernel_module_path_id(const EpaKernelModule *module, size_t index) {
  if (!module || index >= module->count) return NULL;
  return module->entries[index].path_id;
}

uint32_t epa_kernel_module_flags(const EpaKernelModule *module, size_t index) {
  if (!module || index >= module->count) return 0u;
  return module->entries[index].flags;
}

EpaKernel* epa_kernel_module_kernel(const EpaKernelModule *module, size_t index) {
  if (!module || index >= module->count) return NULL;
  return module->entries[index].kernel;
}

int epa_kernel_module_find_kernel(const EpaKernelModule *module, const char *path_id) {
  size_t i;
  if (!module || !path_id) return -1;
  for (i = 0; i < module->count; i++) {
    if (module->entries[i].path_id && strcmp(module->entries[i].path_id, path_id) == 0) {
      return (int)i;
    }
  }
  return -1;
}

EpaKernelStatus epa_kernel_module_kernel_status(const EpaKernelModule *module, size_t index) {
  if (!module || index >= module->count || !module->entries[index].kernel) return EPA_KERNEL_STATUS_ERROR;
  return epa_kernel_get_status(module->entries[index].kernel);
}

const char* epa_kernel_module_kernel_error(const EpaKernelModule *module, size_t index) {
  if (!module || index >= module->count || !module->entries[index].kernel) return NULL;
  if (module->entries[index].last_error[0]) return module->entries[index].last_error;
  return epa_kernel_get_last_error(module->entries[index].kernel);
}

int epa_kernel_module_start_kernel(EpaKernelModule *module, size_t index, char err[EPA_MAX_ERR]) {
  EpaKernelModuleEntry *entry;
  if (err) err[0] = 0;
  if (!module || index >= module->count) {
    if (err) snprintf(err, EPA_MAX_ERR, "start_kernel: bad index");
    return 0;
  }
  entry = &module->entries[index];
  if (!entry->kernel) {
    if (err) snprintf(err, EPA_MAX_ERR, "start_kernel: kernel missing");
    return 0;
  }
  if (entry->thread_started) {
    if (err) snprintf(err, EPA_MAX_ERR, "start_kernel: kernel already running");
    return 0;
  }
  if (epa_kernel_get_status(entry->kernel) == EPA_KERNEL_STATUS_HALTED ||
      epa_kernel_get_status(entry->kernel) == EPA_KERNEL_STATUS_FAULTED ||
      epa_kernel_get_status(entry->kernel) == EPA_KERNEL_STATUS_ERROR) {
    if (err) snprintf(err, EPA_MAX_ERR, "start_kernel: kernel '%s' is not restartable in status %s",
                      entry->path_id ? entry->path_id : "(unnamed)",
                      epa_kernel_status_name(epa_kernel_get_status(entry->kernel)));
    return 0;
  }
  entry->stop_requested = 0;
  entry->last_error[0] = 0;
  if (pthread_create(&entry->thread, NULL, module_kernel_thread_main, entry) != 0) {
    if (err) snprintf(err, EPA_MAX_ERR, "start_kernel: pthread_create failed");
    return 0;
  }
  entry->thread_started = 1;
  return 1;
}

int epa_kernel_module_stop_kernel(EpaKernelModule *module, size_t index, char err[EPA_MAX_ERR]) {
  EpaKernelModuleEntry *entry;
  if (err) err[0] = 0;
  if (!module || index >= module->count) {
    if (err) snprintf(err, EPA_MAX_ERR, "stop_kernel: bad index");
    return 0;
  }
  entry = &module->entries[index];
  if (!entry->thread_started) return 1;
  entry->stop_requested = 1;
  if (entry->kernel) epa_kernel_request_interrupt(entry->kernel);
  pthread_join(entry->thread, NULL);
  entry->thread_started = 0;
  if (entry->kernel && epa_kernel_get_status(entry->kernel) == EPA_KERNEL_STATUS_RUNNING) {
    epa_kernel_set_status_text(entry->kernel, EPA_KERNEL_STATUS_STOPPED, NULL);
  }
  return 1;
}

int epa_kernel_module_add_kernel_threads(EpaKernelModule *module, size_t index, uint32_t add_count, char err[EPA_MAX_ERR]) {
  EpaKernelModuleEntry *entry;
  if (err) err[0] = 0;
  if (!module || index >= module->count) {
    if (err) snprintf(err, EPA_MAX_ERR, "add_kernel_threads: bad index");
    return 0;
  }
  entry = &module->entries[index];
  if (!entry->kernel) {
    if (err) snprintf(err, EPA_MAX_ERR, "add_kernel_threads: kernel missing");
    return 0;
  }
  return epa_kernel_add_threads(entry->kernel, add_count, err);
}

uint32_t epa_kernel_module_kernel_thread_count(const EpaKernelModule *module, size_t index) {
  if (!module || index >= module->count || !module->entries[index].kernel) return 0u;
  return epa_kernel_thread_count(module->entries[index].kernel);
}

int epa_kernel_module_start_all_kernels(EpaKernelModule *module, char err[EPA_MAX_ERR]) {
  size_t i;
  if (err) err[0] = 0;
  if (!module) {
    if (err) snprintf(err, EPA_MAX_ERR, "start_all_kernels: module null");
    return 0;
  }
  for (i = 0; i < module->count; i++) {
    if (!(module->entries[i].flags & EPA_BUNDLE_FLAG_STARTED)) continue;
    if (!epa_kernel_module_start_kernel(module, i, err)) return 0;
  }
  return 1;
}

int epa_kernel_module_stop_all_kernels(EpaKernelModule *module, char err[EPA_MAX_ERR]) {
  size_t i;
  if (err) err[0] = 0;
  if (!module) {
    if (err) snprintf(err, EPA_MAX_ERR, "stop_all_kernels: module null");
    return 0;
  }
  for (i = 0; i < module->count; i++) {
    if (!epa_kernel_module_stop_kernel(module, i, err)) return 0;
  }
  return 1;
}
