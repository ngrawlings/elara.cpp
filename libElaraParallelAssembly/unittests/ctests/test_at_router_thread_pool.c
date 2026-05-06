#include "ctest.h"

#include "atomic_tasks/epa_at_router.h"
#include "memory/epa_ghs.h"
#include "threads/epa_thread_pool.h"
#include "vm/epa_vm.h"
#include "epa_kernel.h"

#include <stdatomic.h>
#include <string.h>
static _Atomic uint32_t g_router_hits;

static int test_router_exec_env(EpaAtBatch *b,
                                uint32_t vtid,
                                int32_t tid,
                                epa_ghs_t *ghs,
                                epa_ghs_handle_t h) {
    (void)b;
    (void)vtid;
    (void)tid;
    (void)ghs;
    (void)h;

    EpaVM *vm = (EpaVM*)epa_thread_pool_tls_vm();
    if (!vm) return 0;

    if (vm->csc[0] != 0 || vm->csc[1] != 0 || vm->csc[2] != 0 || vm->csc[3] != 0) return 0;
    if (vm->locals[0] != 0) return 0;
    if (vm->lbytes_top != 0) return 0;

    char err[256] = {0};
    if (!epa_vm_lbytes_alloc(vm, 32u, 8u, err)) return 0;

    vm->csc[0] = 0xA5A5A5A5u;
    vm->locals[0] = 12345;

    atomic_fetch_add(&g_router_hits, 1u);
    return 1;
}

CTEST(test_at_router_launches_into_fresh_thread_vm)
{
    char err[256] = {0};
    atomic_store(&g_router_hits, 0u);

    EpaThreadPool pool;
    memset(&pool, 0, sizeof(pool));
    ASSERT_TRUE(epa_thread_pool_init(&pool, 1));

    EpaKernel k;
    memset(&k, 0, sizeof(k));
    k.impl.tp = &pool;

    EpaWorkerState w;
    memset(&w, 0, sizeof(w));
    w.id = 1;
    w.blocked = 1;  // emulate WAIT_FOR_AT parking the worker

    epa_ghs_t *ghs = epa_ghs_create(8, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    epa_ghs_handle_t h = 0;
    ASSERT_EQ(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, 64, &h), EPA_GHS_OK);

    ASSERT_TRUE(epa_at_router_register(0x4242u, test_router_exec_env));
    ASSERT_TRUE(epa_at_router_launch_parallel(&k, &w, 0x4242u, ghs, h, 3u, err));
    ASSERT_TRUE(w.at_running == 1);

    int done = 0;
    for (int i = 0; i < 100000; i++) {
        if (!epa_at_router_update_worker(&w, err)) break;
        if (!w.at_running) {
            done = 1;
            break;
        }
    }

    ASSERT_TRUE(done);
    ASSERT_EQ(atomic_load(&g_router_hits), 3u);
    ASSERT_TRUE(w.at_running == 0);
    ASSERT_TRUE(w.at.active == 0);
    ASSERT_EQ(w.at.done_threads, 3u);
    ASSERT_TRUE(w.blocked == 0);

    ASSERT_EQ(epa_ghs_free(ghs, h), EPA_GHS_OK);
    epa_ghs_destroy(ghs);
    epa_thread_pool_shutdown(&pool);
    return 0;
}

void ctest_register_test_at_router_thread_pool(void)
{
    const char *F = "test_at_router_thread_pool.c";
    REG(F, test_at_router_launches_into_fresh_thread_vm);
}
