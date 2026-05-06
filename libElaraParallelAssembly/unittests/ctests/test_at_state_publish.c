// unittests/ctests/test_at_state_publish.c
#include "ctest.h"

#include "atomic_tasks/at_state.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

CTEST(test_at_state_publish_basic)
{
    char err[256] = {0};

    // Minimal fake weights (we only test publishing / pointer stability here)
    static float emb[8];
    static float norm[4];
    static float lm[8];

    // Per-layer views
    static AtLayerView layers[2];
    memset(layers, 0, sizeof(layers));

    // Populate a minimally-valid model view
    AtModelView v;
    memset(&v, 0, sizeof(v));

    v.n_layers    = 2;
    v.hidden_dim  = 4;
    v.n_heads     = 1;
    v.head_dim    = 4;
    v.vocab_size  = 2;
    v.mlp_dim     = 8;

    v.tok_emb     = emb;
    v.final_rms_w = norm;
    v.lm_head_w   = lm;     // can be tied to emb in real life, but must be non-null for publish
    v.layers      = layers;

    // Publish model_id=0
    ASSERT_TRUE(at_state_publish_model(0, &v, err) == 0);

    // Read back
    const AtModelView *r = at_state_get_model(0);
    ASSERT_TRUE(r != NULL);

    ASSERT_TRUE(r->n_layers == v.n_layers);
    ASSERT_TRUE(r->layers == layers);

    ASSERT_TRUE(r->tok_emb     == emb);
    ASSERT_TRUE(r->final_rms_w == norm);
    ASSERT_TRUE(r->lm_head_w   == lm);

    return 0;
}

CTEST(test_at_state_publish_rejects_invalid)
{
    char err[256] = {0};

    AtModelView v;
    memset(&v, 0, sizeof(v));

    // invalid: n_layers=0 and layers=NULL
    v.tok_emb     = (const float*)0x1;
    v.final_rms_w = (const float*)0x1;
    v.lm_head_w   = (const float*)0x1;

    ASSERT_TRUE(at_state_publish_model(0, &v, err) != 0);

    // invalid: layers NULL with n_layers non-zero
    memset(&v, 0, sizeof(v));
    v.n_layers    = 1;
    v.layers      = NULL;
    v.tok_emb     = (const float*)0x1;
    v.final_rms_w = (const float*)0x1;
    v.lm_head_w   = (const float*)0x1;

    ASSERT_TRUE(at_state_publish_model(0, &v, err) != 0);

    // invalid: missing global weights
    memset(&v, 0, sizeof(v));
    v.n_layers = 1;
    v.layers   = (AtLayerView*)0x1; // dummy non-null
    v.tok_emb  = NULL;
    v.final_rms_w = (const float*)0x1;
    v.lm_head_w   = (const float*)0x1;

    ASSERT_TRUE(at_state_publish_model(0, &v, err) != 0);

    return 0;
}

void ctest_register_test_at_state_publish(void)
{
    const char *F = "test_at_state_publish.c";
    REG(F, test_at_state_publish_basic);
    REG(F, test_at_state_publish_rejects_invalid);
}

