#include "epa_dynamic_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char err[256], const char *msg)
{
  if (err) snprintf(err, 256, "%s", msg);
}

static void set_errf(char err[256], const char *fmt, uint32_t a, uint32_t b)
{
  if (err) snprintf(err, 256, fmt, a, b);
}

static uint32_t dyn_vtable_stride_for_level(uint32_t level)
{
  uint32_t stride = 10u;
  while (level > 0u) {
    stride *= 10u;
    level--;
  }
  return stride;
}

static int dyn_vtable_reserve(EpaDynamicOrdinalVTable *table, uint32_t need, char err[256])
{
  uint32_t next_cap;
  uint32_t *next;
  if (need <= table->count) return 1;
  next_cap = table->count ? table->count : 16u;
  while (next_cap < need) next_cap *= 2u;
  next = (uint32_t*)realloc(table->ids, sizeof(uint32_t) * next_cap);
  if (!next) {
    set_err(err, "epa_dynamic_pool: ordinal vtable realloc failed");
    return 0;
  }
  table->ids = next;
  table->count = next_cap;
  return 1;
}

static int dyn_slots_reserve(EpaDynamicPool *pool, uint32_t need, char err[256])
{
  uint32_t next_cap;
  EpaDynamicSlot *next;
  uint8_t *next_data = NULL;
  if (need <= pool->slot_cap) return 1;
  next_cap = pool->slot_cap ? pool->slot_cap : 16u;
  while (next_cap < need) next_cap *= 2u;
  next = (EpaDynamicSlot*)realloc(pool->slots, sizeof(EpaDynamicSlot) * next_cap);
  if (!next) {
    set_err(err, "epa_dynamic_pool: slot realloc failed");
    return 0;
  }
  pool->slots = next;
  if (pool->element_size != 0u) {
    next_data = (uint8_t*)realloc(pool->slot_data, (size_t)next_cap * (size_t)pool->element_size);
    if (!next_data) {
      set_err(err, "epa_dynamic_pool: slot data realloc failed");
      return 0;
    }
    pool->slot_data = next_data;
    memset(pool->slot_data + ((size_t)pool->slot_cap * (size_t)pool->element_size), 0,
           (size_t)(next_cap - pool->slot_cap) * (size_t)pool->element_size);
  }
  memset(pool->slots + pool->slot_cap, 0, sizeof(EpaDynamicSlot) * (next_cap - pool->slot_cap));
  pool->slot_cap = next_cap;
  return 1;
}

static int dyn_segments_reserve(EpaDynamicPool *pool, uint32_t need, char err[256])
{
  uint32_t next_cap;
  EpaDynamicSegment *next;
  if (need <= pool->segment_cap) return 1;
  next_cap = pool->segment_cap ? pool->segment_cap : 8u;
  while (next_cap < need) next_cap *= 2u;
  next = (EpaDynamicSegment*)realloc(pool->segments, sizeof(EpaDynamicSegment) * next_cap);
  if (!next) {
    set_err(err, "epa_dynamic_pool: segment realloc failed");
    return 0;
  }
  pool->segments = next;
  memset(pool->segments + pool->segment_cap, 0,
         sizeof(EpaDynamicSegment) * (next_cap - pool->segment_cap));
  pool->segment_cap = next_cap;
  return 1;
}

EpaDynamicSlot *epa_dynamic_pool_slot(EpaDynamicPool *pool, uint32_t id)
{
  if (!pool || id == EPA_DYNAMIC_NULL || id >= pool->slot_count) return NULL;
  return &pool->slots[id];
}

static uint8_t *dyn_slot_data(EpaDynamicPool *pool, uint32_t id)
{
  if (!pool || !pool->slot_data || id >= pool->slot_count) return NULL;
  return pool->slot_data + ((size_t)id * (size_t)pool->element_size);
}

