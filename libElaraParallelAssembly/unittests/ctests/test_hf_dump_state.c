// unittests/ctests/test_hf_dump_state.c
#include "ctest.h"

#include "weights/hf_loader.h"
#include "weights/phi3_map.h"
#include "atomic_tasks/at_state.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    uint32_t count;
} KeyDumpCtx;

typedef struct {
    HfLoader   *L;
    Phi3KeyMap  map;
    AtModelView view;
    uint32_t    published_model_id;
} Phi3TestState;

static void phi3_test_state_dtor(void *p);   // <-- add this

static void dump_key_cb(void *user, const char *key) {
    KeyDumpCtx *ctx = (KeyDumpCtx*)user;
    ctx->count++;
    printf("  [%6u] %s\n", (unsigned)ctx->count, key);
}

static const char *variant_str(Phi3NamingVariant v) {
    switch (v) {
        case PHI3_NAMING_TRANSFORMER_LAYERS: return "transformer.layers.{i}.*";
        case PHI3_NAMING_MODEL_LAYERS:       return "model.layers.{i}.*";
        default:                             return "unknown";
    }
}

// helper
static void dump_layer_ptrs(const AtModelView *v, uint32_t max_layers) {
    uint32_t n = v->n_layers;
    if (n > max_layers) n = max_layers;

    for (uint32_t i = 0; i < n; i++) {
        const AtLayerView *lw = &v->layers[i];
        printf("  layer[%u] attn_rms=%p qkv=%p o=%p ffn_rms=%p up=%p gate=%p down=%p\n",
               (unsigned)i,
               (void*)lw->attn_rms_w,
               (void*)lw->qkv_w,
               (void*)lw->o_w,
               (void*)lw->ffn_rms_w,
               (void*)lw->up_w,
               (void*)lw->gate_w,
               (void*)lw->down_w);
    }
}

