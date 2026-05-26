#pragma once
#include <stdint.h>
#include <stddef.h>
#include "epa_program_desc.h"
#include "vm/epa_worker_state.h"

#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

static inline int epa_prog_resolve_view(
    const EpaProgramDesc *p,
    uint8_t block_type, uint32_t block_id,
    const uint8_t **out_code, size_t *out_len
) {
  if (!p || !out_code || !out_len) return 0;
  if (block_type == EPA_BLOCK_ENTRY) {
    if (block_id >= 256 || !p->entry_present[block_id]) return 0;
    *out_code = p->entries[block_id].code;
    *out_len  = p->entries[block_id].code_len;
    return 1;
  }
  if (block_type == EPA_BLOCK_FUNC) {
    if (block_id >= p->nfuncs) return 0;
    *out_code = p->funcs[block_id].code.code;
    *out_len  = p->funcs[block_id].code.code_len;
    return 1;
  }
  return 0;
}

// VTable for “backend under flow”
typedef struct {
  EpaNonFlowRc (*exec_one)(
      void *impl,
      const EpaProgramDesc *prog,
      EpaWorkerState *w,
      EpaEip *eip,
      char err[EPA_MAX_ERR]
  );
} EpaNonFlowBackendVTable;

typedef struct {
  const EpaNonFlowBackendVTable *vt;
  void *impl;
} EpaNonFlowBackend;