static int dyn_rebuild_ord_vtables(EpaDynamicPool *pool, char err[256])
{
  uint32_t level;
  uint32_t ordinal = 0u;
  uint32_t id;
  if (!pool) {
    set_err(err, "epa_dynamic_pool: null pool in ordinal vtable rebuild");
    return 0;
  }
  for (level = 0; level < EPA_DYNAMIC_VTABLE_LEVELS; level++) {
    uint32_t stride = pool->ord_vtables[level].stride;
    uint32_t need = (pool->active_count == 0u) ? 0u : (pool->active_count - 1u) / stride + 1u;
    if (need != 0u && !dyn_vtable_reserve(&pool->ord_vtables[level], need, err)) return 0;
  }
  id = pool->live_head;
  while (id != EPA_DYNAMIC_NULL) {
    EpaDynamicSlot *slot = epa_dynamic_pool_slot(pool, id);
    if (!slot) {
      set_err(err, "epa_dynamic_pool: invalid live slot during ordinal vtable rebuild");
      return 0;
    }
    for (level = 0; level < EPA_DYNAMIC_VTABLE_LEVELS; level++) {
      uint32_t stride = pool->ord_vtables[level].stride;
      if ((ordinal % stride) == 0u) {
        pool->ord_vtables[level].ids[ordinal / stride] = id;
      }
    }
    ordinal++;
    id = slot->next_live;
  }
  return 1;
}

static int dyn_free_prepend(EpaDynamicPool *pool, uint32_t id, char err[256])
{
  EpaDynamicSlot *slot = epa_dynamic_pool_slot(pool, id);
  if (!slot) {
    set_errf(err, "epa_dynamic_pool: invalid free prepend slot %u/%u", id, pool->slot_count);
    return 0;
  }
  if (!slot->is_free) {
    set_err(err, "epa_dynamic_pool: free prepend requires free slot");
    return 0;
  }
  slot->prev_free = EPA_DYNAMIC_NULL;
  slot->next_free = pool->free_head;
  if (pool->free_head != EPA_DYNAMIC_NULL) {
    EpaDynamicSlot *head = epa_dynamic_pool_slot(pool, pool->free_head);
    if (!head) {
      set_err(err, "epa_dynamic_pool: invalid free_head");
      return 0;
    }
    head->prev_free = id;
  }
  pool->free_head = id;
  pool->free_count++;
  return 1;
}

static int dyn_free_remove(EpaDynamicPool *pool, uint32_t id, char err[256])
{
  EpaDynamicSlot *slot = epa_dynamic_pool_slot(pool, id);
  if (!slot) {
    set_err(err, "epa_dynamic_pool: invalid free remove slot");
    return 0;
  }
  if (!slot->is_free) {
    set_err(err, "epa_dynamic_pool: free remove requires free slot");
    return 0;
  }
  if (slot->prev_free != EPA_DYNAMIC_NULL) {
    EpaDynamicSlot *prev = epa_dynamic_pool_slot(pool, slot->prev_free);
    if (!prev) {
      set_err(err, "epa_dynamic_pool: invalid prev_free");
      return 0;
    }
    prev->next_free = slot->next_free;
  } else {
    if (pool->free_head != id) {
      set_err(err, "epa_dynamic_pool: free_head mismatch");
      return 0;
    }
    pool->free_head = slot->next_free;
  }
  if (slot->next_free != EPA_DYNAMIC_NULL) {
    EpaDynamicSlot *next = epa_dynamic_pool_slot(pool, slot->next_free);
    if (!next) {
      set_err(err, "epa_dynamic_pool: invalid next_free");
      return 0;
    }
    next->prev_free = slot->prev_free;
  }
  slot->next_free = EPA_DYNAMIC_NULL;
  slot->prev_free = EPA_DYNAMIC_NULL;
  pool->free_count--;
  return 1;
}

