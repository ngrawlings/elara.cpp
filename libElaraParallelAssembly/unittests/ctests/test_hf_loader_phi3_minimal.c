// unittests/ctests/test_hf_loader_phi3_minimal.c
#include "ctest.h"

#include "weights/hf_loader.h"
#include "weights/phi3_map.h"
#include "atomic_tasks/at_state.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

CTEST(test_hf_loader_phi3_minimal)
{
    const char *dir = getenv("EPA_TEST_MODEL_DIR");
    if (!dir || !dir[0]) {
        printf("env EPA_TEST_MODEL_DIR not set, skipping test_hf_loader_phi3_minimal test\n");
        return 0;
    }

    char err[256];

    HfLoader *L = NULL;
    ASSERT_TRUE(hf_open_model_dir(&L, dir, err) == 0);
    ASSERT_TRUE(L != NULL);

    Phi3KeyMap map;
    memset(&map, 0, sizeof(map));
    ASSERT_TRUE(phi3_map_keys(L, &map, err) == 0);

    AtModelView view;
    memset(&view, 0, sizeof(view));
    ASSERT_TRUE(phi3_build_at_model_view(L, &map, &view, err) == 0);

    ASSERT_TRUE(at_state_publish_model(0, &view, err) == 0);

    const AtModelView *r = at_state_get_model(0);
    ASSERT_TRUE(r != NULL);

    // Once implemented for real, these should always be non-null:
    ASSERT_TRUE(r->tok_emb != NULL);
    ASSERT_TRUE(r->final_rms_w != NULL);
    ASSERT_TRUE(r->layers != NULL);
    ASSERT_TRUE(r->n_layers > 0);
    ASSERT_TRUE(r->hidden_dim > 0);
    ASSERT_TRUE(r->vocab_size > 0);

    hf_close(L);

    return 0;
}

void ctest_register_test_hf_loader_phi3_minimal(void)
{
    const char *F = "test_hf_loader_phi3_minimal.c";
    REG(F, test_hf_loader_phi3_minimal);
}

