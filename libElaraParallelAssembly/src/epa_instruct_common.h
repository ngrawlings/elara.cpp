#pragma once
#include <stdint.h>

// Common State Control (CSC)
// r0..r3 live here (flow-owned). r0 is the canonical condition/result register.
#ifndef EPA_CSC_SLOTS
#define EPA_CSC_SLOTS 4
#endif
#include <stddef.h>

#include "vm/epa_worker_state.h"
#include "epa_program_loader.h"
#include "epa_program_desc.h"   // EpaProgramDesc resolve
#include "opcodes/epa_opcode_values.h"
#include "opcodes/opcode_def.h"         // EpaOpcodeDef, epa_find_opcode
#include "memory/epa_stack.h"

#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

int hook_entry_exec(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
int hook_entry_halt(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
int hook_sync(void *user, char err[EPA_MAX_ERR]);
int hook_wait_on_sync(void *user, char err[EPA_MAX_ERR]);
EpaWorkerState* hook_get_worker(void *user, uint8_t wid);
int hook_far_signal(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
int hook_host_signal(void *user, uint8_t wid, char err[EPA_MAX_ERR]);

typedef enum {
  EPA_FLOW_ERR      			= 0,
  EPA_FLOW_OK       			= 1,   // e.g. END/halt
  EPA_FLOW_YIELDED  			= 2,   // executed exactly one op
  EPA_FLOW_NOT_FLOW 			= 3    // caller should dispatch to backend
} EpaFlowRc;

// CALL frame helpers
int epa_flow_push_ret_frame(EpaStack *st, const EpaEip *ret, uint16_t argc, char err[EPA_MAX_ERR]);
int epa_flow_pop_ret_frame(EpaStack *st, EpaEip *out_ret, uint16_t *out_argc, char err[EPA_MAX_ERR]);

// Backend hooks that flow opcodes can trigger (scheduler hooks etc)
typedef struct {
  // Kernel scheduling: ENTRY_EXEC wid, ENTRY_HALT wid
  int (*on_entry_exec)(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
  int (*on_entry_halt)(void *user, uint8_t wid, char err[EPA_MAX_ERR]);

  // SYNC / WAIT_ON_SYNC hooks
  int (*on_sync)(void *user, char err[EPA_MAX_ERR]);
  int (*on_wait_on_sync)(void *user, char err[EPA_MAX_ERR]);

  int (*on_data_ready)(void *user, uint8_t wid, char err[EPA_MAX_ERR]);

  // Worker lookup (needed by common ring-buffer transfer ops)
    // Return NULL if wid invalid / not present.
  EpaWorkerState *(*get_worker)(void *user, uint8_t wid);

  // Interrupt / debug hooks (flow-owned)
  // code: user-defined u32 (assert ids, breakpoint ids, etc.)
  // at:   instruction location (copy of worker EIP at the interrupt op)
  int (*on_break)(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]);
  int (*on_trap)(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]);
  int (*on_except)(void *user, uint8_t wid, uint32_t code, const EpaEip *at, char err[EPA_MAX_ERR]);
  int (*on_signal)(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
  int (*on_far_signal)(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
  int (*on_host_signal)(void *user, uint8_t wid, char err[EPA_MAX_ERR]);
} EpaFlowHooks;

typedef struct {
  const EpaProgramDesc *prog;
  EpaFlowHooks hooks;
  void *hooks_user;
} EpaFlowCtx;

// Execute one opcode if it is a flow opcode.
// If not a flow opcode, returns EPA_FLOW_NOT_FLOW (and does not mutate state).
EpaFlowRc epa_flow_step(
	void *k,
    const EpaFlowCtx *ctx,
	EpaWorkerState *w,
    EpaStack *st,
    char err[EPA_MAX_ERR]
);
