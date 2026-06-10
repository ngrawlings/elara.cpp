// epa_kernel_so.c
#include "epa_kernel_so.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

#include "epa_asm_compiler.h"
#include "log.h"
#include "epa_program_loader.h"
#include "epa_flow_glue.h"
#include "epa_backend_nonflow.h"
#include "epa_instruct_common.h"

#include "memory/epa_ring_buffer.h"
#include "vm/epa_worker_state.h"

#include "epa_kernel_hooks.h"

static EpaNonFlowRc epa_null_nf_exec_one(void *impl, const EpaProgramDesc *prog,
                                          EpaWorkerState *w, EpaEip *eip,
                                          char err[EPA_MAX_ERR]) {
  (void)impl; (void)prog; (void)w; (void)eip; (void)err;
  return EPA_NF_EXEC_NOT_MINE;
}
static const EpaNonFlowBackendVTable epa_null_nf_vt = { epa_null_nf_exec_one };
static const EpaNonFlowBackend epa_null_nf_backend = { &epa_null_nf_vt, NULL };
static void kernel_install_standard_hooks(EpaKernel *k);
static int epa_kernel_ingress_push_framed(EpaKernel *k, uint32_t wid, uint32_t tag,
                                          const void *data, uint32_t len,
                                          uint64_t source_kernel_uid,
                                          uint32_t source_worker_id);

#define EPA_BUNDLE_MAGIC "EPABNDL1"
#define EPA_BUNDLE_VERSION 1u
#define EPA_BUNDLE_FLAG_ROOT    0x00000001u
#define EPA_BUNDLE_FLAG_STARTED 0x00000002u
#define EPA_PRIVILEGE_ACL_ADMIN 100u

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
  uint64_t kernel_uid;
  EpaKernel *kernel;
  struct KernelRegistryNode *next;
} KernelRegistryNode;

typedef struct EpaProcessRegistryNode {
  uint32_t pid;
  EpaKernel **kernels;
  size_t count;
  size_t cap;
  struct EpaProcessRegistryNode *next;
} EpaProcessRegistryNode;

static pthread_mutex_t g_kernel_registry_mu = PTHREAD_MUTEX_INITIALIZER;
static KernelRegistryNode *g_kernel_registry = NULL;
static EpaProcessRegistryNode *g_process_registry = NULL;

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

static void kernel_atq_clear(EpaKernel *k) {
  if (!k) return;
  for (uint32_t i = 0; i < EPA_SYSTEM_AT_QMAX; i++) {
    free(k->impl.atq.q[i].descriptor_words);
    k->impl.atq.q[i].descriptor_words = NULL;
    k->impl.atq.q[i].descriptor_word_count = 0;
    k->impl.atq.q[i].request_id = 0;
    k->impl.atq.q[i].wid = 0;
    k->impl.atq.q[i].at_entry_index = 0;
  }
  k->impl.atq.head = 0;
  k->impl.atq.tail = 0;
  k->impl.atq.count = 0;
  if (k->impl.atq.next_request_id == 0u) k->impl.atq.next_request_id = 1u;
}

static void kernel_memq_clear(EpaKernel *k) {
  if (!k) return;
  memset(&k->impl.memq, 0, sizeof(k->impl.memq));
  k->impl.memq.next_request_id = 1u;
}

static void kernel_fault_worker(EpaKernel *k, uint32_t wid, const char *fmt, ...) {
  va_list ap;
  if (!k || wid >= EPA_MAX_WORKERS || !k->impl.workers[wid].inited) return;
  k->impl.workers[wid].faulted = 1;
  va_start(ap, fmt);
  vsnprintf(k->impl.workers[wid].fault_message, sizeof(k->impl.workers[wid].fault_message), fmt, ap);
  va_end(ap);
}

