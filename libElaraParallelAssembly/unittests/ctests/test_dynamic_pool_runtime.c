#include "ctest.h"
#include "epa_asm_compiler.h"
#include "epa_program_loader.h"
#include "memory/epa_dynamic_pool.h"
#include "vm/epa_worker_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int write_dynamic_pool_test_asm(char *path_buf, size_t path_buf_size)
{
    FILE *fp;
    const char *asm_text =
        "DYNAMIC_POOL 0 8 3 9 3\n"
        "ENTRY_START 0 8 8 64\n"
        "WAIT_FOR_DATA\n"
        "ENTRY_END\n"
        "END\n";

    snprintf(path_buf, path_buf_size, "/tmp/epa-dynamic-pool-%ld.epaasm", (long)getpid());
    fp = fopen(path_buf, "w");
    if (!fp) return 0;
    if (fputs(asm_text, fp) == EOF) {
        fclose(fp);
        unlink(path_buf);
        return 0;
    }
    if (fclose(fp) != 0) {
        unlink(path_buf);
        return 0;
    }
    return 1;
}

CTEST(test_runtime_dynamic_pool_basic_round_enter)
{
    EpaDynamicPool pool;
    char err[256];
    uint32_t id = EPA_DYNAMIC_NULL;

    ASSERT_TRUE(epa_dynamic_pool_init(&pool, 4u, 12u, 4u, 8u, err));
    ASSERT_TRUE(epa_dynamic_pool_round_enter(&pool, err));
    ASSERT_EQ(pool.free_count, 4u);
    ASSERT_EQ(pool.active_count, 0u);
    ASSERT_TRUE(epa_dynamic_pool_alloc(&pool, &id, err));
    ASSERT_EQ(pool.active_count, 1u);
    ASSERT_EQ(pool.free_count, 3u);
    ASSERT_TRUE(epa_dynamic_pool_release(&pool, id, err));
    ASSERT_EQ(pool.active_count, 0u);
    ASSERT_EQ(pool.free_count, 4u);
    ASSERT_TRUE(epa_dynamic_pool_validate(&pool, err));

    epa_dynamic_pool_free(&pool);
    return 0;
}

CTEST(test_worker_dynamic_pool_round_enter_configured_pools)
{
    EpaWorkerState w;
    EpaDynamicPoolConfig cfg;
    char err[256];
    uint32_t id = EPA_DYNAMIC_NULL;

    ASSERT_TRUE(epa_worker_init(&w, 7u, 0u, 0u, 8u, 8u, 64u, err));
    cfg.pool_id = 0u;
    cfg.element_size = 8u;
    cfg.min_free = 3u;
    cfg.max_free = 9u;
    cfg.grow_by = 3u;
    ASSERT_TRUE(epa_worker_configure_dynamic_pools(&w, &cfg, 1u, err));
    ASSERT_EQ(w.dynamic_pool_count, 1u);
    ASSERT_TRUE(epa_worker_round_enter(&w, err));
    ASSERT_EQ(w.dynamic_pools[0].free_count, 3u);
    ASSERT_TRUE(epa_dynamic_pool_alloc(&w.dynamic_pools[0], &id, err));
    ASSERT_EQ(w.dynamic_pools[0].active_count, 1u);
    ASSERT_EQ(w.dynamic_pools[0].live_tail, id);
    ASSERT_TRUE(epa_worker_round_enter(&w, err));
    ASSERT_TRUE(w.dynamic_pools[0].free_count >= 3u);
    ASSERT_TRUE(epa_dynamic_pool_validate(&w.dynamic_pools[0], err));

    epa_worker_free(&w);
    return 0;
}

CTEST(test_program_dynamic_pool_manifest_parses)
{
    char asm_path[64];
    char err[256];
    uint8_t *blob;
    size_t blob_len = 0u;
    EpaProgramDesc prog;

    ASSERT_TRUE(write_dynamic_pool_test_asm(asm_path, sizeof(asm_path)));
    blob = epa_asm_compile_file(asm_path, &blob_len, err);
    unlink(asm_path);
    ASSERT_TRUE(blob != NULL);
    ASSERT_TRUE(epa_program_parse(&prog, blob, blob_len, err));
    ASSERT_EQ(prog.dynamic_pool_count, 1u);
    ASSERT_EQ(prog.dynamic_pools[0].pool_id, 0u);
    ASSERT_EQ(prog.dynamic_pools[0].element_size, 8u);
    ASSERT_EQ(prog.dynamic_pools[0].min_free, 3u);
    ASSERT_EQ(prog.dynamic_pools[0].max_free, 9u);
    ASSERT_EQ(prog.dynamic_pools[0].grow_by, 3u);

    epa_program_free(&prog);
    free(blob);
    return 0;
}

CTEST(test_runtime_dynamic_pool_read_write_and_swap)
{
    EpaDynamicPool pool;
    char err[256];
    uint32_t id0 = EPA_DYNAMIC_NULL;
    uint32_t id1 = EPA_DYNAMIC_NULL;
    uint32_t value0[2] = {11u, 22u};
    uint32_t value1[2] = {33u, 44u};
    uint32_t out[2] = {0u, 0u};
    uint32_t live[2] = {0u, 0u};

    ASSERT_TRUE(epa_dynamic_pool_init(&pool, 2u, 6u, 2u, 8u, err));
    ASSERT_TRUE(epa_dynamic_pool_round_enter(&pool, err));
    ASSERT_TRUE(epa_dynamic_pool_alloc(&pool, &id0, err));
    ASSERT_TRUE(epa_dynamic_pool_alloc(&pool, &id1, err));
    ASSERT_TRUE(epa_dynamic_pool_write(&pool, id0, value0, sizeof(value0), err));
    ASSERT_TRUE(epa_dynamic_pool_write(&pool, id1, value1, sizeof(value1), err));
    ASSERT_TRUE(epa_dynamic_pool_read(&pool, id1, out, sizeof(out), err));
    ASSERT_EQ(out[0], 33u);
    ASSERT_EQ(out[1], 44u);
    ASSERT_EQ(epa_dynamic_pool_collect_live(&pool, live, 2u), 2u);
    ASSERT_EQ(live[0], id0);
    ASSERT_EQ(live[1], id1);
    ASSERT_TRUE(epa_dynamic_pool_swap_live_order(&pool, id0, id1, err));
    ASSERT_EQ(epa_dynamic_pool_collect_live(&pool, live, 2u), 2u);
    ASSERT_EQ(live[0], id1);
    ASSERT_EQ(live[1], id0);
    ASSERT_TRUE(epa_dynamic_pool_validate(&pool, err));

    epa_dynamic_pool_free(&pool);
    return 0;
}

void ctest_register_test_dynamic_pool_runtime(void)
{
    const char *F = "test_dynamic_pool_runtime.c";
    REG(F, test_runtime_dynamic_pool_basic_round_enter);
    REG(F, test_worker_dynamic_pool_round_enter_configured_pools);
    REG(F, test_program_dynamic_pool_manifest_parses);
    REG(F, test_runtime_dynamic_pool_read_write_and_swap);
}