static int dyn_live_append(EpaDynamicPool *pool, uint32_t id, char err[256])
{
  EpaDynamicSlot *slot = epa_dynamic_pool_slot(pool, id);
  if (!slot) {
    set_err(err, "epa_dynamic_pool: invalid live append slot");
    return 0;
  }
  if (!slot->is_live) {
    set_err(err, "epa_dynamic_pool: live append requires live slot");
    return 0;
  }
  slot->next_live = EPA_DYNAMIC_NULL;
  slot->prev_live = pool->live_tail;
  if (pool->live_tail != EPA_DYNAMIC_NULL) {
    EpaDynamicSlot *tail = epa_dynamic_pool_slot(pool, pool->live_tail);
    if (!tail) {
      set_err(err, "epa_dynamic_pool: invalid live_tail");
      return 0;
    }
    tail->next_live = id;
  } else {
    pool->live_head = id;
  }
  pool->live_tail = id;
  pool->active_count++;
  return 1;
}

static int dyn_live_remove(EpaDynamicPool *pool, uint32_t id, char err[256])
{
  EpaDynamicSlot *slot = epa_dynamic_pool_slot(pool, id);
  if (!slot) {
    set_err(err, "epa_dynamic_pool: invalid live remove slot");
    return 0;
  }
  if (!slot->is_live) {
    set_err(err, "epa_dynamic_pool: live remove requires live slot");
    return 0;
  }
  if (slot->prev_live != EPA_DYNAMIC_NULL) {
    EpaDynamicSlot *prev = epa_dynamic_pool_slot(pool, slot->prev_live);
    if (!prev) {
      set_err(err, "epa_dynamic_pool: invalid prev_live");
      return 0;
    }
    prev->next_live = slot->next_live;
  } else {
    if (pool->live_head != id) {
      set_err(err, "epa_dynamic_pool: live_head mismatch");
      return 0;
    }
    pool->live_head = slot->next_live;
  }
  if (slot->next_live != EPA_DYNAMIC_NULL) {
    EpaDynamicSlot *next = epa_dynamic_pool_slot(pool, slot->next_live);
    if (!next) {
      set_err(err, "epa_dynamic_pool: invalid next_live");
      return 0;
    }
    next->prev_live = slot->prev_live;
  } else {
    if (pool->live_tail != id) {
      set_err(err, "epa_dynamic_pool: live_tail mismatch");
      return 0;
    }
    pool->live_tail = slot->prev_live;
  }
  slot->next_live = EPA_DYNAMIC_NULL;
  slot->prev_live = EPA_DYNAMIC_NULL;
  pool->active_count--;
  return 1;
}

static int dyn_pool_grow(EpaDynamicPool *pool, char err[256])
{
  uint32_t i;
  uint32_t seg_index = pool->segment_count;
  uint32_t start_id = pool->slot_count;

  if (!dyn_segments_reserve(pool, pool->segment_count + 1u, err)) return 0;
  if (!dyn_slots_reserve(pool, pool->slot_count + pool->grow_by, err)) return 0;

  pool->segments[seg_index].index = seg_index;
  pool->segments[seg_index].start_id = start_id;
  pool->segments[seg_index].slot_count = pool->grow_by;
  pool->segments[seg_index].present = 1u;
  pool->segment_count++;
  if (pool->first_present_segment == EPA_DYNAMIC_NULL) pool->first_present_segment = seg_index;
  pool->last_present_segment = seg_index;

  for (i = 0; i < pool->grow_by; i++) {
    EpaDynamicSlot *slot = &pool->slots[pool->slot_count + i];
    memset(slot, 0, sizeof(*slot));
    slot->id = pool->slot_count + i;
    slot->segment_index = seg_index;
    slot->next_live = EPA_DYNAMIC_NULL;
    slot->prev_live = EPA_DYNAMIC_NULL;
    slot->next_free = EPA_DYNAMIC_NULL;
    slot->prev_free = EPA_DYNAMIC_NULL;
    slot->is_live = 0u;
    slot->is_free = 1u;
  }
  pool->slot_count += pool->grow_by;
  for (i = 0; i < pool->grow_by; i++) {
    if (!dyn_free_prepend(pool, start_id + i, err)) return 0;
  }
  return 1;
}

