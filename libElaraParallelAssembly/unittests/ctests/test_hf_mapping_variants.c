// unittests/ctests/test_hf_mapping_variants.c
#include "ctest.h"

#include "weights/phi3_map.h"
#include "weights/hf_loader.h"

#include <string.h>

CTEST(test_phi3_map_keys_rejects_null_loader)
{
    Phi3KeyMap map;
    char err[256];

    ASSERT_TRUE(phi3_map_keys(NULL, &map, err) != 0);
    return 0;
}

CTEST(test_phi3_map_keys_rejects_null_output)
{
    char err[256];
    HfLoader *L = (HfLoader*)0x1; // dummy non-null pointer

    ASSERT_TRUE(phi3_map_keys(L, NULL, err) != 0);
    return 0;
}

void ctest_register_test_hf_mapping_variants(void)
{
    const char *F = "test_hf_mapping_variants.c";
    REG(F, test_phi3_map_keys_rejects_null_loader);
    REG(F, test_phi3_map_keys_rejects_null_output);
}
