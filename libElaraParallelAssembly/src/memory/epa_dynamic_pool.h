#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t  *data;
  uint32_t  count;
  uint32_t  cap;
  uint32_t  element_size;
  uint32_t  min_free;
  uint32_t  max_free;
  uint32_t  grow_by;
} EpaDynamicPool;

int  epa_dynamic_pool_init(EpaDynamicPool *pool, uint32_t min_free, uint32_t max_free,
                           uint32_t grow_by, uint32_t element_size, char err[256]);
void epa_dynamic_pool_free(EpaDynamicPool *pool);

int  epa_dynamic_pool_round_enter(EpaDynamicPool *pool, char err[256]);
int  epa_dynamic_pool_alloc(EpaDynamicPool *pool, uint32_t *out_ordinal, char err[256]);
int  epa_dynamic_pool_release(EpaDynamicPool *pool, uint32_t ordinal, char err[256]);
int  epa_dynamic_pool_read(const EpaDynamicPool *pool, uint32_t ordinal, void *dst, uint32_t dst_len, char err[256]);
int  epa_dynamic_pool_write(EpaDynamicPool *pool, uint32_t ordinal, const void *src, uint32_t src_len, char err[256]);
int  epa_dynamic_pool_swap(EpaDynamicPool *pool, uint32_t ord_a, uint32_t ord_b, char err[256]);
int  epa_dynamic_pool_validate(const EpaDynamicPool *pool, char err[256]);

#ifdef __cplusplus
}
#endif