static int dyn_segment_all_free(EpaDynamicPool *pool, uint32_t seg_index)
{
  uint32_t i;
  EpaDynamicSegment *seg = &pool->segments[seg_index];
  if (!seg->present) return 1;
  for (i = 0; i < seg->slot_count; i++) {
    EpaDynamicSlot *slot = epa_dynamic_pool_slot(pool, seg->start_id + i);
    if (!slot) return 0;
    if (slot->is_live) return 0;
    if (!slot->is_free) return 0;
  }
  return 1;
}

static int dyn_detach_segment_free_slots(EpaDynamicPool *pool, uint32_t seg_index, char err[256])
{
  uint32_t i;
  EpaDynamicSegment *seg = &pool->segments[seg_index];
  for (i = 0; i < seg->slot_count; i++) {
    EpaDynamicSlot *slot = epa_dynamic_pool_slot(pool, seg->start_id + i);
    if (!slot || !slot->is_free) {
      set_err(err, "epa_dynamic_pool: segment detach requires free slots");
      return 0;
    }
    if (!dyn_free_remove(pool, slot->id, err)) return 0;
    slot->is_free = 0u;
  }
  return 1;
}

static int dyn_pool_shrink_tail_segments(EpaDynamicPool *pool, char err[256])
{
  while (pool->free_count > pool->max_free && pool->last_present_segment != EPA_DYNAMIC_NULL) {
    EpaDynamicSegment *seg = &pool->segments[pool->last_present_segment];
    if (!seg->present) {
      if (pool->last_present_segment == 0u) pool->last_present_segment = EPA_DYNAMIC_NULL;
      else pool->last_present_segment--;
      continue;
    }
    if (!dyn_segment_all_free(pool, seg->index)) break;
    if (pool->free_count - seg->slot_count < pool->min_free) break;
    if (!dyn_detach_segment_free_slots(pool, seg->index, err)) return 0;
    seg->present = 0u;
    while (pool->last_present_segment != EPA_DYNAMIC_NULL &&
           pool->segments[pool->last_present_segment].present == 0u) {
      if (pool->last_present_segment == 0u) pool->last_present_segment = EPA_DYNAMIC_NULL;
      else pool->last_present_segment--;
    }
    while (pool->first_present_segment != EPA_DYNAMIC_NULL &&
           pool->first_present_segment < pool->segment_count &&
           pool->segments[pool->first_present_segment].present == 0u) {
      if (pool->first_present_segment + 1u >= pool->segment_count) {
        pool->first_present_segment = EPA_DYNAMIC_NULL;
        break;
      }
      pool->first_present_segment++;
    }
  }
  return 1;
}

int epa_dynamic_pool_init(EpaDynamicPool *pool, uint32_t min_free, uint32_t max_free,
                          uint32_t grow_by, uint32_t element_size, char err[256])
{
  if (!pool) {
    set_err(err, "epa_dynamic_pool_init: null pool");
    return 0;
  }
  if (grow_by == 0u) {
    set_err(err, "epa_dynamic_pool_init: grow_by must be > 0");
    return 0;
  }
  if (min_free > max_free) {
    set_err(err, "epa_dynamic_pool_init: min_free must be <= max_free");
    return 0;
  }
  memset(pool, 0, sizeof(*pool));
  pool->min_free = min_free;
  pool->max_free = max_free;
  pool->grow_by = grow_by;
  pool->element_size = element_size;
  for (uint32_t level = 0; level < EPA_DYNAMIC_VTABLE_LEVELS; level++) {
    pool->ord_vtables[level].stride = dyn_vtable_stride_for_level(level);
    pool->ord_vtables[level].ids = NULL;
    pool->ord_vtables[level].count = 0u;
  }
  pool->live_head = EPA_DYNAMIC_NULL;
  pool->live_tail = EPA_DYNAMIC_NULL;
  pool->free_head = EPA_DYNAMIC_NULL;
  pool->first_present_segment = EPA_DYNAMIC_NULL;
  pool->last_present_segment = EPA_DYNAMIC_NULL;
  if (err) err[0] = 0;
  return 1;
}