int epa_kernel_store_last_host_signal(EpaKernel *k, uint32_t wid, const uint8_t *bytes, uint32_t len, char err[EPA_MAX_ERR]) {
  uint8_t *next = NULL;
  if (!k) return 0;
  if (len > 0u) {
    if (k->last_host_signal_cap < len) {
      next = (uint8_t*)realloc(k->last_host_signal_bytes, len);
      if (!next) {
        if (err) snprintf(err, EPA_MAX_ERR, "OOM expanding last host signal buffer");
        return 0;
      }
      k->last_host_signal_bytes = next;
      k->last_host_signal_cap = len;
    }
    memcpy(k->last_host_signal_bytes, bytes, len);
  }
  k->last_host_signal_len = len;
  k->last_host_signal_wid = wid;
  return 1;
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

static EpaKernel *registry_find_kernel_by_uid_locked(uint64_t kernel_uid) {
  KernelRegistryNode *node = g_kernel_registry;
  while (node) {
    if (node->kernel_uid == kernel_uid) return node->kernel;
    node = node->next;
  }
  return NULL;
}

static int registry_uid_exists_locked(uint64_t kernel_uid, const EpaKernel *except_kernel) {
  KernelRegistryNode *node = g_kernel_registry;
  while (node) {
    if (node->kernel_uid == kernel_uid && node->kernel != except_kernel) return 1;
    node = node->next;
  }
  return 0;
}

static uint64_t fnv1a64_bytes(const char *s) {
  uint64_t h = 14695981039346656037ull;
  while (s && *s) {
    h ^= (uint8_t)*s++;
    h *= 1099511628211ull;
  }
  return h;
}

static uint64_t fnv1a64_u32(uint64_t h, uint32_t v) {
  for (uint32_t i = 0; i < 4u; i++) {
    h ^= (uint8_t)((v >> (i * 8u)) & 0xFFu);
    h *= 1099511628211ull;
  }
  return h;
}

static uint64_t fnv1a64_u64(uint64_t h, uint64_t v) {
  for (uint32_t i = 0; i < 8u; i++) {
    h ^= (uint8_t)((v >> (i * 8u)) & 0xFFu);
    h *= 1099511628211ull;
  }
  return h;
}

uint64_t epa_kernel_namespace_uid(uint32_t pid, uint64_t local_uid) {
  uint64_t h = 14695981039346656037ull;
  h = fnv1a64_u32(h, pid);
  h = fnv1a64_u64(h, local_uid);
  return h ? h : 1u;
}

uint32_t epa_kernel_get_pid(const EpaKernel *k) {
  return k ? k->pid : 0u;
}

uint64_t epa_kernel_local_uid(const EpaKernel *k) {
  return k ? k->local_kernel_uid : 0u;
}

uint64_t epa_kernel_resolve_uid_for_sender(const EpaKernel *sender, uint64_t local_or_global_uid) {
  uint64_t namespaced;
  if (!sender || sender->pid == 0u || local_or_global_uid == 0u) return local_or_global_uid;
  namespaced = epa_kernel_namespace_uid(sender->pid, local_or_global_uid);
  pthread_mutex_lock(&g_kernel_registry_mu);
  if (registry_find_kernel_by_uid_locked(namespaced)) {
    pthread_mutex_unlock(&g_kernel_registry_mu);
    return namespaced;
  }
  pthread_mutex_unlock(&g_kernel_registry_mu);
  return local_or_global_uid;
}

static EpaProcessRegistryNode *process_find_locked(uint32_t pid) {
  EpaProcessRegistryNode *node = g_process_registry;
  while (node) {
    if (node->pid == pid) return node;
    node = node->next;
  }
  return NULL;
}

static int process_add_kernel_locked(uint32_t pid, EpaKernel *kernel, char err[EPA_MAX_ERR]) {
  EpaProcessRegistryNode *node;
  EpaKernel **next;
  if (pid == 0u || !kernel) {
    if (err) snprintf(err, EPA_MAX_ERR, "process add: bad args");
    return 0;
  }
  node = process_find_locked(pid);
  if (!node) {
    node = (EpaProcessRegistryNode*)calloc(1, sizeof(*node));
    if (!node) {
      if (err) snprintf(err, EPA_MAX_ERR, "process add: OOM");
      return 0;
    }
    node->pid = pid;
    node->next = g_process_registry;
    g_process_registry = node;
  }
  if (node->count == node->cap) {
    size_t next_cap = node->cap ? node->cap * 2u : 4u;
    next = (EpaKernel**)realloc(node->kernels, sizeof(*next) * next_cap);
    if (!next) {
      if (err) snprintf(err, EPA_MAX_ERR, "process add: OOM");
      return 0;
    }
    node->kernels = next;
    node->cap = next_cap;
  }
  node->kernels[node->count++] = kernel;
  return 1;
}

static void process_remove_kernel_locked(EpaKernel *kernel) {
  EpaProcessRegistryNode **pp = &g_process_registry;
  while (*pp) {
    EpaProcessRegistryNode *node = *pp;
    size_t write = 0u;
    for (size_t read = 0u; read < node->count; read++) {
      if (node->kernels[read] == kernel) continue;
      node->kernels[write++] = node->kernels[read];
    }
    node->count = write;
    if (node->count == 0u) {
      *pp = node->next;
      free(node->kernels);
      free(node);
      continue;
    }
    pp = &node->next;
  }
}

static int epa_kernel_register_identity(EpaKernel *k, const char *kernel_id, uint64_t kernel_uid, char err[EPA_MAX_ERR]);

static int epa_kernel_register_uid_only(EpaKernel *k, uint64_t kernel_uid, char err[EPA_MAX_ERR]) {
  char synthetic_id[64];
  snprintf(synthetic_id, sizeof(synthetic_id), "uid.%016llx", (unsigned long long)kernel_uid);
  return epa_kernel_register_identity(k, synthetic_id, kernel_uid, err);
}

static uint32_t kernel_default_ingress_wid(const EpaKernel *k) {
  uint32_t wid;
  if (!k || !k->prog_loaded) return 0u;
  for (wid = 1u; wid < EPA_MAX_WORKERS; wid++) {
    if (k->prog.entry_present[wid]) return wid;
  }
  return 0u;
}

static int kernel_acl_route_matches(uint64_t route_remote_uid, uint32_t route_wid,
                                    uint64_t remote_uid, uint32_t local_wid) {
  if (route_wid != local_wid) return 0;
  if (route_remote_uid != 0u && route_remote_uid != remote_uid) return 0;
  return 1;
}

static int kernel_acl_has_entries(const EpaKernel *target) {
  return target && (target->prog.acl_count > 0u || target->dynamic_acl_count > 0u);
}

static int kernel_acl_allows(const EpaKernel *target, uint64_t remote_uid, uint32_t local_wid) {
  size_t i;
  if (!target) return 0;
  if (!kernel_acl_has_entries(target)) return 1;
  for (i = 0; i < target->prog.acl_count; i++) {
    const EpaProgramAclEntry *acl = &target->prog.acl_entries[i];
    if (kernel_acl_route_matches(acl->remote_kernel_uid, acl->local_wid, remote_uid, local_wid)) return 1;
  }
  for (i = 0; i < target->dynamic_acl_count; i++) {
    const EpaDynamicAclEntry *acl = &target->dynamic_acl_entries[i];
    if (kernel_acl_route_matches(acl->remote_kernel_uid, acl->local_wid, remote_uid, local_wid)) return 1;
  }
  return 0;
}

static uint32_t kernel_acl_first_route_for_remote(const EpaKernel *target, uint64_t remote_uid) {
  size_t i;
  if (!target) return 0u;
  for (i = 0; i < target->prog.acl_count; i++) {
    const EpaProgramAclEntry *acl = &target->prog.acl_entries[i];
    if (acl->remote_kernel_uid == 0u || acl->remote_kernel_uid == remote_uid) return acl->local_wid;
  }
  for (i = 0; i < target->dynamic_acl_count; i++) {
    const EpaDynamicAclEntry *acl = &target->dynamic_acl_entries[i];
    if (acl->remote_kernel_uid == 0u || acl->remote_kernel_uid == remote_uid) return acl->local_wid;
  }
  return 0u;
}

static void kernel_dynamic_acl_clear(EpaKernel *k) {
  if (!k) return;
  free(k->dynamic_acl_entries);
  k->dynamic_acl_entries = NULL;
  k->dynamic_acl_count = 0u;
  k->dynamic_acl_cap = 0u;
}

static void registry_remove_dynamic_routes_for_remote_locked(uint64_t remote_uid) {
  KernelRegistryNode *node = g_kernel_registry;
  while (node) {
    EpaKernel *target = node->kernel;
    if (target && target->dynamic_acl_count > 0u) {
      size_t write = 0u;
      size_t read;
      for (read = 0u; read < target->dynamic_acl_count; read++) {
        if (target->dynamic_acl_entries[read].remote_kernel_uid == remote_uid) continue;
        target->dynamic_acl_entries[write++] = target->dynamic_acl_entries[read];
      }
      target->dynamic_acl_count = write;
    }
    node = node->next;
  }
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

static void kernel_refresh_worker_vm_identity(EpaKernel *k) {
  if (!k) return;
  for (uint32_t wid = 0; wid < EPA_MAX_WORKERS; wid++) {
    EpaWorkerState *w = &k->impl.workers[wid];
    if (!w->inited) continue;
    w->current_kernel_uid = k->kernel_uid;
    w->current_worker_id = wid;
  }
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
  pthread_mutex_init(&k->impl.atq_mu, NULL);
  pthread_mutex_init(&k->impl.memq_mu, NULL);
  pthread_mutex_init(&k->state_mu, NULL);
  k->impl.atq.next_request_id = 1u;
  k->impl.memq.next_request_id = 1u;
  k->runtime_status = EPA_KERNEL_STATUS_UNLOADED;
  k->last_error[0] = 0;

  k->impl.ghs = epa_ghs_create(65536, NULL, NULL, NULL);
  if (!k->impl.ghs) {
     TRACE("GHS init failed");
     return 0; // or your init fail path
  }
  k->impl.rgm = epa_rgm_global();
  if (!k->impl.rgm) {
     TRACE("RGM init failed");
     epa_ghs_destroy(k->impl.ghs);
     k->impl.ghs = NULL;
     return 0;
  }

  epa_kernel_set_scheduler(k, EPA_SCHED_WAVE, err);

  return k;
}

void epa_kernel_destroy(EpaKernel *k) {
  if (!k) return;

  pthread_mutex_lock(&g_kernel_registry_mu);
  registry_remove_kernel_locked(k);
  process_remove_kernel_locked(k);
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
  k->impl.rgm = NULL;

  epa_ring_free(&k->impl.syncq);
  pthread_mutex_lock(&k->impl.atq_mu);
  kernel_atq_clear(k);
  pthread_mutex_unlock(&k->impl.atq_mu);
  pthread_mutex_lock(&k->impl.memq_mu);
  kernel_memq_clear(k);
  pthread_mutex_unlock(&k->impl.memq_mu);
  pthread_mutex_destroy(&k->impl.syncq_mu);
  pthread_mutex_destroy(&k->impl.atq_mu);
  pthread_mutex_destroy(&k->impl.memq_mu);
  pthread_mutex_destroy(&k->state_mu);

  if (k->prog_loaded) epa_program_free(&k->prog);
  free(k->owned_blob);
  k->owned_blob = NULL;
  k->owned_blob_len = 0;
  free(k->last_host_signal_bytes);
  k->last_host_signal_bytes = NULL;
  k->last_host_signal_len = 0;
  k->last_host_signal_cap = 0;
  k->last_host_signal_wid = 0;

  free(k->kernel_id);
  k->kernel_id = NULL;
  kernel_dynamic_acl_clear(k);

  free(k);
}

static int epa_kernel_register_identity(EpaKernel *k, const char *kernel_id, uint64_t kernel_uid, char err[EPA_MAX_ERR]) {
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
  if (registry_uid_exists_locked(kernel_uid, k)) {
    pthread_mutex_unlock(&g_kernel_registry_mu);
    free(copy);
    if (err) snprintf(err, EPA_MAX_ERR, "kernel_set_id: duplicate 64-bit id 0x%016llx for '%s'",
                      (unsigned long long)kernel_uid, kernel_id);
    return 0;
  }
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
  node->kernel_uid = kernel_uid;
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
  k->kernel_uid = kernel_uid;
  kernel_refresh_worker_vm_identity(k);
  return 1;
}

int epa_kernel_set_id(EpaKernel *k, const char *kernel_id, char err[EPA_MAX_ERR]) {
  return epa_kernel_register_identity(k, kernel_id, fnv1a64_bytes(kernel_id), err);
}

const char* epa_kernel_get_id(const EpaKernel *k) {
  return k ? k->kernel_id : NULL;
}

void epa_kernel_set_signal_callback(EpaKernel *k, EpaKernelSignal cb) {
  if (!k) return;
  k->signal_cb = cb;
}

EpaKernel* epa_kernel_find_by_id(const char *kernel_id) {
  EpaKernel *k = NULL;
  if (!kernel_id || !kernel_id[0]) return NULL;
  pthread_mutex_lock(&g_kernel_registry_mu);
  k = registry_find_kernel_locked(kernel_id);
  pthread_mutex_unlock(&g_kernel_registry_mu);
  return k;
}

int epa_kernel_far_signal_by_uid(EpaKernel *sender, uint32_t source_wid, uint64_t target_kernel_uid,
                                 uint32_t target_wid_hint,
                                 const void *payload, uint32_t payload_len, uint32_t payload_tag,
                                 char err[EPA_MAX_ERR]) {
  EpaKernel *target;
  uint32_t target_wid;
  if (err) err[0] = 0;
  if (!sender || target_kernel_uid == 0u || !payload || payload_len == 0u) {
    if (err) snprintf(err, EPA_MAX_ERR, "far_signal_by_uid: bad args");
    return 0;
  }
  if (source_wid >= EPA_MAX_WORKERS || !sender->impl.workers[source_wid].inited) {
    if (err) snprintf(err, EPA_MAX_ERR, "far_signal_by_uid: bad source wid %u", (unsigned)source_wid);
    return 0;
  }

  pthread_mutex_lock(&g_kernel_registry_mu);
  target = registry_find_kernel_by_uid_locked(target_kernel_uid);
  pthread_mutex_unlock(&g_kernel_registry_mu);
  if (!target) {
    if (err) snprintf(err, EPA_MAX_ERR, "far_signal_by_uid: target kernel 0x%016llx not found",
                      (unsigned long long)target_kernel_uid);
    return 0;
  }

  if (target_wid_hint != 0u) {
    target_wid = target_wid_hint;
    if (!kernel_acl_allows(target, sender->kernel_uid, target_wid)) {
      EpaWorkerState *sw = &sender->impl.workers[source_wid];
      sw->faulted = 1;
      snprintf(sw->fault_message, sizeof(sw->fault_message),
               "ACL FAULT: kernel '%s' cannot route to target kernel 0x%016llx wid=%u",
               sender->kernel_id ? sender->kernel_id : "(unnamed)",
               (unsigned long long)target_kernel_uid,
               (unsigned)target_wid);
      if (err) snprintf(err, EPA_MAX_ERR, "%s", sw->fault_message);
      return 0;
    }
  } else if (kernel_acl_has_entries(target)) {
    target_wid = kernel_acl_first_route_for_remote(target, sender->kernel_uid);
    if (target_wid == 0u) {
      EpaWorkerState *sw = &sender->impl.workers[source_wid];
      sw->faulted = 1;
      snprintf(sw->fault_message, sizeof(sw->fault_message),
               "ACL FAULT: kernel '%s' is not whitelisted by target kernel 0x%016llx",
               sender->kernel_id ? sender->kernel_id : "(unnamed)",
               (unsigned long long)target_kernel_uid);
      if (err) snprintf(err, EPA_MAX_ERR, "%s", sw->fault_message);
      return 0;
    }
  } else {
    target_wid = kernel_default_ingress_wid(target);
  }
  if (target_wid == 0u) {
    if (err) snprintf(err, EPA_MAX_ERR, "far_signal_by_uid: target kernel 0x%016llx has no default ingress worker",
                      (unsigned long long)target_kernel_uid);
    return 0;
  }

  if (!epa_kernel_ingress_push_framed(target, target_wid, payload_tag, payload, payload_len,
                                      sender->kernel_uid, source_wid)) {
    EpaWorkerState *sw = &sender->impl.workers[source_wid];
    sw->faulted = 1;
    snprintf(sw->fault_message, sizeof(sw->fault_message),
             "INGRESS FAULT: target ingress queue full for kernel 0x%016llx wid=%u",
             (unsigned long long)target_kernel_uid,
             (unsigned)target_wid);
    if (err) snprintf(err, EPA_MAX_ERR, "far_signal_by_uid: target ingress queue full for 0x%016llx",
                      (unsigned long long)target_kernel_uid);
    return 0;
  }
  epa_kernel_request_interrupt(target);
  return 1;
}

int epa_kernel_retire_by_uid(uint64_t kernel_uid, char err[EPA_MAX_ERR]) {
  EpaKernel *target;
  if (err) err[0] = 0;
  if (kernel_uid == 0u) {
    if (err) snprintf(err, EPA_MAX_ERR, "retire_kernel: bad kernel uid");
    return 0;
  }

  pthread_mutex_lock(&g_kernel_registry_mu);
  target = registry_find_kernel_by_uid_locked(kernel_uid);
  if (target) {
    registry_remove_kernel_locked(target);
    process_remove_kernel_locked(target);
  }
  registry_remove_dynamic_routes_for_remote_locked(kernel_uid);
  pthread_mutex_unlock(&g_kernel_registry_mu);

  if (!target) {
    if (err) snprintf(err, EPA_MAX_ERR, "retire_kernel: target kernel 0x%016llx not found",
                      (unsigned long long)kernel_uid);
    return 0;
  }

  for (uint32_t wid = 0; wid < EPA_MAX_WORKERS; wid++) {
    EpaWorkerState *w = &target->impl.workers[wid];
    if (!w->inited) continue;
    w->retired = 1;
    w->halted = 1;
    w->blocked = 1;
    epa_ring_clear(&w->inq);
    epa_ring_clear(&w->outq);
  }
  target->impl.worker_head = EPA_MAX_WORKERS;
  target->impl.n_workers = 0u;
  for (uint32_t wid = 0; wid < EPA_MAX_WORKERS; wid++) {
    target->impl.worker_next[wid] = EPA_MAX_WORKERS;
  }
  memset(&target->ingress, 0, sizeof(target->ingress));
  kernel_dynamic_acl_clear(target);
  kernel_atq_clear(target);
  kernel_memq_clear(target);
  epa_kernel_set_status_text(target, EPA_KERNEL_STATUS_UNLOADED, NULL);
  epa_kernel_request_interrupt(target);
  return 1;
}

int epa_kernel_retire_by_id(const char *kernel_id, char err[EPA_MAX_ERR]) {
  if (!kernel_id || !kernel_id[0]) {
    if (err) snprintf(err, EPA_MAX_ERR, "retire_kernel: bad kernel id");
    return 0;
  }
  return epa_kernel_retire_by_uid(fnv1a64_bytes(kernel_id), err);
}

static int kernel_actor_has_privilege(EpaKernel *actor, uint32_t source_wid, uint32_t min_privilege, const char *op, char err[EPA_MAX_ERR]) {
  if (!actor || source_wid >= EPA_MAX_WORKERS || !actor->impl.workers[source_wid].inited) {
    if (err) snprintf(err, EPA_MAX_ERR, "%s: bad actor worker %u", op ? op : "privileged op", (unsigned)source_wid);
    return 0;
  }
  if (actor->impl.workers[source_wid].privilege < min_privilege) {
    if (err) snprintf(err, EPA_MAX_ERR, "%s: worker %u privilege %u below threshold %u",
                      op ? op : "privileged op",
                      (unsigned)source_wid,
                      (unsigned)actor->impl.workers[source_wid].privilege,
                      (unsigned)min_privilege);
    return 0;
  }
  return 1;
}

static int kernel_actor_has_acl_admin(EpaKernel *actor, uint32_t source_wid, char err[EPA_MAX_ERR]) {
  return kernel_actor_has_privilege(actor, source_wid, EPA_PRIVILEGE_ACL_ADMIN, "dynamic ACL", err);
}

int epa_kernel_acl_grant_by_uid(EpaKernel *actor, uint32_t source_wid,
                                uint64_t target_kernel_uid, uint64_t remote_kernel_uid,
                                uint32_t local_wid, char err[EPA_MAX_ERR]) {
  EpaKernel *target;
  EpaDynamicAclEntry *next;
  size_t i;
  if (err) err[0] = 0;
  if (!kernel_actor_has_acl_admin(actor, source_wid, err)) return 0;
  if (target_kernel_uid == 0u || remote_kernel_uid == 0u || local_wid >= EPA_MAX_WORKERS) {
    if (err) snprintf(err, EPA_MAX_ERR, "dynamic ACL grant: bad args");
    return 0;
  }

  pthread_mutex_lock(&g_kernel_registry_mu);
  target = registry_find_kernel_by_uid_locked(target_kernel_uid);
  if (!target) {
    pthread_mutex_unlock(&g_kernel_registry_mu);
    if (err) snprintf(err, EPA_MAX_ERR, "dynamic ACL grant: target kernel 0x%016llx not found",
                      (unsigned long long)target_kernel_uid);
    return 0;
  }
  if (!target->impl.workers[local_wid].inited) {
    pthread_mutex_unlock(&g_kernel_registry_mu);
    if (err) snprintf(err, EPA_MAX_ERR, "dynamic ACL grant: target worker %u missing", (unsigned)local_wid);
    return 0;
  }
  for (i = 0; i < target->dynamic_acl_count; i++) {
    EpaDynamicAclEntry *entry = &target->dynamic_acl_entries[i];
    if (entry->remote_kernel_uid == remote_kernel_uid && entry->local_wid == local_wid) {
      pthread_mutex_unlock(&g_kernel_registry_mu);
      return 1;
    }
  }
  if (target->dynamic_acl_count == target->dynamic_acl_cap) {
    size_t next_cap = target->dynamic_acl_cap ? target->dynamic_acl_cap * 2u : 8u;
    next = (EpaDynamicAclEntry*)realloc(target->dynamic_acl_entries, sizeof(*next) * next_cap);
    if (!next) {
      pthread_mutex_unlock(&g_kernel_registry_mu);
      if (err) snprintf(err, EPA_MAX_ERR, "dynamic ACL grant: OOM");
      return 0;
    }
    target->dynamic_acl_entries = next;
    target->dynamic_acl_cap = next_cap;
  }
  target->dynamic_acl_entries[target->dynamic_acl_count].remote_kernel_uid = remote_kernel_uid;
  target->dynamic_acl_entries[target->dynamic_acl_count].local_wid = local_wid;
  target->dynamic_acl_count++;
  pthread_mutex_unlock(&g_kernel_registry_mu);
  return 1;
}

int epa_kernel_acl_revoke_by_uid(EpaKernel *actor, uint32_t source_wid,
                                 uint64_t target_kernel_uid, uint64_t remote_kernel_uid,
                                 uint32_t local_wid, char err[EPA_MAX_ERR]) {
  EpaKernel *target;
  size_t read;
  size_t write = 0u;
  if (err) err[0] = 0;
  if (!kernel_actor_has_acl_admin(actor, source_wid, err)) return 0;

  pthread_mutex_lock(&g_kernel_registry_mu);
  target = registry_find_kernel_by_uid_locked(target_kernel_uid);
  if (!target) {
    pthread_mutex_unlock(&g_kernel_registry_mu);
    if (err) snprintf(err, EPA_MAX_ERR, "dynamic ACL revoke: target kernel 0x%016llx not found",
                      (unsigned long long)target_kernel_uid);
    return 0;
  }
  for (read = 0u; read < target->dynamic_acl_count; read++) {
    EpaDynamicAclEntry entry = target->dynamic_acl_entries[read];
    if (entry.remote_kernel_uid == remote_kernel_uid && entry.local_wid == local_wid) continue;
    target->dynamic_acl_entries[write++] = entry;
  }
  target->dynamic_acl_count = write;
  pthread_mutex_unlock(&g_kernel_registry_mu);
  return 1;
}

int epa_kernel_acl_revoke_all_by_uid(EpaKernel *actor, uint32_t source_wid,
                                     uint64_t target_kernel_uid, uint64_t remote_kernel_uid,
                                     char err[EPA_MAX_ERR]) {
  EpaKernel *target;
  size_t read;
  size_t write = 0u;
  if (err) err[0] = 0;
  if (!kernel_actor_has_acl_admin(actor, source_wid, err)) return 0;

  pthread_mutex_lock(&g_kernel_registry_mu);
  target = registry_find_kernel_by_uid_locked(target_kernel_uid);
  if (!target) {
    pthread_mutex_unlock(&g_kernel_registry_mu);
    if (err) snprintf(err, EPA_MAX_ERR, "dynamic ACL revoke_all: target kernel 0x%016llx not found",
                      (unsigned long long)target_kernel_uid);
    return 0;
  }
  for (read = 0u; read < target->dynamic_acl_count; read++) {
    EpaDynamicAclEntry entry = target->dynamic_acl_entries[read];
    if (entry.remote_kernel_uid == remote_kernel_uid) continue;
    target->dynamic_acl_entries[write++] = entry;
  }
  target->dynamic_acl_count = write;
  pthread_mutex_unlock(&g_kernel_registry_mu);
  return 1;
}

int epa_kernel_far_signal_by_id(EpaKernel *sender, uint32_t source_wid, const char *target_kernel_id,
                                const void *payload, uint32_t payload_len, uint32_t payload_tag,
                                char err[EPA_MAX_ERR]) {
  if (!target_kernel_id || !target_kernel_id[0]) {
    if (err) snprintf(err, EPA_MAX_ERR, "far_signal_by_id: bad target id");
    return 0;
  }
  return epa_kernel_far_signal_by_uid(sender, source_wid, fnv1a64_bytes(target_kernel_id),
                                      0u, payload, payload_len, payload_tag, err);
}

static int init_workers_from_prog(KernelImpl *k, const EpaProgramDesc *prog, char err[EPA_MAX_ERR]) {
  EpaDynamicPoolConfig dynamic_cfgs[256];
  uint32_t dynamic_cfg_count = 0u;
  // wipe old workers and reset active-worker linked list
  for (uint32_t i = 0; i < EPA_MAX_WORKERS; i++) {
    if (k->workers[i].inited) epa_worker_free(&k->workers[i]);
    memset(&k->workers[i], 0, sizeof(k->workers[i]));
    k->worker_next[i] = EPA_MAX_WORKERS; // nil sentinel
  }
  k->n_workers   = 0;
  k->worker_head = EPA_MAX_WORKERS; // empty list
  k->privilege_locked = 0u;

  // tail tracks the last inserted wid for O(1) append.
  // IDs are scanned in ascending order so each new wid > tail.
  uint32_t tail = EPA_MAX_WORKERS;

  if (prog->dynamic_pool_count > 256u) {
    snprintf(err, EPA_MAX_ERR, "too many dynamic pools in program manifest: %zu", prog->dynamic_pool_count);
    return 0;
  }
  for (dynamic_cfg_count = 0; dynamic_cfg_count < (uint32_t)prog->dynamic_pool_count; dynamic_cfg_count++) {
    dynamic_cfgs[dynamic_cfg_count].pool_id = prog->dynamic_pools[dynamic_cfg_count].pool_id;
    dynamic_cfgs[dynamic_cfg_count].element_size = prog->dynamic_pools[dynamic_cfg_count].element_size;
    dynamic_cfgs[dynamic_cfg_count].min_free = prog->dynamic_pools[dynamic_cfg_count].min_free;
    dynamic_cfgs[dynamic_cfg_count].max_free = prog->dynamic_pools[dynamic_cfg_count].max_free;
    dynamic_cfgs[dynamic_cfg_count].grow_by = prog->dynamic_pools[dynamic_cfg_count].grow_by;
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
    k->workers[id].privilege = prog->worker_privilege[id];
    k->workers[id].ignore_max_ticks = prog->worker_ignore_max_ticks[id] ? 1u : 0u;
    if (!epa_worker_configure_dynamic_pools(&k->workers[id], dynamic_cfgs, dynamic_cfg_count, err)) {
      return 0;
    }

    // Append to active-worker linked list (ascending order guaranteed by loop).
    k->n_workers++;
    if (tail >= EPA_MAX_WORKERS) {
      k->worker_head = id;        // first entry
    } else {
      k->worker_next[tail] = id;  // link previous tail to this worker
    }
    // worker_next[id] is already EPA_MAX_WORKERS (nil) from the memset above
    tail = id;

    // Worker entry definitions are loaded immediately, but only the kernel
    // entry starts in the execution pool. The kernel activates workers with
    // ENTRY_EXEC, surfaced in E as start_worker(...).
    if (id == 0u) {
      k->workers[id].halted = 0;
      k->workers[id].blocked = 0;
    } else {
      k->workers[id].halted = 1;
      k->workers[id].blocked = 1;
    }

    // Store EIP in worker state if you've added it there.
    // If you haven't yet: add `EpaEip eip;` into EpaWorkerState.
    k->workers[id].vm.eip.block_type = EPA_BLOCK_ENTRY;
    k->workers[id].vm.eip.block_id   = (uint32_t)id;
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
  if (k->prog.kernel_uid != 0u && !epa_kernel_register_uid_only(k, k->prog.kernel_uid, err)) {
    epa_program_free(&k->prog);
    k->prog_loaded = 0;
    free(blob);
    return 0;
  }

  if (!init_workers_from_prog(&k->impl, &k->prog, err)) return 0;
  kernel_refresh_worker_vm_identity(k);

  // Build flow ctx + hooks
  k->local_kernel_uid = k->prog.kernel_uid;
  k->pid = 0u;
  k->child_cluster = 0u;
  kernel_install_standard_hooks(k);

  k->flow = epa_flow_ctx_make(&k->prog, k->hooks, k);

  // non-flow backend
  k->nf = epa_null_nf_backend;

  k->owned_blob = blob;
  k->owned_blob_len = blob_len;
  epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_LOADED, NULL);
  return 1;
}

static void kernel_install_standard_hooks(EpaKernel *k) {
  memset(&k->hooks, 0, sizeof(k->hooks));
  k->hooks.on_entry_exec   = hook_entry_exec;
  k->hooks.on_entry_halt   = hook_entry_halt;
  k->hooks.on_entry_retire = hook_entry_retire;
  k->hooks.on_kernel_retire = hook_kernel_retire;
  k->hooks.on_entry_privilege = hook_entry_privilege;
  k->hooks.on_privilege_lock = hook_privilege_lock;
  k->hooks.on_acl_grant     = hook_acl_grant;
  k->hooks.on_acl_revoke    = hook_acl_revoke;
  k->hooks.on_acl_revoke_all = hook_acl_revoke_all;
  k->hooks.on_pid_self      = hook_pid_self;
  k->hooks.on_pid_retire    = hook_pid_retire;
  k->hooks.on_sync         = hook_sync;
  k->hooks.on_wait_on_sync = hook_wait_on_sync;
  k->hooks.get_worker      = hook_get_worker;
  k->hooks.on_break        = hook_break;
  k->hooks.on_trap         = hook_trap;
  k->hooks.on_except       = hook_except;
  k->hooks.on_signal       = hook_signal;
  k->hooks.on_far_signal   = hook_far_signal;
  k->hooks.on_host_signal  = hook_host_signal;
  k->hooks.on_request_threads = hook_request_threads;
  k->hooks.on_request_at   = hook_request_at;
  k->hooks.on_request_dynamic_pool_capacity = hook_request_dynamic_pool_capacity;
}

static int epa_kernel_load_blob_internal(EpaKernel *k, const uint8_t *blob, size_t blob_len,
                                         uint32_t pid, uint64_t override_kernel_uid,
                                         char err[EPA_MAX_ERR]) {
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
  k->local_kernel_uid = k->prog.kernel_uid;
  k->pid = pid;
  k->child_cluster = pid != 0u ? 1u : 0u;
  if (pid != 0u && k->prog.kernel_uid != 0u && override_kernel_uid == 0u) {
    override_kernel_uid = epa_kernel_namespace_uid(pid, k->prog.kernel_uid);
  }
  if (k->prog.kernel_uid != 0u && !epa_kernel_register_uid_only(k, override_kernel_uid ? override_kernel_uid : k->prog.kernel_uid, err)) {
    epa_program_free(&k->prog);
    k->prog_loaded = 0;
    free(owned);
    epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_ERROR, err);
    return 0;
  }

  if (!init_workers_from_prog(&k->impl, &k->prog, err)) {
    epa_program_free(&k->prog);
    k->prog_loaded = 0;
    free(owned);
    epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_ERROR, err);
    return 0;
  }
  kernel_refresh_worker_vm_identity(k);

  kernel_install_standard_hooks(k);
  k->flow = epa_flow_ctx_make(&k->prog, k->hooks, k);
  k->nf = epa_null_nf_backend;

  k->owned_blob = owned;
  k->owned_blob_len = blob_len;
  epa_kernel_set_status_text(k, EPA_KERNEL_STATUS_LOADED, NULL);
  return 1;
}

