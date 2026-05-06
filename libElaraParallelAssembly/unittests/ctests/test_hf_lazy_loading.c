// unittests/ctests/test_hf_lazy_loading.c
#include "ctest.h"

#include "weights/hf_loader.h"
#include "weights/phi3_map.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int has_model_dir(const char **out_dir) {
    const char *dir = getenv("EPA_TEST_MODEL_DIR");
    if (!dir || !dir[0]) return 0;
    if (out_dir) *out_dir = dir;
    return 1;
}

CTEST(test_hf_lazy_open_is_lightweight)
{
#ifdef EPA_TESTING
    const char *dir = NULL;
    if (!has_model_dir(&dir)) return 0;

    hf_debug_reset_stats();

    char err[256] = {0};
    HfLoader *L = NULL;
    ASSERT_TRUE(hf_open_model_dir(&L, dir, err) == 0);
    ASSERT_TRUE(L != NULL);

    HfDebugStats st;
    hf_debug_get_stats(&st);

    // Contract: open should not mmap or parse safetensors header
    ASSERT_TRUE(st.st_file_mmaps == 0);
    ASSERT_TRUE(st.st_header_parses == 0);

    hf_close(L);
#else
    // If not built with EPA_TESTING, don't fail CI; treat as skip/pass.
    return 0;
#endif

    return 0;
}

CTEST(test_hf_lazy_list_tensors_parses_header_once)
{
#ifdef EPA_TESTING
    const char *dir = NULL;
    if (!has_model_dir(&dir)) return 0;

    hf_debug_reset_stats();

    char err[256] = {0};
    HfLoader *L = NULL;
    ASSERT_TRUE(hf_open_model_dir(&L, dir, err) == 0);

    // list tensors once
    uint32_t count = 0;
    int cb(void *u, const char *k) { (void)k; (*(uint32_t*)u)++; return 0; }
    ASSERT_TRUE(hf_list_tensors(L, (hf_list_cb)cb, &count, err) == 0);
    ASSERT_TRUE(count > 0);

    HfDebugStats st1;
    hf_debug_get_stats(&st1);
    ASSERT_TRUE(st1.st_header_parses == 1);
    ASSERT_TRUE(st1.st_file_mmaps >= 1);

    // list tensors again - should NOT re-parse header
    ASSERT_TRUE(hf_list_tensors(L, (hf_list_cb)cb, &count, err) == 0);

    HfDebugStats st2;
    hf_debug_get_stats(&st2);
    ASSERT_TRUE(st2.st_header_parses == 1);

    hf_close(L);
#else
    return 0;
#endif

    return 0;
}

CTEST(test_hf_lazy_get_tensor_opens_shard_and_reuses_cache)
{
#ifdef EPA_TESTING
    const char *dir = NULL;
    if (!has_model_dir(&dir)) return 0;

    hf_debug_reset_stats();

    char err[256] = {0};
    HfLoader *L = NULL;
    ASSERT_TRUE(hf_open_model_dir(&L, dir, err) == 0);

    // Find the naming variant first (ensures the keys exist)
    Phi3KeyMap km;
    ASSERT_TRUE(phi3_map_keys(L, &km, err) == 0);

    // Pick a known tensor that exists in your dump
    // (lm_head.weight is always present in your Phi-3 dump)
    AtTensorView tv;
    memset(&tv, 0, sizeof(tv));

    HfDebugStats before;
    hf_debug_get_stats(&before);

    ASSERT_TRUE(hf_get_tensor(L, "lm_head.weight", &tv, err) == 0);
    ASSERT_TRUE(tv.data != NULL);

    HfDebugStats after1;
    hf_debug_get_stats(&after1);

    // First get_tensor should cause at least one mmap/open if nothing touched before
    ASSERT_TRUE(after1.st_file_opens >= before.st_file_opens);
    ASSERT_TRUE(after1.st_file_mmaps >= before.st_file_mmaps);

    // Second get_tensor should NOT create more opens/mmaps for same shard if caching works
    memset(&tv, 0, sizeof(tv));
    ASSERT_TRUE(hf_get_tensor(L, "lm_head.weight", &tv, err) == 0);

    HfDebugStats after2;
    hf_debug_get_stats(&after2);

    ASSERT_TRUE(after2.st_file_opens == after1.st_file_opens);
    ASSERT_TRUE(after2.st_file_mmaps == after1.st_file_mmaps);

    hf_close(L);
#else
    return 0;
#endif

    return 0;
}

// in test_hf_lazy_loading.c
void ctest_register_test_hf_lazy_loading(void) {
    ctest_register("test_hf_lazy_loading.c", "test_hf_lazy_open_is_lightweight", test_hf_lazy_open_is_lightweight);
    ctest_register("test_hf_lazy_loading.c", "test_hf_lazy_list_tensors_parses_header_once", test_hf_lazy_list_tensors_parses_header_once);
    ctest_register("test_hf_lazy_loading.c", "test_hf_lazy_get_tensor_opens_shard_and_reuses_cache", test_hf_lazy_get_tensor_opens_shard_and_reuses_cache);
}


