#pragma once

#include "epa_kernel.h"

#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

void kdbg_emit(EpaKernel *k, EpaKernelDbgKind kind, uint8_t wid, uint32_t code, const EpaEip *at, const char *msg);
int hook_entry_exec(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
int hook_entry_halt(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
int hook_sync(void *user, char err[EPA_MAX_ERR]);
int hook_wait_on_sync(void *user, char err[EPA_MAX_ERR]);
EpaWorkerState* hook_get_worker(void *user, uint8_t wid);
int hook_break(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]);
int hook_trap(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]);
int hook_except(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]);
int hook_signal(void *user, uint8_t wid, char err[EPA_MAX_ERR]);


