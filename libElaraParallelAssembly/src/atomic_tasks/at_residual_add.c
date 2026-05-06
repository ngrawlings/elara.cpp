// src/atomic_tasks/at_residual_add.c
#include "at_residual_add.h"

#include <stddef.h>
#include <string.h>

int at_residual_add_run(epa_ghs_t *ghs, epa_ghs_handle_t h) {
    if (!ghs) return AT_RESID_ERR;

    void *base = NULL;
    epa_ghs_meta_t meta;
    if (epa_ghs_get_meta(ghs, h, &meta) != EPA_GHS_OK) return AT_RESID_ERR;
    if (epa_ghs_get_ptr(ghs, h, &base) != EPA_GHS_OK)  return AT_RESID_ERR;
    if (!base) return AT_RESID_ERR;

    if (meta.size_bytes < (uint32_t)sizeof(AtResidualAddHdr_v1)) return AT_RESID_ERR;

    uint8_t *p = (uint8_t *)base;
    AtResidualAddHdr_v1 *hdr = (AtResidualAddHdr_v1 *)p;

    const uint32_t flags = hdr->flags;
    const uint32_t count = hdr->count;
    if (count == 0) return AT_RESID_ERR;

    // Compute required bytes
    const size_t x_bytes = (size_t)count * sizeof(float);
    const size_t r_bytes = (size_t)count * sizeof(float);

    size_t bytes_needed = sizeof(AtResidualAddHdr_v1) + x_bytes + r_bytes;

    const int out_inplace_x = (flags & AT_RESID_F_OUT_INPLACE_X) != 0;
    const int out_inplace_r = (flags & AT_RESID_F_OUT_INPLACE_R) != 0;

    // Disallow both in-place flags at once (ambiguous target)
    if (out_inplace_x && out_inplace_r) return AT_RESID_ERR;

    if (!out_inplace_x && !out_inplace_r) {
        bytes_needed += (size_t)count * sizeof(float); // OUT region required
    }

    if ((size_t)meta.size_bytes < bytes_needed) return AT_RESID_ERR;

    p += sizeof(AtResidualAddHdr_v1);

    float *X = (float *)p; p += x_bytes;
    float *R = (float *)p; p += r_bytes;

    float *OUT = NULL;
    if (out_inplace_x) OUT = X;
    else if (out_inplace_r) OUT = R;
    else OUT = (float *)p;

    // OUT[i] = X[i] + R[i]
    for (uint32_t i = 0; i < count; i++) {
        OUT[i] = X[i] + R[i];
    }

    return AT_RESID_OK;
}
