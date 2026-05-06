// src/atomic_tasks/at_rmsnorm_final.c
#include "at_rmsnorm_final.h"

#include <stddef.h>
#include <string.h>
#include <math.h>

static int at_bounds_ok(uint32_t off, uint32_t bytes_needed, uint32_t cap) {
    // Avoid overflow: require off <= cap and bytes_needed <= cap-off
    if (off > cap) return 0;
    if (bytes_needed > (cap - off)) return 0;
    return 1;
}

int at_rmsnorm_final_run(epa_ghs_t *ghs, epa_ghs_handle_t h) {
    if (!ghs) return AT_RMSNORM_FINAL_ERR_NULL;

    epa_ghs_meta_t meta;
    if (epa_ghs_get_meta(ghs, h, &meta) != EPA_GHS_OK) {
        return AT_RMSNORM_FINAL_ERR_GHS;
    }

    void *base = NULL;
    if (epa_ghs_get_ptr(ghs, h, &base) != EPA_GHS_OK || !base) {
        return AT_RMSNORM_FINAL_ERR_GHS;
    }

    if (meta.capacity < sizeof(AtRmsnormFinalHdr_v1)) {
        return AT_RMSNORM_FINAL_ERR_HDR;
    }

    AtRmsnormFinalHdr_v1 *hdr = (AtRmsnormFinalHdr_v1*)base;

    if (hdr->magic != AT_RMSNORM_FINAL_MAGIC || hdr->version != 1) {
        return AT_RMSNORM_FINAL_ERR_HDR;
    }

    const uint32_t flags = hdr->flags;

    // Mutually exclusive output modes
    const int inplace = (flags & AT_RMSNORM_FINAL_F_INPLACE_X) != 0;
    const int oop     = (flags & AT_RMSNORM_FINAL_F_OUT_OF_PLACE) != 0;
    if (inplace && oop) return AT_RMSNORM_FINAL_ERR_HDR;
    if (!inplace && !oop) {
        // Default to inplace if user forgot flags (keeps it forgiving)
        // but do not modify hdr->flags here; just treat as inplace.
    }

    const uint32_t d_model = hdr->d_model;
    const uint32_t n_rows  = hdr->n_rows;
    if (d_model == 0 || n_rows == 0) return AT_RMSNORM_FINAL_ERR_SHAPE;

    const uint32_t x_bytes = d_model * n_rows * (uint32_t)sizeof(float);
    const uint32_t w_bytes = d_model * (uint32_t)sizeof(float);

    if (!at_bounds_ok(hdr->x_off, x_bytes, meta.capacity)) return AT_RMSNORM_FINAL_ERR_BOUNDS;
    if (!at_bounds_ok(hdr->w_off, w_bytes, meta.capacity)) return AT_RMSNORM_FINAL_ERR_BOUNDS;

    uint8_t *p = (uint8_t*)base;
    float *x = (float*)(p + hdr->x_off);
    float *w = (float*)(p + hdr->w_off);

    float *y = x;
    if (oop || (!inplace && !oop)) {
        // if explicitly out-of-place, require y_off bounds
        if (!at_bounds_ok(hdr->y_off, x_bytes, meta.capacity)) return AT_RMSNORM_FINAL_ERR_BOUNDS;
        y = (float*)(p + hdr->y_off);
    }

    const float eps = hdr->eps;

    for (uint32_t r = 0; r < n_rows; r++) {
        float *xr = x + (size_t)r * d_model;
        float *yr = y + (size_t)r * d_model;

        // mean(x^2)
        float sumsq = 0.0f;
        for (uint32_t i = 0; i < d_model; i++) {
            float v = xr[i];
            sumsq += v * v;
        }

        float mean = sumsq / (float)d_model;
        float inv_rms = 1.0f / sqrtf(mean + eps);

        for (uint32_t i = 0; i < d_model; i++) {
            yr[i] = xr[i] * inv_rms * w[i];
        }
    }

    return AT_RMSNORM_FINAL_OK;
}
