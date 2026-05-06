#pragma once

#include "../src/epa_asm_compiler.h"
#include "log.h"
#include "epa_program_loader.h"
#include "epa_flow_glue.h"
#include "epa_backend_nonflow.h"
#include "epa_instruct_common.h"
#include "gui/viewport.h"

#include "memory/epa_ring_buffer.h"
#include "vm/epa_worker_state.h"

#include "epa_scheduler.h"
#include "threads/epa_thread_pool.h"

#define EPA_INGRESS_QMAX  16

// Event kinds
typedef enum {
  EPA_KDBG_BREAK  = 1,
  EPA_KDBG_TRAP   = 2,
  EPA_KDBG_EXCEPT = 3,
  EPA_KDBG_SIGNAL = 4,
} EpaKernelDbgKind;

typedef struct {
  // same impl you have today
  IdRing syncq;
  EpaWorkerState workers[EPA_MAX_WORKERS];

  // "current running slot" so SYNC/WAIT hooks know who called them
  uint32_t cur_wid;

  uint32_t rr_cursor;

  epa_ghs_t* ghs;
  EpaThreadPool *tp;

  int interrupt_requested;
} KernelImpl;

typedef void (*EpaKernelDbgCallback)(
    void *cb_user,
    EpaKernelDbgKind kind,
    uint8_t wid,
    uint32_t code,
    const EpaEip *at,
    const char *msg   // optional extra text (may be NULL)
);

typedef int (*EpaKernelSignal)(
    uint8_t wid,
    const char *msg,
	const int msg_len
);

typedef struct {
  uint8_t *buf;     // owned heap buffer (malloc)
  uint32_t len;     // bytes (already padded to 4 if you want)
} EpaIngressMsg;

typedef struct {
  EpaIngressMsg q[EPA_INGRESS_QMAX];
  uint32_t head;    // pop index
  uint32_t tail;    // push index
  uint32_t count;
} EpaIngressQ;

typedef struct {
  EpaIngressQ inq[EPA_MAX_WORKERS]; // one queue per worker/entry id
} EpaIngress;

typedef struct EpaKernel {
  KernelImpl impl;

  EpaProgramDesc prog;
  int prog_loaded;

  EpaIngress ingress;

  Viewport *vp;

  EpaFlowHooks hooks;
  EpaFlowCtx   flow;

  EpaNonFlowBackend nf;

  EpaKernelSignal signal_cb;

  // Debug callback
  EpaKernelDbgCallback dbg_cb;
  void *dbg_user;

  EpaSchedProfile sched_profile;
  const EpaSchedulerVt *sched_vt;
  EpaSchedState sched_state;
} EpaKernel;