void epa_dynamic_pool_free(EpaDynamicPool *pool)
{
  if (!pool) return;
  free(pool->slots);
  free(pool->slot_data);
  free(pool->segments);
  for (uint32_t level = 0; level < EPA_DYNAMIC_VTABLE_LEVELS; level++) {
    free(pool->ord_vtables[level].ids);
  }
  memset(pool, 0, sizeof(*pool));
}

int epa_dynamic_pool_round_enter(EpaDynamicPool *pool, char err[256])
{
  if (!pool) {
    set_err(err, "epa_dynamic_pool_round_enter: null pool");
    return 0;
  }
  while (pool->free_count < pool->min_free) {
    if (!dyn_pool_grow(pool, err)) return 0;
  }
  if (!dyn_pool_shrink_tail_segments(pool, err)) return 0;
  return 1;
}

int epa_dynamic_pool_alloc(EpaDynamicPool *pool, uint32_t *out_id, char err[256])
{
  EpaDynamicSlot *slot;
  uint32_t id;
  if (!pool || !out_id) {
    set_err(err, "epa_dynamic_pool_alloc: bad args");
    return 0;
  }
  if (pool->free_head == EPA_DYNAMIC_NULL) {
    set_err(err, "epa_dynamic_pool_alloc: no prepared free slots");
    return 0;
  }
  id = pool->free_head;
  slot = epa_dynamic_pool_slot(pool, id);
  if (!slot) {
    set_err(err, "epa_dynamic_pool_alloc: invalid free_head");
    return 0;
  }
  if (!dyn_free_remove(pool, id, err)) return 0;
  slot->is_free = 0u;
  slot->is_live = 1u;
  if (!dyn_live_append(pool, id, err)) return 0;
  if (!dyn_rebuild_ord_vtables(pool, err)) return 0;
  *out_id = id;
  return 1;
}

int epa_dynamic_pool_release(EpaDynamicPool *pool, uint32_t id, char err[256])
{
  EpaDynamicSlot *slot;
  if (!pool) {
    set_err(err, "epa_dynamic_pool_release: null pool");
    return 0;
  }
  slot = epa_dynamic_pool_slot(pool, id);
  if (!slot) {
    set_err(err, "epa_dynamic_pool_release: invalid slot id");
    return 0;
  }
  if (!slot->is_live) {
    set_err(err, "epa_dynamic_pool_release: slot is not live");
    return 0;
  }
  if (!dyn_live_remove(pool, id, err)) return 0;
  slot->is_live = 0u;
  slot->is_free = 1u;
  if (!dyn_free_prepend(pool, id, err)) return 0;
  if (!dyn_rebuild_ord_vtables(pool, err)) return 0;
  return 1;
}

int epa_dynamic_pool_read(const EpaDynamicPool *pool, uint32_t id, void *dst, uint32_t dst_len, char err[256])
{
  const EpaDynamicSlot *slot;
  if (!pool || !dst) {
    set_err(err, "epa_dynamic_pool_read: bad args");
    return 0;
  }
  if (dst_len != pool->element_size) {
    set_err(err, "epa_dynamic_pool_read: size mismatch");
    return 0;
  }
  if (id == EPA_DYNAMIC_NULL || id >= pool->slot_count) {
    set_err(err, "epa_dynamic_pool_read: invalid slot id");
    return 0;
  }
  slot = &pool->slots[id];
  if (!slot->is_live) {
    set_err(err, "epa_dynamic_pool_read: slot is not live");
    return 0;
  }
  if (pool->element_size != 0u) {
    memcpy(dst, pool->slot_data + ((size_t)id * (size_t)pool->element_size), pool->element_size);
  }
  return 1;
}

