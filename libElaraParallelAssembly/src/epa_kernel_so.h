// epa_kernel_so.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#include "vm/epa_worker_state.h"
#include "epa_kernel.h"

#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  EPA_KERNEL_STATUS_UNLOADED = 0,
  EPA_KERNEL_STATUS_LOADED   = 1,
  EPA_KERNEL_STATUS_RUNNING  = 2,
  EPA_KERNEL_STATUS_STOPPED  = 3,
  EPA_KERNEL_STATUS_HALTED   = 4,
  EPA_KERNEL_STATUS_FAULTED  = 5,
  EPA_KERNEL_STATUS_ERROR    = 6
} EpaKernelStatus;

typedef struct EpaKernelModule EpaKernelModule;

// Create/destroy kernel runtime instance
EpaKernel* epa_kernel_create(char err[EPA_MAX_ERR]);
void       epa_kernel_destroy(EpaKernel *k);
int        epa_kernel_set_id(EpaKernel *k, const char *kernel_id, char err[EPA_MAX_ERR]);
const char* epa_kernel_get_id(const EpaKernel *k);
void       epa_kernel_set_signal_callback(EpaKernel *k, EpaKernelSignal cb);
EpaKernel* epa_kernel_find_by_id(const char *kernel_id);
int        epa_kernel_retire_by_uid(uint64_t kernel_uid, char err[EPA_MAX_ERR]);
int        epa_kernel_retire_by_id(const char *kernel_id, char err[EPA_MAX_ERR]);
uint32_t   epa_kernel_get_pid(const EpaKernel *k);
uint64_t   epa_kernel_namespace_uid(uint32_t pid, uint64_t local_uid);
uint64_t   epa_kernel_local_uid(const EpaKernel *k);
uint64_t   epa_kernel_resolve_uid_for_sender(const EpaKernel *sender, uint64_t local_or_global_uid);
EpaKernelModule* epa_kernel_process_load_bundle_bytes(EpaKernel *actor, uint32_t source_wid,
                                                      const uint8_t *bundle, size_t bundle_len,
                                                      uint32_t requested_pid, uint32_t *out_pid,
                                                      char err[EPA_MAX_ERR]);
int        epa_kernel_pid_retire(EpaKernel *actor, uint32_t source_wid, uint32_t pid, char err[EPA_MAX_ERR]);
int        epa_kernel_acl_grant_by_uid(EpaKernel *actor, uint32_t source_wid,
                                       uint64_t target_kernel_uid, uint64_t remote_kernel_uid,
                                       uint32_t local_wid, char err[EPA_MAX_ERR]);
int        epa_kernel_acl_revoke_by_uid(EpaKernel *actor, uint32_t source_wid,
                                        uint64_t target_kernel_uid, uint64_t remote_kernel_uid,
                                        uint32_t local_wid, char err[EPA_MAX_ERR]);
int        epa_kernel_acl_revoke_all_by_uid(EpaKernel *actor, uint32_t source_wid,
                                            uint64_t target_kernel_uid, uint64_t remote_kernel_uid,
                                            char err[EPA_MAX_ERR]);
int        epa_kernel_far_signal_by_uid(EpaKernel *sender, uint32_t source_wid, uint64_t target_kernel_uid,
                                        uint32_t target_wid_hint,
                                        const void *payload, uint32_t payload_len, uint32_t payload_tag,
                                        char err[EPA_MAX_ERR]);
int        epa_kernel_far_signal_by_id(EpaKernel *sender, uint32_t source_wid, const char *target_kernel_id,
                                       const void *payload, uint32_t payload_len, uint32_t payload_tag,
                                       char err[EPA_MAX_ERR]);

// Set debug callback (can be NULL to disable)
void epa_kernel_set_debug_callback(EpaKernel *k, EpaKernelDbgCallback cb, void *cb_user);

