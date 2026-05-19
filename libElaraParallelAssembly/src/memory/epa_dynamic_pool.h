#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EPA_DYNAMIC_NULL 0xffffffffu

typedef struct {
  uint32_t id;
  uint32_t segment_index;
  uint32_t next_live;
  uint32_t prev_live;
  uint32_t next_free;
  uint32_t prev_free;
  uint8_t is_live;
  uint8_t is_free;
} EpaDynamicSlot;

typedef struct {
  uint32_t index;
  uint32_t start_id;
  uint32_t slot_count;
  uint8_t present;
} EpaDynamicSegment;

typedef struct {
  uint32_t min_free;
  uint32_t max_free;
  uint32_t grow_by;

  /* Main header metadata contract. */
  uint32_t active_count;
  uint32_t free_count;
  uint32_t live_head;
  uint32_t live_tail;
  uint32_t free_head;

  EpaDynamicSlot *slots;
  uint32_t slot_cap;
  uint32_t slot_count;

  EpaDynamicSegment *segments;
  uint32_t segment_cap;
  uint32_t segment_count;
  uint32_t first_present_segment;
  uint32_t last_present_segment;
} EpaDynamicPool;

int epa_dynamic_pool_init(EpaDynamicPool *pool, uint32_t min_free, uint32_t max_free,
                          uint32_t grow_by, char err[256]);
void epa_dynamic_pool_free(EpaDynamicPool *pool);

int epa_dynamic_pool_round_enter(EpaDynamicPool *pool, char err[256]);
int epa_dynamic_pool_alloc(EpaDynamicPool *pool, uint32_t *out_id, char err[256]);
int epa_dynamic_pool_release(EpaDynamicPool *pool, uint32_t id, char err[256]);

EpaDynamicSlot *epa_dynamic_pool_slot(EpaDynamicPool *pool, uint32_t id);
uint32_t epa_dynamic_pool_collect_live(EpaDynamicPool *pool, uint32_t *out_ids, uint32_t cap);
int epa_dynamic_pool_validate(const EpaDynamicPool *pool, char err[256]);

#ifdef __cplusplus
}
#endif