int epa_kernel_load_blob(EpaKernel *k, const uint8_t *blob, size_t blob_len, char err[EPA_MAX_ERR]) {
  return epa_kernel_load_blob_internal(k, blob, blob_len, 0u, 0u, err);
}

static uint32_t pad4(uint32_t n) { return (n + 3u) & ~3u; }

static int epa_kernel_ingress_push_framed(EpaKernel *k, uint32_t wid, uint32_t tag,
                                          const void *data, uint32_t len,
                                          uint64_t source_kernel_uid,
                                          uint32_t source_worker_id) {
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
	q->q[q->tail].source_kernel_uid = source_kernel_uid;
	q->q[q->tail].source_worker_id = source_worker_id;
	q->tail = (q->tail + 1) % EPA_INGRESS_QMAX;
	q->count++;

  if (k->sched_vt && k->sched_vt->wake) {
    k->sched_vt->wake(k, &k->sched_state);
  }

	return 1;
}

int epa_kernel_ingress_push_tagged(EpaKernel *k, uint32_t wid, uint32_t tag, const void *data, uint32_t len) {
  return epa_kernel_ingress_push_framed(k, wid, tag, data, len, EPA_HOST_KERNEL_UID, EPA_HOST_WORKER_ID);
}

int epa_kernel_ingress_push(EpaKernel *k, uint32_t wid, const void *data, uint32_t len) {
  return epa_kernel_ingress_push_tagged(k, wid, 0u, data, len);
}

