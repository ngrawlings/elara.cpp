#pragma once
#include <stdint.h>
#include <stddef.h>

#include "../memory/epa_ring_buffer.h"
#include "../memory/epa_dynamic_pool.h"
#include "epa_vm.h"

#define EPA_MAX_WORKERS 256

#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

#ifndef EPA_CALLSTACK_MAX
#define EPA_CALLSTACK_MAX 256
#endif

// Flow layer gives us a resolved code view for "current EIP" already.
// But for simplicity we re-resolve from descriptor table.
typedef enum {
  EPA_NF_EXEC_ERR      = 0,
  EPA_NF_EXEC_OK       = 1,   // non-flow opcode executed; continue
  EPA_NF_EXEC_HALT     = 2,   // backend decided program completed (e.g. FRAME window closed)
  EPA_NF_EXEC_NOT_MINE = 3    // opcode is flow-owned; caller should run FlowLogic
} EpaNonFlowRc;

typedef struct {
    uint32_t active;        // 0/1
    uint32_t fn;            // r0 EPA_FUNCTION
    epa_ghs_handle_t init_h; // r1
    uint32_t want_threads;  // r2 requested
    uint32_t got_threads;   // assigned
    uint32_t done_threads;  // completed
} EpaAtCtx;

typedef enum { EPA_EXEC_WORKER=0, EPA_EXEC_THREAD=1 } EpaExecType;

typedef struct {
  uint32_t pool_id;
  uint32_t element_size;
  uint32_t min_free;
  uint32_t max_free;
  uint32_t grow_by;
} EpaDynamicPoolConfig;

typedef struct {
  EpaExecType exec_type;
  EpaVM vm;

  // thread identity (scheduler slot)
  uint32_t id;  // 0..255 (entry slot)

  int inited;
  int halted;

  // scheduling
  uint8_t blocked;   // 1 = do not tick
  uint8_t faulted;

  uint8_t waiting_for_data;
  uint8_t at_running;
  uint8_t has_current_ghs;

  // Descriptor-debug metadata (not used for control flow)
  // abs_base = absolute byte offset into original blob where this descriptor begins
  // code_len = length of this descriptor in bytes
  uint32_t abs_base;
  uint32_t code_len;

  EpaAtCtx at;

  // IO rings (u32 words)
  IdRing inq;
  IdRing outq;

  uint8_t  *signal_mailbox;      // A block of memory for passing data directly to the container via a signal interupt
  epa_ghs_handle_t current_ghs;  // Stable current ingress GHS for kernel-side inspection

  EpaDynamicPool *dynamic_pools;
  uint32_t dynamic_pool_count;

} EpaWorkerState;



int  epa_worker_init(EpaWorkerState *w, uint32_t block_id,
                     uint32_t body_start_pc, uint32_t body_end_pc,
                     uint32_t in_words, uint32_t out_words, uint32_t signal_mailbox_size,
                     char err[EPA_MAX_ERR]);

void epa_worker_free(EpaWorkerState *w);
void epa_worker_reset(EpaWorkerState *w);
int  epa_worker_configure_dynamic_pools(EpaWorkerState *w,
                                        const EpaDynamicPoolConfig *configs,
                                        uint32_t config_count,
                                        char err[EPA_MAX_ERR]);
int  epa_worker_round_enter(EpaWorkerState *w, char err[EPA_MAX_ERR]);
