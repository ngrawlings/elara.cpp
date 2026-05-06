// src/atomic_tasks/at_residual_add.h
#pragma once
#include <stdint.h>

#include "../memory/epa_ghs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AT_RESID_OK   0
#define AT_RESID_ERR -1

// Flags
#define AT_RESID_F_NONE            0u
// If set, output is written into X buffer (in-place), and OUT region may be omitted.
#define AT_RESID_F_OUT_INPLACE_X   (1u << 0)
// If set, output is written into R buffer (in-place), and OUT region may be omitted.
#define AT_RESID_F_OUT_INPLACE_R   (1u << 1)

// Payload layout (v1):
//   [AtResidualAddHdr_v1]
//   [X float32]  count
//   [R float32]  count
//   [OUT float32] count    (only required if neither INPLACE flag is set)
//
// Semantics:
//   OUT[i] = X[i] + R[i]
typedef struct AtResidualAddHdr_v1 {
    uint32_t flags;
    uint32_t count;     // number of float elements
    uint32_t rsv0;
    uint32_t rsv1;
} AtResidualAddHdr_v1;

int at_residual_add_run(epa_ghs_t *ghs, epa_ghs_handle_t h);

#ifdef __cplusplus
}
#endif