int epa_kernel_set_worker_ignore_max_ticks(EpaKernel *k, uint32_t wid, int ignore) {
  if (!k || wid >= EPA_MAX_WORKERS || !k->impl.workers[wid].inited) return 0;
  k->impl.workers[wid].ignore_max_ticks = ignore ? 1u : 0u;
  return 1;
}

int epa_kernel_get_worker_ignore_max_ticks(const EpaKernel *k, uint32_t wid) {
  if (!k || wid >= EPA_MAX_WORKERS || !k->impl.workers[wid].inited) return 0;
  return k->impl.workers[wid].ignore_max_ticks ? 1 : 0;
}

static void ingress_free_msg(EpaIngressMsg *m) {
  free(m->buf);
  m->buf = NULL;
  m->len = 0;
  m->tag = 0u;
  m->source_kernel_uid = EPA_HOST_KERNEL_UID;
  m->source_worker_id = EPA_HOST_WORKER_ID;
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
                                         uint64_t source_kernel_uid,
                                         uint32_t source_worker_id,
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
  dst->ingress_source_kernel_uid = source_kernel_uid;
  dst->ingress_source_worker_id = source_worker_id;

  if (epa_ring_space(&dst->inq) < 4) {
    kernel_fault_worker(k, dst_wid, "INGRESS FAULT: ring buffer full for wid=%u (need 4 words)", dst_wid);
    snprintf(err, EPA_MAX_ERR, "ingress: wid=%u inq full (need 4 words)", dst_wid);
    return 0;
  }

  uint32_t idx = epa_ghs_handle_index(h);
  uint32_t gen2 = epa_ghs_handle_gen(h);

  // epa_ring_push takes err[256]; EPA_MAX_ERR is 256 in your project.
  if (!epa_ring_push(&dst->inq, 1 /* Always one because there is only one GHS handle*/, 0, err)) return 0;
  if (!epa_ring_push(&dst->inq, idx,               0, err)) return 0;
  if (!epa_ring_push(&dst->inq, gen2,              0, err)) return 0;
  if (!epa_ring_push(&dst->inq, len_bytes,         0, err)) return 0;

  if (dst->waiting_for_data) {
    if (!epa_worker_round_enter(dst, err)) return 0;
  }
  dst->waiting_for_data = 0;
  dst->blocked = 0;
  if (k->sched_vt && k->sched_vt->wake) {
    k->sched_vt->wake(k, &k->sched_state);
  }

  return 1;
}