int epa_dynamic_pool_write(EpaDynamicPool *pool, uint32_t id, const void *src, uint32_t src_len, char err[256])
{
  EpaDynamicSlot *slot;
  if (!pool || !src) {
    set_err(err, "epa_dynamic_pool_write: bad args");
    return 0;
  }
  if (src_len != pool->element_size) {
    set_err(err, "epa_dynamic_pool_write: size mismatch");
    return 0;
  }
  slot = epa_dynamic_pool_slot(pool, id);
  if (!slot) {
    set_err(err, "epa_dynamic_pool_write: invalid slot id");
    return 0;
  }
  if (!slot->is_live) {
    set_err(err, "epa_dynamic_pool_write: slot is not live");
    return 0;
  }
  if (pool->element_size != 0u) {
    uint8_t *dst = dyn_slot_data(pool, id);
    if (!dst) {
      set_err(err, "epa_dynamic_pool_write: missing slot storage");
      return 0;
    }
    memcpy(dst, src, pool->element_size);
  }
  return 1;
}

int epa_dynamic_pool_swap_live_order(EpaDynamicPool *pool, uint32_t id_a, uint32_t id_b, char err[256])
{
  EpaDynamicSlot *a;
  EpaDynamicSlot *b;
  uint32_t a_prev;
  uint32_t a_next;
  uint32_t b_prev;
  uint32_t b_next;
  if (!pool) {
    set_err(err, "epa_dynamic_pool_swap_live_order: null pool");
    return 0;
  }
  if (id_a == id_b) return 1;
  a = epa_dynamic_pool_slot(pool, id_a);
  b = epa_dynamic_pool_slot(pool, id_b);
  if (!a || !b) {
    set_err(err, "epa_dynamic_pool_swap_live_order: invalid slot id");
    return 0;
  }
  if (!a->is_live || !b->is_live) {
    set_err(err, "epa_dynamic_pool_swap_live_order: both slots must be live");
    return 0;
  }
  a_prev = a->prev_live;
  a_next = a->next_live;
  b_prev = b->prev_live;
  b_next = b->next_live;

  if (a_next == id_b) {
    a->prev_live = id_b;
    a->next_live = b_next;
    b->prev_live = a_prev;
    b->next_live = id_a;
    if (a_prev != EPA_DYNAMIC_NULL) pool->slots[a_prev].next_live = id_b;
    else pool->live_head = id_b;
    if (b_next != EPA_DYNAMIC_NULL) pool->slots[b_next].prev_live = id_a;
    else pool->live_tail = id_a;
    return dyn_rebuild_ord_vtables(pool, err);
  }
  if (b_next == id_a) {
    b->prev_live = id_a;
    b->next_live = a_next;
    a->prev_live = b_prev;
    a->next_live = id_b;
    if (b_prev != EPA_DYNAMIC_NULL) pool->slots[b_prev].next_live = id_a;
    else pool->live_head = id_a;
    if (a_next != EPA_DYNAMIC_NULL) pool->slots[a_next].prev_live = id_b;
    else pool->live_tail = id_b;
    return dyn_rebuild_ord_vtables(pool, err);
  }

  a->prev_live = b_prev;
  a->next_live = b_next;
  b->prev_live = a_prev;
  b->next_live = a_next;

  if (a_prev != EPA_DYNAMIC_NULL) pool->slots[a_prev].next_live = id_b;
  else pool->live_head = id_b;
  if (a_next != EPA_DYNAMIC_NULL) pool->slots[a_next].prev_live = id_b;
  else pool->live_tail = id_b;
  if (b_prev != EPA_DYNAMIC_NULL) pool->slots[b_prev].next_live = id_a;
  else pool->live_head = id_a;
  if (b_next != EPA_DYNAMIC_NULL) pool->slots[b_next].prev_live = id_a;
  else pool->live_tail = id_a;
  return dyn_rebuild_ord_vtables(pool, err);
}

