#ifndef EPADBGSHIM_H
#define EPADBGSHIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EpaKernel EpaKernel;

#define EPA_DBG_MAX_WORKERS      256
#define EPA_DBG_STACK_PREVIEW    8
#define EPA_DBG_LOCALS           32
#define EPA_DBG_STACK_WORDS      64
#define EPA_DBG_ARENA_PREVIEW    160
#define EPA_DBG_GHS_PREVIEW      160

typedef struct {
    uint8_t  block_type;
    uint32_t block_id;
    uint32_t rel_pc;
} EpaDbgEip;

typedef struct {
    uint32_t  wid;
    uint32_t  active;
    uint32_t  inited;
    uint32_t  halted;
    uint32_t  blocked;
    uint32_t  faulted;
    uint32_t  waiting_for_data;
    uint32_t  at_running;
    uint32_t  has_current_ghs;
    uint32_t  csc[4];
    uint32_t  stack_depth;
    uint32_t  stack_preview_count;
    uint32_t  stack_preview[EPA_DBG_STACK_PREVIEW];
    uint32_t  inq_count;
    uint32_t  outq_count;
    int32_t   locals[EPA_DBG_LOCALS];
    uint32_t  lbytes_top;
    uint32_t  lbytes_cap;
    uint32_t  lscope_depth;
    uint64_t  current_ghs;
    EpaDbgEip eip;
    char      fault_message[256];
} EpaDbgWorkerSnapshot;

typedef struct {
    uint32_t prog_loaded;
    uint32_t rr_cursor;
    uint32_t current_wid;
    uint32_t interrupt_requested;
    uint32_t worker_count;
} EpaDbgKernelSnapshot;

typedef struct {
    uint32_t valid;
    uint32_t type;
    uint32_t owner;
    uint32_t flags;
    uint32_t size_bytes;
    uint32_t capacity;
    uint32_t generation;
    uint32_t preview_len;
    uint8_t  preview[EPA_DBG_GHS_PREVIEW];
} EpaDbgGhsInspect;

typedef struct {
    uint32_t wid;
    uint32_t halted;
    uint32_t blocked;
    uint32_t faulted;
    uint32_t waiting_for_data;
    uint32_t at_running;
    uint32_t inq_count;
    uint32_t outq_count;
    uint32_t csc[4];
    EpaDbgEip eip;
    uint32_t stack_depth;
    uint32_t stack_start;
    uint32_t stack_word_count;
    uint32_t stack_words[EPA_DBG_STACK_WORDS];
    int32_t  locals[EPA_DBG_LOCALS];
    uint32_t lbytes_top;
    uint32_t lbytes_cap;
    uint32_t lscope_depth;
    uint32_t arena_preview_from;
    uint32_t arena_preview_len;
    uint8_t  arena_preview[EPA_DBG_ARENA_PREVIEW];
    uint32_t has_current_ghs;
    uint64_t current_ghs;
    uint32_t ghs_live_count;
    uint32_t ghs_capacity;
    EpaDbgGhsInspect ghs;
    char fault_message[256];
} EpaDbgWorkerInspect;

int    epa_dbg_capture_kernel(EpaKernel *kernel, EpaDbgKernelSnapshot *out);
size_t epa_dbg_capture_workers(EpaKernel *kernel, EpaDbgWorkerSnapshot *out, size_t max_workers);
int    epa_dbg_any_worker_at(EpaKernel *kernel, uint8_t block_type, uint32_t block_id, uint32_t rel_pc, uint32_t *out_wid);
int    epa_dbg_capture_worker_inspect(EpaKernel *kernel, uint32_t wid, EpaDbgWorkerInspect *out,
                                      uint32_t stack_words_limit, uint32_t arena_bytes_limit, uint32_t ghs_bytes_limit);

#ifdef __cplusplus
}
#endif

#endif