int epa_kernel_deliver_ghs_handles_framed(EpaKernel *k,
                                         uint32_t dst_wid,
                                         const uint64_t *ghs_handles,
                                         uint32_t ghs_handle_count,
                                         uint64_t source_kernel_uid,
                                         uint32_t source_worker_id,
                                         char err[EPA_MAX_ERR]) {
  if (!k || !ghs_handles || ghs_handle_count == 0) {
    snprintf(err, EPA_MAX_ERR, "ingress: bad args");
    return 0;
  }
  if (dst_wid >= EPA_MAX_WORKERS) {
    snprintf(err, EPA_MAX_ERR, "ingress: bad wid %u", dst_wid);
    return 0;
  }

  // GHS is shared; convention: kernel (owner=0).
  epa_ghs_t *ghs = k->impl.ghs;
  if (!ghs) {
    snprintf(err, EPA_MAX_ERR, "ingress: GHS not initialized");
    return 0;
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
				return 0;
			  }
		  }
	  }
  }

  // 4) Ring notify: push count + handle lo/gen pairs to dst worker input ring.
  EpaWorkerState *dst = &k->impl.workers[dst_wid];
  dst->ingress_source_kernel_uid = source_kernel_uid;
  dst->ingress_source_worker_id = source_worker_id;
  uint32_t needed_words = 2u + (ghs_handle_count * 2u);

  if (epa_ring_space(&dst->inq) < needed_words) {
    kernel_fault_worker(k, dst_wid, "INGRESS FAULT: ring buffer full for wid=%u (need %u words)", dst_wid, needed_words);
    snprintf(err, EPA_MAX_ERR, "ingress: wid=%u inq full (need %u words)", dst_wid, needed_words);
    return 0;
  }

  // epa_ring_push takes err[256]; EPA_MAX_ERR is 256 in your project.
  if (!epa_ring_push(&dst->inq, ghs_handle_count, 0, err)) return 0;

  for (int i=0; i<ghs_handle_count; i++) {
	  uint32_t idx = epa_ghs_handle_index(ghs_handles[i]);
	  uint32_t gen2 = epa_ghs_handle_gen(ghs_handles[i]);

	  if (!epa_ring_push(&dst->inq, idx,               0, err)) return 0;
	  if (!epa_ring_push(&dst->inq, gen2,              0, err)) return 0;
  }
  if (!epa_ring_push(&dst->inq, 0u, 0, err)) return 0;

  if (dst->waiting_for_data) {
    if (!epa_worker_round_enter(dst, err)) return 0;
  }
  dst->waiting_for_data = 0;
  dst->blocked = 0;
  if (k->sched_vt && k->sched_vt->wake) {
    k->sched_vt->wake(k, &k->sched_state);
  }

  return 1;
}

