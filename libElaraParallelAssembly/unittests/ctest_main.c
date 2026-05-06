// unittests/ctest_main.c
#include "ctest.h"

/* per-file registration functions */
void ctest_register_test_ring(void);
void ctest_register_test_ghs(void);
void ctest_register_test_stack(void);

void ctest_register_test_at_embed_gather(void);
void ctest_register_test_at_flash_attn(void);
void ctest_register_test_at_kv_cache(void);
void ctest_register_test_at_linear_o(void);
void ctest_register_test_at_lm_head_linear(void);
void ctest_register_test_at_mlp_fused(void);
void ctest_register_test_at_residual_add(void);
void ctest_register_test_at_rmsnorm_final(void);
void ctest_register_test_at_rmsnorm_qkv_rope_fused(void);
void ctest_register_test_at_sample_rng(void);
void ctest_register_test_at_topk(void);
void ctest_register_test_at_router_thread_pool(void);

void ctest_register_test_at_state_publish(void);

void ctest_register_test_hf_mapping_variants(void);
void ctest_register_test_hf_loader_phi3_minimal(void);
void ctest_register_test_hf_dump_state(void);
void ctest_register_test_hf_lazy_loading(void);

typedef void (*ctest_reg_fn)(void);

static const struct {
    const char *file;      // purely informational
    ctest_reg_fn reg;
} groups[] = {
    { "test_ring_buffer.c", ctest_register_test_ring },
    { "test_ghs.c",         ctest_register_test_ghs  },
    { "test_stack.c",       ctest_register_test_stack },

    { "test_at_embed_gather.c",      ctest_register_test_at_embed_gather },
    { "test_at_flash_attn.c",        ctest_register_test_at_flash_attn   },
    { "test_at_kv_cache.c",          ctest_register_test_at_kv_cache     },
    { "test_at_linear_o.c",          ctest_register_test_at_linear_o     },
    { "test_at_lm_head_linear.c",    ctest_register_test_at_lm_head_linear },
    { "test_at_mlp_fused.c",         ctest_register_test_at_mlp_fused    },
    { "test_at_residual_add.c",      ctest_register_test_at_residual_add },
    { "test_at_rmsnorm_final.c",     ctest_register_test_at_rmsnorm_final },
    { "test_at_rmsnorm_qkv_rope_fused.c",
                                      ctest_register_test_at_rmsnorm_qkv_rope_fused },
    { "test_at_sample_rng.c",        ctest_register_test_at_sample_rng   },
    { "test_at_topk.c",              ctest_register_test_at_topk         },
    { "test_at_router_thread_pool.c", ctest_register_test_at_router_thread_pool },

    // ---- AT global state / HF loader / integration ----
    { "test_at_state_publish.c",     ctest_register_test_at_state_publish },
    { "test_hf_mapping_variants.c",  ctest_register_test_hf_mapping_variants },
    { "test_hf_loader_phi3_minimal.c",
                                      ctest_register_test_hf_loader_phi3_minimal },
    { "test_hf_dump_state.c",        ctest_register_test_hf_dump_state    },
	{ "test_hf_lazy_loading.c",     ctest_register_test_hf_lazy_loading }

};


int main(void) {
    // Register all groups in desired order
    for (unsigned i = 0; i < sizeof(groups)/sizeof(groups[0]); i++) {
        groups[i].reg();
    }

    int fails = ctest_run_all();
    return fails ? 1 : 0;
}
