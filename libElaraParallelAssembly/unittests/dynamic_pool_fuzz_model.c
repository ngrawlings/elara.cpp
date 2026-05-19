#include "dynamic_pool_fuzz_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_ASSERT(x) do { \
    if (!(x)) { \
        fprintf(stderr, "FUZZ_ASSERT failed: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

static void dyn_slots_reserve(DynPool *pool, uint32_t need)
{
    uint32_t next_cap;
    if (need <= pool->slot_cap) return;
    next_cap = pool->slot_cap ? pool->slot_cap : 16u;
    while (next_cap < need) next_cap *= 2u;
    pool->slots = (DynSlot*)realloc(pool->slots, sizeof(DynSlot) * next_cap);
    FUZZ_ASSERT(pool->slots != NULL);
    memset(pool->slots + pool->slot_cap, 0, sizeof(DynSlot) * (next_cap - pool->slot_cap));
    pool->slot_cap = next_cap;
}

static void dyn_segments_reserve(DynPool *pool, uint32_t need)
{
    uint32_t next_cap;
    if (need <= pool->segment_cap) return;
    next_cap = pool->segment_cap ? pool->segment_cap : 8u;
    while (next_cap < need) next_cap *= 2u;
    pool->segments = (DynSegment*)realloc(pool->segments, sizeof(DynSegment) * next_cap);
    FUZZ_ASSERT(pool->segments != NULL);
    memset(pool->segments + pool->segment_cap, 0, sizeof(DynSegment) * (next_cap - pool->segment_cap));
    pool->segment_cap = next_cap;
}

static void dyn_free_prepend(DynPool *pool, uint32_t id)
{
    DynSlot *slot = dyn_slot(pool, id);
    FUZZ_ASSERT(slot != NULL);
    FUZZ_ASSERT(slot->is_free);
    slot->prev_free = DYN_NULL;
    slot->next_free = pool->free_head;
    if (pool->free_head != DYN_NULL) {
        DynSlot *old = dyn_slot(pool, pool->free_head);
        FUZZ_ASSERT(old != NULL);
        old->prev_free = id;
    }
    pool->free_head = id;
    pool->free_count++;
}

static void dyn_free_remove(DynPool *pool, uint32_t id)
{
    DynSlot *slot = dyn_slot(pool, id);
    FUZZ_ASSERT(slot != NULL);
    FUZZ_ASSERT(slot->is_free);
    if (slot->prev_free != DYN_NULL) {
        DynSlot *prev = dyn_slot(pool, slot->prev_free);
        FUZZ_ASSERT(prev != NULL);
        prev->next_free = slot->next_free;
    } else {
        FUZZ_ASSERT(pool->free_head == id);
        pool->free_head = slot->next_free;
    }
    if (slot->next_free != DYN_NULL) {
        DynSlot *next = dyn_slot(pool, slot->next_free);
        FUZZ_ASSERT(next != NULL);
        next->prev_free = slot->prev_free;
    }
    slot->next_free = DYN_NULL;
    slot->prev_free = DYN_NULL;
    pool->free_count--;
}

static void dyn_live_append(DynPool *pool, uint32_t id)
{
    DynSlot *slot = dyn_slot(pool, id);
    FUZZ_ASSERT(slot != NULL);
    FUZZ_ASSERT(slot->is_live);
    slot->next_live = DYN_NULL;
    slot->prev_live = pool->live_tail;
    if (pool->live_tail != DYN_NULL) {
        DynSlot *tail = dyn_slot(pool, pool->live_tail);
        FUZZ_ASSERT(tail != NULL);
        tail->next_live = id;
    } else {
        pool->live_head = id;
    }
    pool->live_tail = id;
    pool->active_count++;
}

static void dyn_live_remove(DynPool *pool, uint32_t id)
{
    DynSlot *slot = dyn_slot(pool, id);
    FUZZ_ASSERT(slot != NULL);
    FUZZ_ASSERT(slot->is_live);
    if (slot->prev_live != DYN_NULL) {
        DynSlot *prev = dyn_slot(pool, slot->prev_live);
        FUZZ_ASSERT(prev != NULL);
        prev->next_live = slot->next_live;
    } else {
        FUZZ_ASSERT(pool->live_head == id);
        pool->live_head = slot->next_live;
    }
    if (slot->next_live != DYN_NULL) {
        DynSlot *next = dyn_slot(pool, slot->next_live);
        FUZZ_ASSERT(next != NULL);
        next->prev_live = slot->prev_live;
    } else {
        FUZZ_ASSERT(pool->live_tail == id);
        pool->live_tail = slot->prev_live;
    }
    slot->next_live = DYN_NULL;
    slot->prev_live = DYN_NULL;
    pool->active_count--;
}

static void dyn_pool_grow(DynPool *pool)
{
    uint32_t i;
    uint32_t seg_index = pool->segment_count;
    uint32_t start_id = pool->slot_count;

    dyn_segments_reserve(pool, pool->segment_count + 1u);
    dyn_slots_reserve(pool, pool->slot_count + pool->grow_by);

    pool->segments[seg_index].index = seg_index;
    pool->segments[seg_index].start_id = start_id;
    pool->segments[seg_index].slot_count = pool->grow_by;
    pool->segments[seg_index].present = 1u;
    pool->segment_count++;
    if (pool->first_present_segment == DYN_NULL) pool->first_present_segment = seg_index;
    pool->last_present_segment = seg_index;

    for (i = 0; i < pool->grow_by; i++) {
        DynSlot *slot = &pool->slots[pool->slot_count + i];
        memset(slot, 0, sizeof(*slot));
        slot->id = pool->slot_count + i;
        slot->segment_index = seg_index;
        slot->next_live = DYN_NULL;
        slot->prev_live = DYN_NULL;
        slot->next_free = DYN_NULL;
        slot->prev_free = DYN_NULL;
        slot->is_live = 0u;
        slot->is_free = 1u;
    }
    pool->slot_count += pool->grow_by;
    for (i = 0; i < pool->grow_by; i++) {
        dyn_free_prepend(pool, start_id + i);
    }
}

static int dyn_segment_all_free(DynPool *pool, uint32_t seg_index)
{
    uint32_t i;
    DynSegment *seg = &pool->segments[seg_index];
    if (!seg->present) return 1;
    for (i = 0; i < seg->slot_count; i++) {
        DynSlot *slot = dyn_slot(pool, seg->start_id + i);
        FUZZ_ASSERT(slot != NULL);
        if (slot->is_live) return 0;
        if (!slot->is_free) return 0;
    }
    return 1;
}

static void dyn_detach_segment_free_slots(DynPool *pool, uint32_t seg_index)
{
    uint32_t i;
    DynSegment *seg = &pool->segments[seg_index];
    for (i = 0; i < seg->slot_count; i++) {
        DynSlot *slot = dyn_slot(pool, seg->start_id + i);
        FUZZ_ASSERT(slot != NULL);
        FUZZ_ASSERT(slot->is_free);
        dyn_free_remove(pool, slot->id);
        slot->is_free = 0u;
    }
}

static void dyn_pool_shrink_tail_segments(DynPool *pool)
{
    while (pool->free_count > pool->max_free && pool->last_present_segment != DYN_NULL) {
        DynSegment *seg = &pool->segments[pool->last_present_segment];
        if (!seg->present) {
            if (pool->last_present_segment == 0u) pool->last_present_segment = DYN_NULL;
            else pool->last_present_segment--;
            continue;
        }
        if (!dyn_segment_all_free(pool, seg->index)) break;
        if (pool->free_count - seg->slot_count < pool->min_free) break;
        dyn_detach_segment_free_slots(pool, seg->index);
        seg->present = 0u;
        while (pool->last_present_segment != DYN_NULL &&
               pool->segments[pool->last_present_segment].present == 0u) {
            if (pool->last_present_segment == 0u) pool->last_present_segment = DYN_NULL;
            else pool->last_present_segment--;
        }
        while (pool->first_present_segment != DYN_NULL &&
               pool->first_present_segment < pool->segment_count &&
               pool->segments[pool->first_present_segment].present == 0u) {
            if (pool->first_present_segment + 1u >= pool->segment_count) {
                pool->first_present_segment = DYN_NULL;
                break;
            }
            pool->first_present_segment++;
        }
    }
}

uint32_t dynamic_fuzz_env_u32(const char *name, uint32_t fallback)
{
    const char *s = getenv(name);
    char *end = NULL;
    unsigned long v;
    if (!s || !s[0]) return fallback;
    v = strtoul(s, &end, 10);
    if (!end || *end != '\0') return fallback;
    return (uint32_t)v;
}

uint32_t dynamic_fuzz_rng_next(FuzzRng *rng)
{
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x ? x : 0x6d2b79f5u;
    return rng->state;
}

uint32_t dynamic_fuzz_rng_range(FuzzRng *rng, uint32_t n)
{
    if (n == 0u) return 0u;
    return dynamic_fuzz_rng_next(rng) % n;
}

void dyn_pool_init(DynPool *pool, uint32_t min_free, uint32_t max_free, uint32_t grow_by)
{
    memset(pool, 0, sizeof(*pool));
    pool->min_free = min_free;
    pool->max_free = max_free;
    pool->grow_by = grow_by;
    pool->live_head = DYN_NULL;
    pool->live_tail = DYN_NULL;
    pool->free_head = DYN_NULL;
    pool->first_present_segment = DYN_NULL;
    pool->last_present_segment = DYN_NULL;
}

void dyn_pool_destroy(DynPool *pool)
{
    free(pool->slots);
    free(pool->segments);
    memset(pool, 0, sizeof(*pool));
}

DynSlot *dyn_slot(DynPool *pool, uint32_t id)
{
    if (id == DYN_NULL || id >= pool->slot_count) return NULL;
    return &pool->slots[id];
}

void dyn_pool_round_enter(DynPool *pool)
{
    while (pool->free_count < pool->min_free) {
        dyn_pool_grow(pool);
    }
    dyn_pool_shrink_tail_segments(pool);
}

int dyn_pool_alloc(DynPool *pool, uint32_t *out_id)
{
    DynSlot *slot;
    uint32_t id;
    if (pool->free_head == DYN_NULL) return 0;
    id = pool->free_head;
    slot = dyn_slot(pool, id);
    FUZZ_ASSERT(slot != NULL);
    dyn_free_remove(pool, id);
    slot->is_free = 0u;
    slot->is_live = 1u;
    dyn_live_append(pool, id);
    *out_id = id;
    return 1;
}

void dyn_pool_free(DynPool *pool, uint32_t id)
{
    DynSlot *slot = dyn_slot(pool, id);
    FUZZ_ASSERT(slot != NULL);
    FUZZ_ASSERT(slot->is_live);
    dyn_live_remove(pool, id);
    slot->is_live = 0u;
    slot->is_free = 1u;
    dyn_free_prepend(pool, id);
}

uint32_t dyn_collect_live(DynPool *pool, uint32_t *out_ids, uint32_t cap)
{
    uint32_t count = 0u;
    uint32_t id = pool->live_head;
    while (id != DYN_NULL) {
        DynSlot *slot = dyn_slot(pool, id);
        FUZZ_ASSERT(slot != NULL);
        if (count < cap) out_ids[count] = id;
        count++;
        id = slot->next_live;
    }
    return count;
}

void dyn_assert_invariants(DynPool *pool)
{
    uint32_t i;
    uint32_t free_seen = 0u;
    uint32_t live_seen = 0u;
    uint32_t id;
    uint32_t last_live = DYN_NULL;
    uint8_t *free_marks = (uint8_t*)calloc(pool->slot_count ? pool->slot_count : 1u, 1u);
    uint8_t *live_marks = (uint8_t*)calloc(pool->slot_count ? pool->slot_count : 1u, 1u);
    FUZZ_ASSERT(free_marks != NULL);
    FUZZ_ASSERT(live_marks != NULL);

    id = pool->free_head;
    while (id != DYN_NULL) {
        DynSlot *slot = dyn_slot(pool, id);
        FUZZ_ASSERT(slot != NULL);
        FUZZ_ASSERT(slot->is_free);
        FUZZ_ASSERT(!slot->is_live);
        FUZZ_ASSERT(free_marks[id] == 0u);
        free_marks[id] = 1u;
        if (slot->next_free != DYN_NULL) {
            DynSlot *next = dyn_slot(pool, slot->next_free);
            FUZZ_ASSERT(next != NULL);
            FUZZ_ASSERT(next->prev_free == id);
        }
        free_seen++;
        id = slot->next_free;
    }
    FUZZ_ASSERT(free_seen == pool->free_count);

    id = pool->live_head;
    while (id != DYN_NULL) {
        DynSlot *slot = dyn_slot(pool, id);
        FUZZ_ASSERT(slot != NULL);
        FUZZ_ASSERT(slot->is_live);
        FUZZ_ASSERT(!slot->is_free);
        FUZZ_ASSERT(live_marks[id] == 0u);
        live_marks[id] = 1u;
        FUZZ_ASSERT(slot->prev_live == last_live);
        last_live = id;
        if (slot->next_live != DYN_NULL) {
            DynSlot *next = dyn_slot(pool, slot->next_live);
            FUZZ_ASSERT(next != NULL);
            FUZZ_ASSERT(next->prev_live == id);
        }
        live_seen++;
        id = slot->next_live;
    }
    FUZZ_ASSERT(live_seen == pool->active_count);
    if (live_seen == 0u) {
        FUZZ_ASSERT(pool->live_head == DYN_NULL);
        FUZZ_ASSERT(pool->live_tail == DYN_NULL);
    } else {
        FUZZ_ASSERT(pool->live_tail == last_live);
    }

    for (i = 0; i < pool->slot_count; i++) {
        DynSlot *slot = &pool->slots[i];
        if (!pool->segments[slot->segment_index].present) {
            FUZZ_ASSERT(!slot->is_live);
            FUZZ_ASSERT(!slot->is_free);
            FUZZ_ASSERT(free_marks[i] == 0u);
            FUZZ_ASSERT(live_marks[i] == 0u);
            continue;
        }
        FUZZ_ASSERT((slot->is_live ? 1u : 0u) + (slot->is_free ? 1u : 0u) == 1u);
        FUZZ_ASSERT(free_marks[i] == (slot->is_free ? 1u : 0u));
        FUZZ_ASSERT(live_marks[i] == (slot->is_live ? 1u : 0u));
    }

    if (pool->last_present_segment != DYN_NULL) {
        FUZZ_ASSERT(pool->segments[pool->last_present_segment].present);
    }

    free(free_marks);
    free(live_marks);
}

int run_dynamic_fuzz_case(uint32_t seed, uint32_t min_free, uint32_t max_free,
                          uint32_t grow_by, uint32_t rounds, uint32_t max_ops_per_round)
{
    DynPool pool;
    FuzzRng rng;
    uint32_t round_index;

    rng.state = seed ? seed : 0x9e3779b9u;
    dyn_pool_init(&pool, min_free, max_free, grow_by);

    for (round_index = 0; round_index < rounds; round_index++) {
        uint32_t op_count = 1u + dynamic_fuzz_rng_range(&rng, max_ops_per_round);
        uint32_t op;

        dyn_pool_round_enter(&pool);
        if (pool.free_count < pool.min_free) {
            dyn_pool_destroy(&pool);
            return 1;
        }
        dyn_assert_invariants(&pool);

        for (op = 0; op < op_count; op++) {
            uint32_t choice = dynamic_fuzz_rng_range(&rng, 100u);
            if (choice < 55u) {
                uint32_t id = DYN_NULL;
                uint32_t free_before = pool.free_count;
                int ok = dyn_pool_alloc(&pool, &id);
                if (!ok) {
                    if (free_before != 0u) {
                        dyn_pool_destroy(&pool);
                        return 1;
                    }
                } else {
                    if (id == DYN_NULL || pool.free_count + 1u != free_before) {
                        dyn_pool_destroy(&pool);
                        return 1;
                    }
                }
            } else if (choice < 90u) {
                uint32_t live_ids[1024];
                uint32_t live_count = dyn_collect_live(&pool, live_ids, 1024u);
                if (live_count > 0u) {
                    uint32_t pick = dynamic_fuzz_rng_range(&rng, live_count);
                    dyn_pool_free(&pool, live_ids[pick]);
                }
            } else {
                uint32_t burst = 1u + dynamic_fuzz_rng_range(&rng, 6u);
                uint32_t i;
                for (i = 0; i < burst; i++) {
                    uint32_t id = DYN_NULL;
                    if (!dyn_pool_alloc(&pool, &id)) break;
                }
            }
            dyn_assert_invariants(&pool);
        }

        if ((round_index % 7u) == 0u) {
            uint32_t live_ids[1024];
            uint32_t live_count = dyn_collect_live(&pool, live_ids, 1024u);
            while (live_count > 0u) {
                dyn_pool_free(&pool, live_ids[live_count - 1u]);
                live_count--;
            }
            dyn_assert_invariants(&pool);
        }
    }

    dyn_pool_destroy(&pool);
    return 0;
}
