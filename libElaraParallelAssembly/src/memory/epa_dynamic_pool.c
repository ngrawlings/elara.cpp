#include "epa_dynamic_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char err[256], const char *msg)
{
  if (err) snprintf(err, 256, "%s", msg);
}

static int dyn_ensure_cap(EpaDynamicPool *pool, uint32_t need, char err[256])
{
  uint32_t next_cap;
  uint8_t *next;
  if (need <= pool->cap) return 1;
  next_cap = pool->cap ? pool->cap : 16u;
  while (next_cap < need) next_cap *= 2u;
  next = (uint8_t*)realloc(pool->data, (size_t)next_cap * (size_t)pool->element_size);
  if (!next) {
    set_err(err, "epa_dynamic_pool: realloc failed");
    return 0;
  }
  memset(next + (size_t)pool->cap * (size_t)pool->element_size, 0,
         (size_t)(next_cap - pool->cap) * (size_t)pool->element_size);
  pool->data = next;
  pool->cap  = next_cap;
  return 1;
}

int epa_dynamic_pool_init(EpaDynamicPool *pool, uint32_t min_free, uint32_t max_free,
                          uint32_t grow_by, uint32_t element_size, char err[256])
{
  if (!pool) { set_err(err, "epa_dynamic_pool_init: null pool"); return 0; }
  if (grow_by == 0u) { set_err(err, "epa_dynamic_pool_init: grow_by must be > 0"); return 0; }
  if (min_free > max_free) { set_err(err, "epa_dynamic_pool_init: min_free must be <= max_free"); return 0; }
  memset(pool, 0, sizeof(*pool));
  pool->min_free    = min_free;
  pool->max_free    = max_free;
  pool->grow_by     = grow_by;
  pool->element_size = element_size;
  if (err) err[0] = 0;
  return 1;
}

void epa_dynamic_pool_free(EpaDynamicPool *pool)
{
  if (!pool) return;
  free(pool->data);
  memset(pool, 0, sizeof(*pool));
}

int epa_dynamic_pool_round_enter(EpaDynamicPool *pool, char err[256])
{
  uint32_t free_slots;
  if (!pool) { set_err(err, "epa_dynamic_pool_round_enter: null pool"); return 0; }
  free_slots = pool->cap - pool->count;
  if (free_slots < pool->min_free) {
    uint32_t need = pool->count + pool->min_free;
    uint32_t next_cap = pool->cap ? pool->cap : pool->grow_by;
    while (next_cap < need) next_cap += pool->grow_by;
    if (!dyn_ensure_cap(pool, next_cap, err)) return 0;
  } else if (free_slots > pool->max_free) {
    uint32_t new_cap = pool->count + pool->min_free;
    uint8_t *next = (uint8_t*)realloc(pool->data, (size_t)new_cap * (size_t)pool->element_size);
    if (next || new_cap == 0u) {
      pool->data = next;
      pool->cap  = new_cap;
    }
  }
  return 1;
}

int epa_dynamic_pool_request_capacity(EpaDynamicPool *pool, uint32_t requested_cap,
                                      int hard_order, char err[256])
{
  uint8_t *next;
  if (!pool) { set_err(err, "epa_dynamic_pool_request_capacity: null pool"); return 0; }

  if (requested_cap < pool->count) {
    if (hard_order) {
      set_err(err, "epa_dynamic_pool_request_capacity: requested capacity below live count");
      return 0;
    }
    return 1;
  }

  if (requested_cap == pool->cap) return 1;

  if (requested_cap > pool->cap) {
    return dyn_ensure_cap(pool, requested_cap, err);
  }

  next = (uint8_t*)realloc(pool->data, (size_t)requested_cap * (size_t)pool->element_size);
  if (!next && requested_cap != 0u) {
    if (hard_order) {
      set_err(err, "epa_dynamic_pool_request_capacity: shrink realloc failed");
      return 0;
    }
    return 1;
  }
  pool->data = next;
  pool->cap = requested_cap;
  return 1;
}

