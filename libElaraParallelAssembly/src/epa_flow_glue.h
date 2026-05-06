#pragma once
#include <stdint.h>
#include <stddef.h>

#include "epa_instruct_common.h"
#include "epa_program_loader.h"
#include "vm/epa_worker_state.h"

#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

// Resolve EIP -> code view (shared utility; flow + backend-under-flow can use it)
int epa_resolve_eip(
    const EpaProgramDesc *prog,
    const EpaEip *eip,
    const uint8_t **out_code, size_t *out_len,
    uint32_t *out_abs_base,   // optional: blob absolute base
    char err[EPA_MAX_ERR]
);

// Convenience: build flow ctx
static inline EpaFlowCtx epa_flow_ctx_make(const EpaProgramDesc *p, EpaFlowHooks h, void *user) {
  EpaFlowCtx c;
  c.prog = p;
  c.hooks = h;
  c.hooks_user = user;
  return c;
}
