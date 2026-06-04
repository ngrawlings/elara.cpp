#pragma once

#include "epa_kernel.h"

#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

void kdbg_emit(EpaKernel *k, EpaKernelDbgKind kind, uint8_t wid, uint32_t code, const EpaEip *at, const char *msg);
void epa_print_fault_location(EpaKernel *k, uint32_t wid, const EpaEip *eip, const char *detail);
int hook_entry_exec(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
int hook_entry_halt(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
int hook_sync(void *user, char err[EPA_MAX_ERR]);
int hook_wait_on_sync(void *user, char err[EPA_MAX_ERR]);
EpaWorkerState* hook_get_worker(void *user, uint8_t wid);
int hook_break(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]);
int hook_trap(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]);
int hook_except(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]);
int hook_signal(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
int hook_far_signal(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
int hook_host_signal(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
int hook_request_threads(void *user, uint8_t wid, uint32_t desired_total, char err[EPA_MAX_ERR]);
// Returns 1=submitted, 2=retry later/backpressure, 0=hard error.
int hook_request_at(void *user, uint8_t wid, const uint32_t *descriptor_words, uint32_t descriptor_word_count, uint32_t *out_request_id, char err[EPA_MAX_ERR]);
// Returns 1=accepted/satisfied, 2=retry later/backpressure, 0=hard error.
int hook_request_dynamic_pool_capacity(void *user, uint8_t wid, uint32_t pool_id, uint32_t requested_capacity, int hard_order, char err[EPA_MAX_ERR]);
