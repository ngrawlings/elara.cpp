// src/weights/hf_shape.c
#include "hf_shape.h"

#include <stdio.h>

static void seterr(char err[256], const char *fmt, const char *name, long a, long b) {
  if (!err) return;
  snprintf(err, 256, fmt, name ? name : "(null)", a, b);
}

size_t hf_shape_numel(const AtTensorView *t) {
  if (!t) return 0;
  if (t->rank == 0 || t->rank > 8) return 0;
  size_t n = 1;
  for (uint32_t i = 0; i < t->rank; i++) {
    if (t->dims[i] <= 0) return 0;
    // overflow check
    size_t di = (size_t)t->dims[i];
    if (n > (SIZE_MAX / di)) return 0;
    n *= di;
  }
  return n;
}

int hf_shape_require_f32(const AtTensorView *t, const char *name, char err[256]) {
  if (err) err[0] = 0;
  if (!t) { if (err) snprintf(err, 256, "%s: tensor is NULL", name); return -1; }
  if (t->dtype != AT_DTYPE_F32) {
    if (err) snprintf(err, 256, "%s: expected F32 dtype (got %u)", name, (unsigned)t->dtype);
    return -1;
  }
  return 0;
}

int hf_shape_require_rank(const AtTensorView *t, const char *name, uint32_t rank, char err[256]) {
  if (err) err[0] = 0;
  if (!t) { if (err) snprintf(err, 256, "%s: tensor is NULL", name); return -1; }
  if (t->rank != rank) {
    if (err) snprintf(err, 256, "%s: expected rank %u (got %u)", name, rank, t->rank);
    return -1;
  }
  return 0;
}

int hf_shape_require_dims(const AtTensorView *t, const char *name,
                          uint32_t rank, const int64_t *dims, char err[256]) {
  if (err) err[0] = 0;
  if (!t) { if (err) snprintf(err, 256, "%s: tensor is NULL", name); return -1; }
  if (t->rank != rank) {
    if (err) snprintf(err, 256, "%s: expected rank %u (got %u)", name, rank, t->rank);
    return -1;
  }
  for (uint32_t i = 0; i < rank; i++) {
    if (t->dims[i] != dims[i]) {
      if (err) snprintf(err, 256, "%s: dim[%u] expected %lld got %lld",
                        name, (unsigned)i, (long long)dims[i], (long long)t->dims[i]);
      return -1;
    }
  }
  return 0;
}