int epa_dynamic_pool_live_id_at(const EpaDynamicPool *pool, uint32_t ordinal, uint32_t *out_id, char err[256])
{
  uint32_t level;
  uint32_t base_ordinal = 0u;
  uint32_t id = EPA_DYNAMIC_NULL;
  if (!pool || !out_id) {
    set_err(err, "epa_dynamic_pool_live_id_at: bad args");
    return 0;
  }
  if (ordinal >= pool->active_count) {
    set_err(err, "epa_dynamic_pool_live_id_at: ordinal out of range");
    return 0;
  }
  id = pool->live_head;
  for (level = EPA_DYNAMIC_VTABLE_LEVELS; level > 0u; level--) {
    uint32_t idx = level - 1u;
    uint32_t stride = pool->ord_vtables[idx].stride;
    uint32_t entry = ordinal / stride;
    if (entry == 0u) continue;
    if (entry >= pool->ord_vtables[idx].count) {
      set_err(err, "epa_dynamic_pool_live_id_at: ordinal vtable miss");
      return 0;
    }
    id = pool->ord_vtables[idx].ids[entry];
    base_ordinal = entry * stride;
    break;
  }
  while (base_ordinal < ordinal) {
    const EpaDynamicSlot *slot;
    if (id == EPA_DYNAMIC_NULL || id >= pool->slot_count) {
      set_err(err, "epa_dynamic_pool_live_id_at: broken live chain");
      return 0;
    }
    slot = &pool->slots[id];
    id = slot->next_live;
    base_ordinal++;
  }
  *out_id = id;
  return 1;
}

uint32_t epa_dynamic_pool_collect_live(EpaDynamicPool *pool, uint32_t *out_ids, uint32_t cap)
{
  uint32_t count = 0u;
  uint32_t id;
  if (!pool) return 0u;
  id = pool->live_head;
  while (id != EPA_DYNAMIC_NULL) {
    EpaDynamicSlot *slot = epa_dynamic_pool_slot(pool, id);
    if (!slot) break;
    if (count < cap && out_ids) out_ids[count] = id;
    count++;
    id = slot->next_live;
  }
  return count;
}

