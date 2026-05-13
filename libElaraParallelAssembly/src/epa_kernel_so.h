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

// Forward types from your runtime headers (keep includes minimal in public API)
typedef struct Viewport Viewport;

// Create/destroy kernel runtime instance
EpaKernel* epa_kernel_create(char err[EPA_MAX_ERR]);
void       epa_kernel_destroy(EpaKernel *k);
int        epa_kernel_set_id(EpaKernel *k, const char *kernel_id, char err[EPA_MAX_ERR]);
const char* epa_kernel_get_id(const EpaKernel *k);
EpaKernel* epa_kernel_find_by_id(const char *kernel_id);
int        epa_kernel_far_signal_by_id(EpaKernel *sender, uint32_t source_wid, const char *target_kernel_id,
                                       const void *payload, uint32_t payload_len, uint32_t payload_tag,
                                       char err[EPA_MAX_ERR]);

// Set debug callback (can be NULL to disable)
void epa_kernel_set_debug_callback(EpaKernel *k, EpaKernelDbgCallback cb, void *cb_user);

// Load program (either compile asm or accept blob)
int epa_kernel_load_asm(EpaKernel *k, const char *asm_path, char err[EPA_MAX_ERR]);
int epa_kernel_load_blob(EpaKernel *k, const uint8_t *blob, size_t blob_len, char err[EPA_MAX_ERR]);

// Viewport control (optional; if you want headless tests, allow vp=NULL and make backends handle it)
int epa_kernel_open_viewport(EpaKernel *k, int w, int h, const char *title, int enable_cuda, char err[EPA_MAX_ERR]);
void epa_kernel_close_viewport(EpaKernel *k);

int epa_kernel_ingress_push(EpaKernel *k, uint32_t wid, const void *data, uint32_t len);
int epa_kernel_ingress_push_tagged(EpaKernel *k, uint32_t wid, uint32_t tag, const void *data, uint32_t len);

// Run scheduler loop until done, timeout, or viewport closes.
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

#ifdef __cplusplus
}
#endif
