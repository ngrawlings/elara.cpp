#include "ctest.h"
#include "dynamic_pool_fuzz_model.h"

CTEST(test_dynamic_pool_fuzz_reference_model)
{
    uint32_t seeds = dynamic_fuzz_env_u32("EPA_DYNAMIC_FUZZ_SEEDS", 24u);
    uint32_t rounds = dynamic_fuzz_env_u32("EPA_DYNAMIC_FUZZ_ROUNDS", 80u);
    uint32_t ops = dynamic_fuzz_env_u32("EPA_DYNAMIC_FUZZ_OPS", 40u);
    uint32_t i;
    const struct {
        uint32_t min_free;
        uint32_t max_free;
        uint32_t grow_by;
    } configs[] = {
        { 4u, 12u, 4u },
        { 8u, 24u, 8u },
        { 5u, 25u, 5u },
        { 3u, 18u, 6u }
    };

    for (i = 0; i < seeds; i++) {
        uint32_t cfg = i % (uint32_t)(sizeof(configs) / sizeof(configs[0]));
        ASSERT_EQ(run_dynamic_fuzz_case(
            0x12345678u + (i * 0x9e3779b9u),
            configs[cfg].min_free,
            configs[cfg].max_free,
            configs[cfg].grow_by,
            rounds,
            ops
        ), 0);
    }
    return 0;
}

CTEST(test_dynamic_pool_round_entry_replenishes_to_min_free)
{
    DynPool pool;
    uint32_t i;
    dyn_pool_init(&pool, 6u, 18u, 6u);

    dyn_pool_round_enter(&pool);
    ASSERT_EQ(pool.free_count, 6u);
    ASSERT_EQ(pool.active_count, 0u);
    ASSERT_EQ(pool.live_head, DYN_NULL);
    ASSERT_EQ(pool.live_tail, DYN_NULL);

    for (i = 0; i < 6u; i++) {
        uint32_t id = DYN_NULL;
        ASSERT_TRUE(dyn_pool_alloc(&pool, &id));
    }
    ASSERT_EQ(pool.free_count, 0u);
    dyn_pool_round_enter(&pool);
    ASSERT_TRUE(pool.free_count >= 6u);
    dyn_assert_invariants(&pool);

    dyn_pool_destroy(&pool);
    return 0;
}

CTEST(test_dynamic_pool_grow_prepends_new_capacity_and_live_tail_appends)
{
    DynPool pool;
    uint32_t a = DYN_NULL;
    uint32_t b = DYN_NULL;
    uint32_t c = DYN_NULL;
    dyn_pool_init(&pool, 2u, 8u, 2u);

    dyn_pool_round_enter(&pool);
    ASSERT_TRUE(dyn_pool_alloc(&pool, &a));
    ASSERT_TRUE(dyn_pool_alloc(&pool, &b));
    ASSERT_EQ(pool.free_count, 0u);

    dyn_pool_round_enter(&pool);
    ASSERT_TRUE(pool.free_head != DYN_NULL);
    ASSERT_TRUE(pool.free_head >= 2u);

    ASSERT_TRUE(dyn_pool_alloc(&pool, &c));
    ASSERT_EQ(pool.live_head, a);
    ASSERT_EQ(pool.live_tail, c);
    ASSERT_EQ(dyn_slot(&pool, a)->next_live, b);
    ASSERT_EQ(dyn_slot(&pool, b)->next_live, c);
    ASSERT_EQ(dyn_slot(&pool, c)->prev_live, b);
    dyn_assert_invariants(&pool);

    dyn_pool_destroy(&pool);
    return 0;
}

CTEST(test_dynamic_pool_shrink_only_releases_highest_fully_free_segments)
{
    DynPool pool;
    uint32_t a = DYN_NULL;
    uint32_t b = DYN_NULL;
    dyn_pool_init(&pool, 2u, 4u, 2u);

    dyn_pool_round_enter(&pool);
    ASSERT_TRUE(dyn_pool_alloc(&pool, &a));
    ASSERT_TRUE(dyn_pool_alloc(&pool, &b));
    dyn_pool_round_enter(&pool);
    ASSERT_TRUE(dyn_pool_alloc(&pool, &a));
    ASSERT_TRUE(dyn_pool_alloc(&pool, &b));
    dyn_pool_round_enter(&pool);
    ASSERT_TRUE(pool.segment_count >= 3u);

    dyn_pool_free(&pool, b);
    dyn_pool_free(&pool, a);
    dyn_pool_round_enter(&pool);
    ASSERT_TRUE(pool.last_present_segment != DYN_NULL);
    ASSERT_TRUE(pool.segments[0].present);
    ASSERT_TRUE(pool.segments[1].present);
    ASSERT_TRUE(pool.segments[2].present == 0u || pool.free_count <= pool.max_free);
    dyn_assert_invariants(&pool);

    dyn_pool_destroy(&pool);
    return 0;
}

void ctest_register_test_dynamic_pool_fuzz(void)
{
    const char *F = "test_dynamic_pool_fuzz.c";
    REG(F, test_dynamic_pool_fuzz_reference_model);
    REG(F, test_dynamic_pool_round_entry_replenishes_to_min_free);
    REG(F, test_dynamic_pool_grow_prepends_new_capacity_and_live_tail_appends);
    REG(F, test_dynamic_pool_shrink_only_releases_highest_fully_free_segments);
}
