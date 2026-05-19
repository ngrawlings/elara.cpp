#include "dynamic_pool_fuzz_model.h"
#include "memory/epa_dynamic_pool.h"

#include <stdio.h>

static int compare_model_and_runtime(DynPool *model, EpaDynamicPool *runtime, char err[256])
{
    uint32_t i;
    if (model->active_count != runtime->active_count ||
        model->free_count != runtime->free_count ||
        model->live_head != runtime->live_head ||
        model->live_tail != runtime->live_tail ||
        model->free_head != runtime->free_head ||
        model->slot_count != runtime->slot_count ||
        model->segment_count != runtime->segment_count ||
        model->first_present_segment != runtime->first_present_segment ||
        model->last_present_segment != runtime->last_present_segment) {
        if (err) snprintf(err, 256, "header mismatch");
        return 0;
    }
    for (i = 0; i < model->slot_count; i++) {
        DynSlot *ms = &model->slots[i];
        EpaDynamicSlot *rs = &runtime->slots[i];
        if (ms->id != rs->id ||
            ms->segment_index != rs->segment_index ||
            ms->next_live != rs->next_live ||
            ms->prev_live != rs->prev_live ||
            ms->next_free != rs->next_free ||
            ms->prev_free != rs->prev_free ||
            ms->is_live != rs->is_live ||
            ms->is_free != rs->is_free) {
            if (err) snprintf(err, 256, "slot mismatch at %u", i);
            return 0;
        }
    }
    for (i = 0; i < model->segment_count; i++) {
        DynSegment *ms = &model->segments[i];
        EpaDynamicSegment *rs = &runtime->segments[i];
        if (ms->index != rs->index ||
            ms->start_id != rs->start_id ||
            ms->slot_count != rs->slot_count ||
            ms->present != rs->present) {
            if (err) snprintf(err, 256, "segment mismatch at %u", i);
            return 0;
        }
    }
    return 1;
}