int epa_kernel_deliver_ghs_handles(EpaKernel *k,
                                         uint32_t dst_wid,
                                         const uint64_t *ghs_handles,
                                         uint32_t ghs_handle_count,
                                         char err[EPA_MAX_ERR]) {
  return epa_kernel_deliver_ghs_handles_framed(k, dst_wid, ghs_handles, ghs_handle_count,
                                               EPA_HOST_KERNEL_UID, EPA_HOST_WORKER_ID, err);
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
      uint64_t source_kernel_uid = m->source_kernel_uid;
      uint32_t source_worker_id = m->source_worker_id;

      if (!buf || len == 0) {
        // consume malformed msg so we don't deadlock the queue
        ingress_free_msg(m);
        q->head = (q->head + 1) % EPA_INGRESS_QMAX;
        q->count--;
        continue;
      }

      if (!epa_kernel_deliver_ingress_msg(k, wid, buf, tag, len,
                                          source_kernel_uid, source_worker_id, err)) {
        // do NOT consume the message if delivery failed: keeps behavior deterministic
        // (host can retry / expand rings / etc)
        return 0;
      }

      // consume on success
      ingress_free_msg(m);
      q->head = (q->head + 1) % EPA_INGRESS_QMAX;
      q->count--;
      k->impl.ingress_deliveries++;
    }
  }

  return 1;
}