int epa_dynamic_pool_validate(const EpaDynamicPool *pool, char err[256])
{
  uint32_t i;
  uint32_t free_seen = 0u;
  uint32_t live_seen = 0u;
  uint32_t id;
  uint32_t last_live = EPA_DYNAMIC_NULL;
  uint8_t *free_marks;
  uint8_t *live_marks;
  if (!pool) {
    set_err(err, "epa_dynamic_pool_validate: null pool");
    return 0;
  }
  free_marks = (uint8_t*)calloc(pool->slot_count ? pool->slot_count : 1u, 1u);
  live_marks = (uint8_t*)calloc(pool->slot_count ? pool->slot_count : 1u, 1u);
  if (!free_marks || !live_marks) {
    free(free_marks);
    free(live_marks);
    set_err(err, "epa_dynamic_pool_validate: alloc failed");
    return 0;
  }

  id = pool->free_head;
  while (id != EPA_DYNAMIC_NULL) {
    const EpaDynamicSlot *slot;
    if (id >= pool->slot_count) {
      free(free_marks); free(live_marks);
      set_err(err, "epa_dynamic_pool_validate: free list id OOB");
      return 0;
    }
    slot = &pool->slots[id];
    if (!slot->is_free || slot->is_live || free_marks[id] != 0u) {
      free(free_marks); free(live_marks);
      set_err(err, "epa_dynamic_pool_validate: bad free slot state");
      return 0;
    }
    free_marks[id] = 1u;
    if (slot->next_free != EPA_DYNAMIC_NULL) {
      if (slot->next_free >= pool->slot_count ||
          pool->slots[slot->next_free].prev_free != id) {
        free(free_marks); free(live_marks);
        set_err(err, "epa_dynamic_pool_validate: free list link mismatch");
        return 0;
      }
    }
    free_seen++;
    id = slot->next_free;
  }
  if (free_seen != pool->free_count) {
    free(free_marks); free(live_marks);
    set_err(err, "epa_dynamic_pool_validate: free_count mismatch");
    return 0;
  }

  id = pool->live_head;
  while (id != EPA_DYNAMIC_NULL) {
    const EpaDynamicSlot *slot;
    if (id >= pool->slot_count) {
      free(free_marks); free(live_marks);
      set_err(err, "epa_dynamic_pool_validate: live list id OOB");
      return 0;
    }
    slot = &pool->slots[id];
    if (!slot->is_live || slot->is_free || live_marks[id] != 0u) {
      free(free_marks); free(live_marks);
      set_err(err, "epa_dynamic_pool_validate: bad live slot state");
      return 0;
    }
    live_marks[id] = 1u;
    if (slot->prev_live != last_live) {
      free(free_marks); free(live_marks);
      set_err(err, "epa_dynamic_pool_validate: live prev mismatch");
      return 0;
    }
    last_live = id;
    if (slot->next_live != EPA_DYNAMIC_NULL) {
      if (slot->next_live >= pool->slot_count ||
          pool->slots[slot->next_live].prev_live != id) {
        free(free_marks); free(live_marks);
        set_err(err, "epa_dynamic_pool_validate: live list link mismatch");
        return 0;
      }
    }
    live_seen++;
    id = slot->next_live;
  }
  if (live_seen != pool->active_count) {
    free(free_marks); free(live_marks);
    set_err(err, "epa_dynamic_pool_validate: active_count mismatch");
    return 0;
  }
  if (live_seen == 0u) {
    if (pool->live_head != EPA_DYNAMIC_NULL || pool->live_tail != EPA_DYNAMIC_NULL) {
      free(free_marks); free(live_marks);
      set_err(err, "epa_dynamic_pool_validate: empty live header mismatch");
      return 0;
    }
  } else if (pool->live_tail != last_live) {
    free(free_marks); free(live_marks);
    set_err(err, "epa_dynamic_pool_validate: live_tail mismatch");
    return 0;
  }

  for (i = 0; i < pool->slot_count; i++) {
    const EpaDynamicSlot *slot = &pool->slots[i];
    if (slot->segment_index >= pool->segment_count) {
      free(free_marks); free(live_marks);
      set_err(err, "epa_dynamic_pool_validate: bad segment index");
      return 0;
    }
    if (!pool->segments[slot->segment_index].present) {
      if (slot->is_live || slot->is_free || free_marks[i] != 0u || live_marks[i] != 0u) {
        free(free_marks); free(live_marks);
        set_err(err, "epa_dynamic_pool_validate: released segment slot still active");
        return 0;
      }
      continue;
    }
    if (((slot->is_live ? 1u : 0u) + (slot->is_free ? 1u : 0u)) != 1u) {
      free(free_marks); free(live_marks);
      set_err(err, "epa_dynamic_pool_validate: slot must be exactly live or free");
      return 0;
    }
    if (free_marks[i] != (slot->is_free ? 1u : 0u) ||
        live_marks[i] != (slot->is_live ? 1u : 0u)) {
      free(free_marks); free(live_marks);
      set_err(err, "epa_dynamic_pool_validate: list membership mismatch");
      return 0;
    }
  }

  if (pool->last_present_segment != EPA_DYNAMIC_NULL &&
      pool->last_present_segment < pool->segment_count &&
      !pool->segments[pool->last_present_segment].present) {
    free(free_marks); free(live_marks);
    set_err(err, "epa_dynamic_pool_validate: last_present_segment mismatch");
    return 0;
  }

  for (i = 0; i < EPA_DYNAMIC_VTABLE_LEVELS; i++) {
    uint32_t stride = pool->ord_vtables[i].stride;
    uint32_t entry_count = (pool->active_count == 0u) ? 0u : ((pool->active_count - 1u) / stride + 1u);
    uint32_t entry;
    for (entry = 0; entry < entry_count; entry++) {
      uint32_t expected = EPA_DYNAMIC_NULL;
      if (!epa_dynamic_pool_live_id_at(pool, entry * stride, &expected, err)) {
        free(free_marks); free(live_marks);
        return 0;
      }
      if (entry >= pool->ord_vtables[i].count || pool->ord_vtables[i].ids[entry] != expected) {
        free(free_marks); free(live_marks);
        set_err(err, "epa_dynamic_pool_validate: ordinal vtable mismatch");
        return 0;
      }
    }
  }

  free(free_marks);
  free(live_marks);
  if (err) err[0] = 0;
  return 1;
}