// Load program (either compile asm or accept blob)
int epa_kernel_load_asm(EpaKernel *k, const char *asm_path, char err[EPA_MAX_ERR]);
int epa_kernel_load_blob(EpaKernel *k, const uint8_t *blob, size_t blob_len, char err[EPA_MAX_ERR]);
int epa_kernel_set_scheduler(EpaKernel *k, EpaSchedProfile profile, char err[EPA_MAX_ERR]);
EpaSchedProfile epa_kernel_get_scheduler(const EpaKernel *k);
int epa_kernel_add_threads(EpaKernel *k, uint32_t add_count, char err[EPA_MAX_ERR]);
uint32_t epa_kernel_thread_count(const EpaKernel *k);
uint32_t epa_kernel_worker_count(const EpaKernel *k);
int epa_kernel_set_worker_ignore_max_ticks(EpaKernel *k, uint32_t wid, int ignore);
int epa_kernel_get_worker_ignore_max_ticks(const EpaKernel *k, uint32_t wid);
EpaKernelStatus epa_kernel_get_status(const EpaKernel *k);
const char* epa_kernel_get_last_error(const EpaKernel *k);
const char* epa_kernel_status_name(EpaKernelStatus status);

int epa_kernel_ingress_push(EpaKernel *k, uint32_t wid, const void *data, uint32_t len);
int epa_kernel_ingress_push_tagged(EpaKernel *k, uint32_t wid, uint32_t tag, const void *data, uint32_t len);

// Run scheduler loop until done or timeout.
// Returns 1 on clean halt, 0 on error (err filled).
int epa_kernel_run(EpaKernel *k, uint32_t max_ticks, int debug, char err[EPA_MAX_ERR]);
void epa_kernel_request_interrupt(EpaKernel *k);

// Optional: retrieve output buffer from kernel slot 0 if you use it
const uint8_t* epa_kernel_get_result(EpaKernel *k, size_t *out_len);

// epa_kernel_so.h (add)

typedef struct {
  // Optional unified callback (if non-NULL, called for all kinds)
  EpaKernelDbgCallback on_debug;

  // Optional per-kind callbacks (used if on_debug == NULL)
  EpaKernelDbgCallback on_break;
  EpaKernelDbgCallback on_trap;
  EpaKernelDbgCallback on_except;

  void *user;
} EpaKernelDbgHooks;

// Sets/overwrites hooks (pass NULL to clear)
void epa_kernel_set_debug_hooks(EpaKernel *k, const EpaKernelDbgHooks *hooks);

int epa_kernel_deliver_ghs_handles(EpaKernel *k, uint32_t dst_wid, const uint64_t *ghs_handles, uint32_t ghs_handle_count, char err[EPA_MAX_ERR]);
int epa_kernel_deliver_ghs_handles_framed(EpaKernel *k, uint32_t dst_wid,
                                          const uint64_t *ghs_handles, uint32_t ghs_handle_count,
                                          uint64_t source_kernel_uid, uint32_t source_worker_id,
                                          char err[EPA_MAX_ERR]);

EpaKernelModule* epa_kernel_module_load_bundle(const char *bundle_path, char err[EPA_MAX_ERR]);
void epa_kernel_module_destroy(EpaKernelModule *module);
size_t epa_kernel_module_count(const EpaKernelModule *module);
const char* epa_kernel_module_path_id(const EpaKernelModule *module, size_t index);
uint32_t epa_kernel_module_flags(const EpaKernelModule *module, size_t index);
EpaKernel* epa_kernel_module_kernel(const EpaKernelModule *module, size_t index);
int epa_kernel_module_find_kernel(const EpaKernelModule *module, const char *path_id);
EpaKernelStatus epa_kernel_module_kernel_status(const EpaKernelModule *module, size_t index);
const char* epa_kernel_module_kernel_error(const EpaKernelModule *module, size_t index);
int epa_kernel_module_start_kernel(EpaKernelModule *module, size_t index, char err[EPA_MAX_ERR]);
int epa_kernel_module_stop_kernel(EpaKernelModule *module, size_t index, char err[EPA_MAX_ERR]);
int epa_kernel_module_add_kernel_threads(EpaKernelModule *module, size_t index, uint32_t add_count, char err[EPA_MAX_ERR]);
uint32_t epa_kernel_module_kernel_thread_count(const EpaKernelModule *module, size_t index);
int epa_kernel_module_start_all_kernels(EpaKernelModule *module, char err[EPA_MAX_ERR]);
int epa_kernel_module_stop_all_kernels(EpaKernelModule *module, char err[EPA_MAX_ERR]);

#ifdef __cplusplus
}
#endif