extern const EpaSchedulerVt EPA_SCHED_WAVE_VT;
extern const EpaSchedulerVt EPA_SCHED_CPU_THREAD_VT;
extern const EpaSchedulerVt EPA_SCHED_DEBUG_VT;

static const EpaSchedulerVt *epa_sched_vt_for(EpaSchedProfile p) {
  switch (p) {
    case EPA_SCHED_WAVE:
      return &EPA_SCHED_WAVE_VT;
    case EPA_SCHED_CPU_THREAD:
      return &EPA_SCHED_CPU_THREAD_VT;
    case EPA_SCHED_DEBUG:
      return &EPA_SCHED_DEBUG_VT;
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
  if (epa_kernel_get_status(k) == EPA_KERNEL_STATUS_UNLOADED) {
    return 1;
  }
  epa_kernel_set_status_only(k, EPA_KERNEL_STATUS_RUNNING);
  rc = k->sched_vt->run(k, &k->sched_state, max_ticks, debug, err);
  if (epa_kernel_get_status(k) == EPA_KERNEL_STATUS_UNLOADED) {
    return rc;
  }
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

uint32_t epa_kernel_worker_count(const EpaKernel *k) {
  if (!k) return 0u;
  return k->impl.n_workers;
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

static EpaKernelModule* epa_kernel_module_load_bundle_bytes_internal(const uint8_t *buf, size_t file_len,
                                                                     uint32_t pid, char err[EPA_MAX_ERR]) {
  EpaKernelModule *module = NULL;
  uint32_t version;
  uint32_t count;
  uint32_t i;

  if (err) err[0] = 0;
  if (file_len < 16u || memcmp(buf, EPA_BUNDLE_MAGIC, 8u) != 0) {
    if (err) snprintf(err, EPA_MAX_ERR, "bundle load: invalid magic");
    return NULL;
  }
  version = read_u32_le(buf + 8u);
  count = read_u32_le(buf + 12u);
  if (version != EPA_BUNDLE_VERSION) {
    if (err) snprintf(err, EPA_MAX_ERR, "bundle load: unsupported version %u", (unsigned)version);
    return NULL;
  }
  if (file_len < 16u + (size_t)count * 24u) {
    if (err) snprintf(err, EPA_MAX_ERR, "bundle load: truncated index");
    return NULL;
  }

  module = (EpaKernelModule*)calloc(1, sizeof(*module));
  if (!module) {
    if (err) snprintf(err, EPA_MAX_ERR, "bundle load: OOM");
    return NULL;
  }
  module->count = (size_t)count;
  module->entries = (EpaKernelModuleEntry*)calloc(module->count, sizeof(EpaKernelModuleEntry));
  if (!module->entries) {
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
      return NULL;
    }

    path_id = (char*)malloc((size_t)path_len + 1u);
    if (!path_id) {
      if (err) snprintf(err, EPA_MAX_ERR, "bundle load: OOM");
      module_free_entries(module);
      free(module);
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
      return NULL;
    }
    if (!epa_kernel_load_blob_internal(kernel, dst->blob, dst->blob_len, pid, 0u, err)) {
      epa_kernel_destroy(kernel);
      module_free_entries(module);
      free(module);
      return NULL;
    }
    if (!epa_kernel_get_id(kernel) && !epa_kernel_set_id(kernel, path_id, err)) {
      epa_kernel_destroy(kernel);
      module_free_entries(module);
      free(module);
      return NULL;
    }
    if (pid != 0u) {
      pthread_mutex_lock(&g_kernel_registry_mu);
      if (!process_add_kernel_locked(pid, kernel, err)) {
        pthread_mutex_unlock(&g_kernel_registry_mu);
        epa_kernel_destroy(kernel);
        module_free_entries(module);
        free(module);
        return NULL;
      }
      pthread_mutex_unlock(&g_kernel_registry_mu);
    }
    if (!epa_kernel_set_scheduler(kernel, EPA_SCHED_CPU_THREAD, err)) {
      epa_kernel_destroy(kernel);
      module_free_entries(module);
      free(module);
      return NULL;
    }
    dst->kernel = kernel;
    dst->last_error[0] = 0;
  }

  return module;
}

EpaKernelModule* epa_kernel_module_load_bundle(const char *bundle_path, char err[EPA_MAX_ERR]) {
  FILE *f;
  long file_len_long;
  size_t file_len;
  uint8_t *buf = NULL;
  EpaKernelModule *module;

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

  module = epa_kernel_module_load_bundle_bytes_internal(buf, file_len, 0u, err);
  free(buf);
  return module;
}

EpaKernelModule* epa_kernel_process_load_bundle_bytes(EpaKernel *actor, uint32_t source_wid,
                                                      const uint8_t *bundle, size_t bundle_len,
                                                      uint32_t requested_pid, uint32_t *out_pid,
                                                      char err[EPA_MAX_ERR]) {
  EpaKernelModule *module;
  if (err) err[0] = 0;
  if (out_pid) *out_pid = 0u;
  if (!bundle || bundle_len == 0u || requested_pid == 0u) {
    if (err) snprintf(err, EPA_MAX_ERR, "process load: bad args");
    return NULL;
  }
  if (!kernel_actor_has_privilege(actor, source_wid, EPA_PRIVILEGE_ACL_ADMIN, "process load", err)) {
    return NULL;
  }

  pthread_mutex_lock(&g_kernel_registry_mu);
  if (process_find_locked(requested_pid)) {
    pthread_mutex_unlock(&g_kernel_registry_mu);
    if (err) snprintf(err, EPA_MAX_ERR, "process load: PID %u already exists", (unsigned)requested_pid);
    return NULL;
  }
  pthread_mutex_unlock(&g_kernel_registry_mu);

  module = epa_kernel_module_load_bundle_bytes_internal(bundle, bundle_len, requested_pid, err);
  if (!module) return NULL;
  if (out_pid) *out_pid = requested_pid;
  return module;
}

int epa_kernel_pid_retire(EpaKernel *actor, uint32_t source_wid, uint32_t pid, char err[EPA_MAX_ERR]) {
  EpaKernel **targets = NULL;
  size_t count = 0u;
  if (err) err[0] = 0;
  if (!actor || source_wid >= EPA_MAX_WORKERS || !actor->impl.workers[source_wid].inited || pid == 0u) {
    if (err) snprintf(err, EPA_MAX_ERR, "pid retire: bad args");
    return 0;
  }
  if (actor->pid != pid &&
      !kernel_actor_has_privilege(actor, source_wid, EPA_PRIVILEGE_ACL_ADMIN, "pid retire", err)) {
    return 0;
  }

  pthread_mutex_lock(&g_kernel_registry_mu);
  {
    EpaProcessRegistryNode *node = process_find_locked(pid);
    if (!node) {
      pthread_mutex_unlock(&g_kernel_registry_mu);
      if (err) snprintf(err, EPA_MAX_ERR, "pid retire: PID %u not found", (unsigned)pid);
      return 0;
    }
    count = node->count;
    targets = (EpaKernel**)malloc(sizeof(*targets) * (count ? count : 1u));
    if (!targets) {
      pthread_mutex_unlock(&g_kernel_registry_mu);
      if (err) snprintf(err, EPA_MAX_ERR, "pid retire: OOM");
      return 0;
    }
    for (size_t i = 0; i < count; i++) targets[i] = node->kernels[i];
    for (size_t i = 0; i < count; i++) {
      if (targets[i]) {
        registry_remove_kernel_locked(targets[i]);
        registry_remove_dynamic_routes_for_remote_locked(targets[i]->kernel_uid);
      }
    }
    while (node->count > 0u) {
      process_remove_kernel_locked(node->kernels[0]);
      node = process_find_locked(pid);
      if (!node) break;
    }
  }
  pthread_mutex_unlock(&g_kernel_registry_mu);

  for (size_t i = 0; i < count; i++) {
    EpaKernel *target = targets[i];
    if (!target) continue;
    for (uint32_t wid = 0; wid < EPA_MAX_WORKERS; wid++) {
      EpaWorkerState *w = &target->impl.workers[wid];
      if (!w->inited) continue;
      w->retired = 1;
      w->halted = 1;
      w->blocked = 1;
      epa_ring_clear(&w->inq);
      epa_ring_clear(&w->outq);
    }
    target->impl.worker_head = EPA_MAX_WORKERS;
    target->impl.n_workers = 0u;
    for (uint32_t wid = 0; wid < EPA_MAX_WORKERS; wid++) {
      target->impl.worker_next[wid] = EPA_MAX_WORKERS;
    }
    memset(&target->ingress, 0, sizeof(target->ingress));
    kernel_dynamic_acl_clear(target);
    kernel_atq_clear(target);
    kernel_memq_clear(target);
    epa_kernel_set_status_text(target, EPA_KERNEL_STATUS_UNLOADED, NULL);
    epa_kernel_request_interrupt(target);
  }
  free(targets);
  return 1;
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
    return 1;
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
    EpaKernel *k = module->entries[i].kernel;
    if (k && epa_kernel_get_scheduler(k) == EPA_SCHED_CPU_THREAD &&
        epa_kernel_thread_count(k) == 0u) {
      uint32_t n = epa_kernel_worker_count(k);
      if (n == 0u) n = 1u;
      if (!epa_kernel_module_add_kernel_threads(module, i, n, err)) return 0;
    }
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
