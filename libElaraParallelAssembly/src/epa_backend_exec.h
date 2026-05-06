#pragma once
#include <stdint.h>
#include <stddef.h>

#include "epa_instruct_common.h"
#include "epa_program_desc.h"

#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

typedef struct EpaBackendExec EpaBackendExec;

typedef struct {
  // Execute exactly ONE non-flow opcode at current EIP.
  // Must advance eip->rel_pc by instruction length on success.
  // Must NOT handle CALL/RET/JMP/YIELD/SYNC/etc.
  EpaFlowRc (*exec_one)(
      EpaBackendExec *self,
      const EpaProgramDesc *prog,
      EpaEip *eip,
      EpaStack *stack,
      char err[EPA_MAX_ERR]
  );
} EpaBackendExecVTable;

struct EpaBackendExec {
  const EpaBackendExecVTable *vt;
  void *impl;
};