CTEST(test_hf_dump_all_state_once)
{
    const char *dir = getenv("EPA_TEST_MODEL_DIR");
    if (!dir || !dir[0]) {
        return 0; // skip/pass
    }

    char err[256];
    err[0] = 0;

    printf("========== HF/AT STATE DUMP BEGIN ==========\n");
    printf("model_dir: %s\n", dir);

    // ---- Open loader ----
    HfLoader *L = NULL;
    int rc = hf_open_model_dir(&L, dir, err);
    if (rc != 0 || !L) {
        printf("hf_open_model_dir FAILED: %s\n", err);
        printf("========== HF/AT STATE DUMP END (open failed) ==========\n");
        ASSERT_TRUE(0);
        return 0;
    }
    printf("hf_open_model_dir: OK\n");

    // ---- Config dump ----
    const Phi3Config *cfg = hf_config(L);
    ASSERT_TRUE(cfg != NULL);

    printf("\n-- config.json (parsed) --\n");
    printf("  n_layers   = %u\n", (unsigned)cfg->n_layers);
    printf("  hidden_dim = %u\n", (unsigned)cfg->hidden_dim);
    printf("  n_heads    = %u\n", (unsigned)cfg->n_heads);
    printf("  head_dim   = %u\n", (unsigned)cfg->head_dim);
    printf("  vocab_size = %u\n", (unsigned)cfg->vocab_size);
    printf("  mlp_dim    = %u\n", (unsigned)cfg->mlp_dim);
    printf("  rope_theta = %f\n", (double)cfg->rope_theta);
    printf("  max_pos    = %u\n", (unsigned)cfg->max_position_embeddings);

    // ---- Key listing ----
    printf("\n-- tensor keys (hf_list_tensors) --\n");
    KeyDumpCtx kctx;
    memset(&kctx, 0, sizeof(kctx));

    err[0] = 0;
    rc = hf_list_tensors(L, dump_key_cb, &kctx, err);
    if (rc != 0) {
        printf("hf_list_tensors: FAILED: %s\n", err);
        printf("========== HF/AT STATE DUMP END (list failed) ==========\n");
        hf_close(L);
        ASSERT_TRUE(0);
        return 0;
    } else {
        printf("hf_list_tensors: OK (keys=%u)\n", (unsigned)kctx.count);
    }

    // ---- Variant map ----
    printf("\n-- phi3_map_keys (variant detection) --\n");
    Phi3KeyMap map;
    memset(&map, 0, sizeof(map));

    err[0] = 0;
    rc = phi3_map_keys(L, &map, err);
    if (rc != 0) {
        printf("phi3_map_keys: FAILED: %s\n", err);
        printf("========== HF/AT STATE DUMP END (map failed) ==========\n");
        hf_close(L);
        ASSERT_TRUE(0);
        return 0;
    }

    printf("phi3_map_keys: OK\n");
    printf("  variant        = %s\n", variant_str(map.variant));
    printf("  has_packed_qkv = %d\n", map.qkv_packed);
    printf("  ties_lm_head   = %d\n", map.lm_head_tied);

    // ---- Build model view ----
    printf("\n-- phi3_build_at_model_view (resolve + validate) --\n");
    AtModelView view;
    memset(&view, 0, sizeof(view));

    err[0] = 0;
    rc = phi3_build_at_model_view(L, &map, &view, err);
    if (rc != 0) {
        printf("phi3_build_at_model_view: FAILED: %s\n", err);
        printf("========== HF/AT STATE DUMP END (build failed) ==========\n");
        hf_close(L);
        ASSERT_TRUE(0);
        return 0;
    }

    printf("phi3_build_at_model_view: OK\n");
    printf("  view.n_layers   = %u\n", (unsigned)view.n_layers);
    printf("  view.hidden_dim = %u\n", (unsigned)view.hidden_dim);
    printf("  view.n_heads    = %u\n", (unsigned)view.n_heads);
    printf("  view.head_dim   = %u\n", (unsigned)view.head_dim);
    printf("  view.vocab_size = %u\n", (unsigned)view.vocab_size);
    printf("  view.mlp_dim    = %u\n", (unsigned)view.mlp_dim);
    printf("  ptr.tok_emb      = %p\n", (void*)view.tok_emb);
    printf("  ptr.final_rms_w  = %p\n", (void*)view.final_rms_w);
    printf("  ptr.lm_head_w    = %p\n", (void*)view.lm_head_w);
    printf("  ptr.layers       = %p\n", (void*)view.layers);

    // ---- Publish into at_state ----
    printf("\n-- at_state_publish_model (slot=0) --\n");
    uint32_t model_id = 0;
    err[0] = 0;
    rc = at_state_publish_model(model_id, &view, err);
    if (rc != 0) {
        printf("at_state_publish_model: FAILED: %s\n", err);
        printf("========== HF/AT STATE DUMP END (publish failed) ==========\n");
        // view.layers was malloc'd by phi3_build_at_model_view
        free(view.layers);
        hf_close(L);
        ASSERT_TRUE(0);
        return 0;
    }
    printf("at_state_publish_model: OK\n");

    // ---- Publish shared ctest state for later tests ----
    Phi3TestState *S = (Phi3TestState*)calloc(1, sizeof(*S));
    if (!S) {
        printf("ctest shared state alloc FAILED (OOM)\n");
        printf("========== HF/AT STATE DUMP END (oom) ==========\n");
        // Still clean up: view.layers + loader
        free(view.layers);
        hf_close(L);
        ASSERT_TRUE(0);
        return 0;
    }

    S->L = L;
    S->map = map;
    S->view = view;                 // shallow copy OK; owns layers pointer
    S->published_model_id = model_id;

    ctest_state_set(S, phi3_test_state_dtor);

    printf("\n========== HF/AT STATE DUMP END ==========\n");
    return 0;
}

CTEST(test_phi3_state_is_available)
{
    Phi3TestState *S = (Phi3TestState*)ctest_state_get();
    ASSERT_TRUE(S != NULL);

    const AtModelView *mv = at_state_get_model(S->published_model_id);
    ASSERT_TRUE(mv != NULL);
    ASSERT_TRUE(mv->tok_emb != NULL);

    return 0;
}

static void phi3_test_state_dtor(void *p)
{
    Phi3TestState *s = (Phi3TestState*)p;
    if (!s) return;
    if (s->view.layers) free(s->view.layers);
    if (s->L) hf_close(s->L);
    free(s);
}

// --- registry function (for your linked-list test registry) ---
void ctest_register_test_hf_dump_state(void)
{
    const char *F = "test_hf_dump_state.c";
    ctest_register(F, "test_hf_dump_all_state_once", test_hf_dump_all_state_once);
    ctest_register(F, "test_phi3_state_is_available", test_phi3_state_is_available);
}


