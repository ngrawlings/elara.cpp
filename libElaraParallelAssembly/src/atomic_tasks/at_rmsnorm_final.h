// src/atomic_tasks/at_rmsnorm_final.h
#pragma once

#include <stdint.h>
#include "../memory/epa_ghs.h"

#ifdef __cplusplus
extern "C" {
#endif

// "RMSF" little-endian marker
#define AT_RMSNORM_FINAL_MAGIC 0x46534D52u

typedef enum {
    AT_RMSNORM_FINAL_OK = 0,
    AT_RMSNORM_FINAL_ERR_NULL = -1,
    AT_RMSNORM_FINAL_ERR_GHS  = -2,
    AT_RMSNORM_FINAL_ERR_HDR  = -3,
    AT_RMSNORM_FINAL_ERR_BOUNDS = -4,
    AT_RMSNORM_FINAL_ERR_SHAPE  = -5,
} AtRmsnormFinalRc;

enum {
    // Writes output back into X (default / typical)
    AT_RMSNORM_FINAL_F_INPLACE_X = 1u << 0,

    // If set, write to Y (y_off) instead of inplace.
    // If this is set, INPLACE_X must be clear.
    AT_RMSNORM_FINAL_F_OUT_OF_PLACE = 1u << 1,
};

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;     // AT_RMSNORM_FINAL_MAGIC
    uint16_t version;   // 1
    uint16_t flags;     // AT_RMSNORM_FINAL_F_*

    float    eps;       // RMSNorm epsilon

    uint32_t d_model;   // hidden size (elements per row)
    uint32_t n_rows;    // number of rows (tokens). decode=1, prefill=seq_len

    // Byte offsets from payload base:
    uint32_t x_off;     // float[d_model*n_rows]
    uint32_t w_off;     // float[d_model]  (scale / gamma)
    uint32_t y_off;     // float[d_model*n_rows] (only if OUT_OF_PLACE)
} AtRmsnormFinalHdr_v1;
#pragma pack(pop)

// Runs RMSNorm: y = x * (1/sqrt(mean(x^2)+eps)) * w
int at_rmsnorm_final_run(epa_ghs_t *ghs, epa_ghs_handle_t h);

#ifdef __cplusplus
}
#endif