int epa_dynamic_pool_alloc(EpaDynamicPool *pool, uint32_t *out_ordinal, char err[256])
{
  if (!pool || !out_ordinal) { set_err(err, "epa_dynamic_pool_alloc: bad args"); return 0; }
  if (pool->count >= pool->cap) {
    set_err(err, "epa_dynamic_pool_alloc: pool full");
    return 0;
  }
  if (pool->element_size)
    memset(pool->data + (size_t)pool->count * (size_t)pool->element_size, 0, pool->element_size);
  *out_ordinal = pool->count++;
  return 1;
}

int epa_dynamic_pool_release(EpaDynamicPool *pool, uint32_t ordinal, char err[256])
{
  if (!pool) { set_err(err, "epa_dynamic_pool_release: null pool"); return 0; }
  if (ordinal >= pool->count) {
    set_err(err, "epa_dynamic_pool_release: ordinal out of range");
    return 0;
  }
  pool->count--;
  if (ordinal != pool->count && pool->element_size) {
    memcpy(pool->data + (size_t)ordinal  * (size_t)pool->element_size,
           pool->data + (size_t)pool->count * (size_t)pool->element_size,
           pool->element_size);
  }
  return 1;
}

int epa_dynamic_pool_read(const EpaDynamicPool *pool, uint32_t ordinal,
                          void *dst, uint32_t dst_len, char err[256])
{
  if (!pool || !dst) { set_err(err, "epa_dynamic_pool_read: bad args"); return 0; }
  if (dst_len != pool->element_size) { set_err(err, "epa_dynamic_pool_read: size mismatch"); return 0; }
  if (ordinal >= pool->count) { set_err(err, "epa_dynamic_pool_read: ordinal out of range"); return 0; }
  if (pool->element_size)
    memcpy(dst, pool->data + (size_t)ordinal * (size_t)pool->element_size, pool->element_size);
  return 1;
}

int epa_dynamic_pool_write(EpaDynamicPool *pool, uint32_t ordinal,
                           const void *src, uint32_t src_len, char err[256])
{
  if (!pool || !src) { set_err(err, "epa_dynamic_pool_write: bad args"); return 0; }
  if (src_len != pool->element_size) { set_err(err, "epa_dynamic_pool_write: size mismatch"); return 0; }
  if (ordinal >= pool->count) { set_err(err, "epa_dynamic_pool_write: ordinal out of range"); return 0; }
  if (pool->element_size)
    memcpy(pool->data + (size_t)ordinal * (size_t)pool->element_size, src, pool->element_size);
  return 1;
}

int epa_dynamic_pool_swap(EpaDynamicPool *pool, uint32_t ord_a, uint32_t ord_b, char err[256])
{
  uint8_t tmp[512];
  uint8_t *a;
  uint8_t *b;
  if (!pool) { set_err(err, "epa_dynamic_pool_swap: null pool"); return 0; }
  if (ord_a == ord_b) return 1;
  if (ord_a >= pool->count || ord_b >= pool->count) {
    set_err(err, "epa_dynamic_pool_swap: ordinal out of range");
    return 0;
  }
  if (pool->element_size > sizeof(tmp)) {
    set_err(err, "epa_dynamic_pool_swap: element_size exceeds swap buffer");
    return 0;
  }
  a = pool->data + (size_t)ord_a * (size_t)pool->element_size;
  b = pool->data + (size_t)ord_b * (size_t)pool->element_size;
  memcpy(tmp, a,   pool->element_size);
  memcpy(a,   b,   pool->element_size);
  memcpy(b,   tmp, pool->element_size);
  return 1;
}

int epa_dynamic_pool_validate(const EpaDynamicPool *pool, char err[256])
{
  if (!pool) { set_err(err, "epa_dynamic_pool_validate: null pool"); return 0; }
  if (pool->count > pool->cap) {
    set_err(err, "epa_dynamic_pool_validate: count exceeds cap");
    return 0;
  }
  if (pool->count > 0u && pool->element_size > 0u && !pool->data) {
    set_err(err, "epa_dynamic_pool_validate: data is null but elements are live");
    return 0;
  }
  if (err) err[0] = 0;
  return 1;
}
