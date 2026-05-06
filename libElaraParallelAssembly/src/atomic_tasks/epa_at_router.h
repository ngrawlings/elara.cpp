#pragma once
#include <stdint.h>
#include "../vm/epa_worker_state.h"
#include "../atomic_tasks/epa_atomic_tasks.h"
#include "../memory/epa_ghs.h"
#include "../epa_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

// AT entry signature you specified
typedef int (*EpaAtExecEntry)(
    EpaAtBatch *b,
    uint32_t    vtid,
    int32_t     tid,
    epa_ghs_t  *ghs,
    epa_ghs_handle_t h
);

// register subroutine id -> entry
int epa_at_router_register(uint32_t sub_id, EpaAtExecEntry fn);

// launch a batch for a worker
int epa_at_router_launch_parallel(
    EpaKernel      *k,
    EpaWorkerState *w,
    uint32_t               sub_id,
    epa_ghs_t             *ghs,
    epa_ghs_handle_t       h,
    uint32_t               thread_count,
    char                   err[EPA_MAX_ERR]
);

int epa_at_router_update_worker(EpaWorkerState *w, char err[EPA_MAX_ERR]);

#ifdef __cplusplus
}
#endif