static int run_dynamic_crosscheck_case(uint32_t seed, uint32_t min_free, uint32_t max_free,
                                       uint32_t grow_by, uint32_t rounds, uint32_t max_ops_per_round)
{
    DynPool model;
    EpaDynamicPool runtime;
    FuzzRng rng;
    uint32_t round_index;
    char err[256];

    rng.state = seed ? seed : 0x9e3779b9u;
    dyn_pool_init(&model, min_free, max_free, grow_by);
    if (!epa_dynamic_pool_init(&runtime, min_free, max_free, grow_by, err)) {
        fprintf(stderr, "runtime init failed: %s\n", err);
        return 1;
    }

    for (round_index = 0; round_index < rounds; round_index++) {
        uint32_t op_count = 1u + dynamic_fuzz_rng_range(&rng, max_ops_per_round);
        uint32_t op;

        dyn_pool_round_enter(&model);
        if (!epa_dynamic_pool_round_enter(&runtime, err)) {
            fprintf(stderr, "runtime round_enter failed seed=%u round=%u: %s\n", seed, round_index, err);
            epa_dynamic_pool_free(&runtime);
            dyn_pool_destroy(&model);
            return 1;
        }
        dyn_assert_invariants(&model);
        if (!epa_dynamic_pool_validate(&runtime, err) ||
            !compare_model_and_runtime(&model, &runtime, err)) {
            fprintf(stderr, "post-round mismatch seed=%u round=%u: %s\n", seed, round_index, err);
            epa_dynamic_pool_free(&runtime);
            dyn_pool_destroy(&model);
            return 1;
        }

        for (op = 0; op < op_count; op++) {
            uint32_t choice = dynamic_fuzz_rng_range(&rng, 100u);
            if (choice < 55u) {
                uint32_t model_id = DYN_NULL;
                uint32_t runtime_id = EPA_DYNAMIC_NULL;
                int mok = dyn_pool_alloc(&model, &model_id);
                int rok = epa_dynamic_pool_alloc(&runtime, &runtime_id, err);
                if (!!mok != !!rok) {
                    fprintf(stderr, "alloc success mismatch seed=%u round=%u op=%u\n", seed, round_index, op);
                    epa_dynamic_pool_free(&runtime);
                    dyn_pool_destroy(&model);
                    return 1;
                }
                if (mok && model_id != runtime_id) {
                    fprintf(stderr, "alloc id mismatch seed=%u round=%u op=%u model=%u runtime=%u\n",
                            seed, round_index, op, model_id, runtime_id);
                    epa_dynamic_pool_free(&runtime);
                    dyn_pool_destroy(&model);
                    return 1;
                }
            } else if (choice < 90u) {
                uint32_t live_ids[1024];
                uint32_t live_count = dyn_collect_live(&model, live_ids, 1024u);
                if (live_count > 0u) {
                    uint32_t pick = dynamic_fuzz_rng_range(&rng, live_count);
                    dyn_pool_free(&model, live_ids[pick]);
                    if (!epa_dynamic_pool_release(&runtime, live_ids[pick], err)) {
                        fprintf(stderr, "release failed seed=%u round=%u op=%u: %s\n",
                                seed, round_index, op, err);
                        epa_dynamic_pool_free(&runtime);
                        dyn_pool_destroy(&model);
                        return 1;
                    }
                }
            } else {
                uint32_t burst = 1u + dynamic_fuzz_rng_range(&rng, 6u);
                uint32_t i;
                for (i = 0; i < burst; i++) {
                    uint32_t model_id = DYN_NULL;
                    uint32_t runtime_id = EPA_DYNAMIC_NULL;
                    int mok = dyn_pool_alloc(&model, &model_id);
                    int rok = epa_dynamic_pool_alloc(&runtime, &runtime_id, err);
                    if (!!mok != !!rok) {
                        fprintf(stderr, "burst alloc mismatch seed=%u round=%u op=%u step=%u\n",
                                seed, round_index, op, i);
                        epa_dynamic_pool_free(&runtime);
                        dyn_pool_destroy(&model);
                        return 1;
                    }
                    if (!mok) break;
                    if (model_id != runtime_id) {
                        fprintf(stderr, "burst alloc id mismatch seed=%u round=%u op=%u step=%u\n",
                                seed, round_index, op, i);
                        epa_dynamic_pool_free(&runtime);
                        dyn_pool_destroy(&model);
                        return 1;
                    }
                }
            }

            dyn_assert_invariants(&model);
            if (!epa_dynamic_pool_validate(&runtime, err) ||
                !compare_model_and_runtime(&model, &runtime, err)) {
                fprintf(stderr, "post-op mismatch seed=%u round=%u op=%u: %s\n", seed, round_index, op, err);
                epa_dynamic_pool_free(&runtime);
                dyn_pool_destroy(&model);
                return 1;
            }
        }

        if ((round_index % 7u) == 0u) {
            uint32_t live_ids[1024];
            uint32_t live_count = dyn_collect_live(&model, live_ids, 1024u);
            while (live_count > 0u) {
                uint32_t id = live_ids[live_count - 1u];
                dyn_pool_free(&model, id);
                if (!epa_dynamic_pool_release(&runtime, id, err)) {
                    fprintf(stderr, "final release failed seed=%u round=%u: %s\n", seed, round_index, err);
                    epa_dynamic_pool_free(&runtime);
                    dyn_pool_destroy(&model);
                    return 1;
                }
                live_count--;
            }
            dyn_assert_invariants(&model);
            if (!epa_dynamic_pool_validate(&runtime, err) ||
                !compare_model_and_runtime(&model, &runtime, err)) {
                fprintf(stderr, "post-drain mismatch seed=%u round=%u: %s\n", seed, round_index, err);
                epa_dynamic_pool_free(&runtime);
                dyn_pool_destroy(&model);
                return 1;
            }
        }
    }

    epa_dynamic_pool_free(&runtime);
    dyn_pool_destroy(&model);
    return 0;
}

int main(void)
{
    uint32_t seeds = dynamic_fuzz_env_u32("EPA_DYNAMIC_FUZZ_SEEDS", 128u);
    uint32_t rounds = dynamic_fuzz_env_u32("EPA_DYNAMIC_FUZZ_ROUNDS", 200u);
    uint32_t ops = dynamic_fuzz_env_u32("EPA_DYNAMIC_FUZZ_OPS", 80u);
    uint32_t i;
    const struct {
        uint32_t min_free;
        uint32_t max_free;
        uint32_t grow_by;
    } configs[] = {
        { 4u, 12u, 4u },
        { 8u, 24u, 8u },
        { 5u, 25u, 5u },
        { 3u, 18u, 6u },
        { 16u, 48u, 16u },
        { 7u, 21u, 7u }
    };

    for (i = 0; i < seeds; i++) {
        uint32_t cfg = i % (uint32_t)(sizeof(configs) / sizeof(configs[0]));
        uint32_t seed = 0x12345678u + (i * 0x9e3779b9u);
        if (run_dynamic_crosscheck_case(seed,
                                        configs[cfg].min_free,
                                        configs[cfg].max_free,
                                        configs[cfg].grow_by,
                                        rounds,
                                        ops) != 0) {
            fprintf(stderr,
                    "dynamic runtime crosscheck failed seed=%u cfg={min=%u max=%u grow=%u} rounds=%u ops=%u\n",
                    seed,
                    configs[cfg].min_free,
                    configs[cfg].max_free,
                    configs[cfg].grow_by,
                    rounds,
                    ops);
            return 1;
        }
    }

    fprintf(stdout,
            "dynamic runtime crosscheck passed seeds=%u rounds=%u ops=%u configs=%zu\n",
            seeds, rounds, ops, sizeof(configs) / sizeof(configs[0]));
    return 0;
}
