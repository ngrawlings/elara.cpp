#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DYN_NULL 0xffffffffu

typedef struct DynSlot {
    uint32_t id;
    uint32_t segment_index;
    uint32_t next_live;
    uint32_t prev_live;
    uint32_t next_free;
    uint32_t prev_free;
    uint8_t is_live;
    uint8_t is_free;
} DynSlot;

typedef struct DynSegment {
    uint32_t index;
    uint32_t start_id;
    uint32_t slot_count;
    uint8_t present;
} DynSegment;

typedef struct DynPool {
    uint32_t min_free;
    uint32_t max_free;
    uint32_t grow_by;

    uint32_t active_count;
    uint32_t free_count;
    uint32_t live_head;
    uint32_t live_tail;
    uint32_t free_head;

    DynSlot *slots;
    uint32_t slot_cap;
    uint32_t slot_count;

    DynSegment *segments;
    uint32_t segment_cap;
    uint32_t segment_count;
    uint32_t first_present_segment;
    uint32_t last_present_segment;
} DynPool;

typedef struct FuzzRng {
    uint32_t state;
} FuzzRng;

uint32_t dynamic_fuzz_env_u32(const char *name, uint32_t fallback);
uint32_t dynamic_fuzz_rng_next(FuzzRng *rng);
uint32_t dynamic_fuzz_rng_range(FuzzRng *rng, uint32_t n);

void dyn_pool_init(DynPool *pool, uint32_t min_free, uint32_t max_free, uint32_t grow_by);
void dyn_pool_destroy(DynPool *pool);
DynSlot *dyn_slot(DynPool *pool, uint32_t id);
void dyn_pool_round_enter(DynPool *pool);
int dyn_pool_alloc(DynPool *pool, uint32_t *out_id);
void dyn_pool_free(DynPool *pool, uint32_t id);
uint32_t dyn_collect_live(DynPool *pool, uint32_t *out_ids, uint32_t cap);
void dyn_assert_invariants(DynPool *pool);
int run_dynamic_fuzz_case(uint32_t seed, uint32_t min_free, uint32_t max_free,
                          uint32_t grow_by, uint32_t rounds, uint32_t max_ops_per_round);

#ifdef __cplusplus
}
#endif
