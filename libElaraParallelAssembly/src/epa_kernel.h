#pragma once

#include "../src/epa_asm_compiler.h"
#include "log.h"
#include "epa_program_loader.h"
#include "epa_flow_glue.h"
#include "epa_backend_nonflow.h"
#include "epa_instruct_common.h"

#include "memory/epa_ring_buffer.h"
#include "vm/epa_worker_state.h"

#include "epa_scheduler.h"
#include "threads/epa_thread_pool.h"
#include <pthread.h>

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

  // Active-worker linked list.  Slot indices (wid) are unchanged.
  // Schedulers iterate the list instead of scanning all EPA_MAX_WORKERS slots.
  // EPA_MAX_WORKERS is the nil/end sentinel in both fields.
  uint32_t n_workers;                      // count of initialized workers
  uint32_t worker_head;                    // first active wid
  uint32_t worker_next[EPA_MAX_WORKERS];   // next active wid per slot

  epa_ghs_t* ghs;
  EpaThreadPool *tp;

  int interrupt_requested;
  pthread_mutex_t syncq_mu;
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
  uint32_t tag;     // GHS tag/type id for runtime routing
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

  EpaFlowHooks hooks;
  EpaFlowCtx   flow;

  EpaNonFlowBackend nf;

  EpaKernelSignal signal_cb;
  char *kernel_id;
  uint8_t *owned_blob;
  size_t owned_blob_len;

  // Debug callback
  EpaKernelDbgCallback dbg_cb;
  void *dbg_user;

  EpaSchedProfile sched_profile;
  const EpaSchedulerVt *sched_vt;
  EpaSchedState sched_state;
  pthread_mutex_t state_mu;
  int runtime_status;
  char last_error[EPA_MAX_ERR];
} EpaKernel;
