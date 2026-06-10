#ifndef ELARAOSEPADEBUGSHIM_H
#define ELARAOSEPADEBUGSHIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EpaKernel EpaKernel;
#define ELARAOS_EPA_DEBUG_MAX_WORKERS 256
#define ELARAOS_EPA_DEBUG_STACK_PREVIEW 8
#define ELARAOS_EPA_DEBUG_LOCALS 32

typedef struct {
  uint8_t block_type;
  uint16_t block_id;
  uint32_t rel_pc;
} ElaraOsEpaDebugEip;

typedef struct {
  uint32_t wid;
  uint32_t active;
  uint32_t inited;
  uint32_t retired;
  uint32_t halted;
  uint32_t blocked;
  uint32_t faulted;
  uint32_t waiting_for_data;
  uint32_t at_running;
  uint32_t has_current_ghs;
  uint32_t csc[4];
  uint32_t stack_depth;
  uint32_t stack_preview_count;
  uint32_t stack_preview[ELARAOS_EPA_DEBUG_STACK_PREVIEW];
  uint32_t inq_count;
  uint32_t outq_count;
  int32_t locals[ELARAOS_EPA_DEBUG_LOCALS];
  uint32_t lbytes_top;
  uint32_t lbytes_cap;
  uint32_t lscope_depth;
  uint64_t current_ghs;
  ElaraOsEpaDebugEip eip;
} ElaraOsEpaDebugWorkerSnapshot;

typedef struct {
  uint32_t prog_loaded;
  uint32_t rr_cursor;
  uint32_t current_wid;
  uint32_t interrupt_requested;
  uint32_t worker_count;
} ElaraOsEpaDebugKernelSnapshot;

int ElaraOs_epa_debug_capture_kernel(EpaKernel *kernel, ElaraOsEpaDebugKernelSnapshot *out_snapshot);
size_t ElaraOs_epa_debug_capture_workers(EpaKernel *kernel, ElaraOsEpaDebugWorkerSnapshot *out_workers, size_t max_workers);
int ElaraOs_epa_debug_any_worker_at(EpaKernel *kernel, uint8_t block_type, uint16_t block_id, uint32_t rel_pc, uint32_t *out_wid);

#ifdef __cplusplus
}
#endif

#endif
