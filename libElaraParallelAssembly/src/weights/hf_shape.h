// src/weights/hf_shape.h
#pragma once

#include "../atomic_tasks/at_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// Validate dtype == F32 for v1 pointer table.
// Later you can expand (F16/BF16) and keep pointer types as void*.
int hf_shape_require_f32(const AtTensorView *t, const char *name, char err[256]);

int hf_shape_require_rank(const AtTensorView *t, const char *name, uint32_t rank, char err[256]);

// Require exact dims for rank<=8
int hf_shape_require_dims(const AtTensorView *t, const char *name,
                          uint32_t rank, const int64_t *dims, char err[256]);

// Utility: compute number of elements (returns 0 on overflow/invalid)
size_t hf_shape_numel(const AtTensorView *t);

#ifdef __cplusplus
}
#endif
