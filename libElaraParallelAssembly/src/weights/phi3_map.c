// src/weights/phi3_map.c
#include "phi3_map.h"
#include "hf_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int probe_exists(const HfLoader *L, const char *name, char err[256]) {
    AtTensorView tv;
    memset(&tv, 0, sizeof(tv));
    char e2[256] = {0};

    int rc = hf_get_tensor(L, name, &tv, e2);
    if (rc == 0) return 1;

    // Bubble up backend/IO problems; "not found" is normal during probing.
    if (err && e2[0]) {
        // Heuristic: if it contains "not implemented" or "open failed" or similar, treat as fatal.
        if (strstr(e2, "not implemented") || strstr(e2, "open failed") || strstr(e2, "mmap") || strstr(e2, "parse")) {
            snprintf(err, 256, "%s", e2);
        }
    }
    return 0;
}

static int tv_expect_f32_1d(const AtTensorView *tv, uint32_t d0, const char *name, char err[256]) {
    if (!tv) { snprintf(err, 256, "missing tensor view for %s", name); return -1; }
    if (tv->rank != 1) { snprintf(err, 256, "%s: expected rank=1, got %u", name, (unsigned)tv->rank); return -1; }
    if (tv->dims[0] != d0) { snprintf(err, 256, "%s: bad shape [%u] expected [%u]", name, (unsigned)tv->dims[0], (unsigned)d0); return -1; }
    return 0;
}

static int tv_expect_f32_2d(const AtTensorView *tv, uint32_t d0, uint32_t d1, const char *name, char err[256]) {
    if (!tv) { snprintf(err, 256, "missing tensor view for %s", name); return -1; }
    if (tv->rank != 2) { snprintf(err, 256, "%s: expected rank=2, got %u", name, (unsigned)tv->rank); return -1; }
    if (tv->dims[0] != d0 || tv->dims[1] != d1) {
        snprintf(err, 256, "%s: bad shape [%u,%u] expected [%u,%u]",
                 name, (unsigned)tv->dims[0], (unsigned)tv->dims[1],
                 (unsigned)d0, (unsigned)d1);
        return -1;
    }
    return 0;
}

static int get_tv(const HfLoader *L, const char *name, AtTensorView *tv, char err[256]) {
    memset(tv, 0, sizeof(*tv));
    int rc = hf_get_tensor(L, name, tv, err);
    if (rc != 0) return -1;
    return 0;
}

int phi3_map_keys(const HfLoader *L, Phi3KeyMap *out, char err[256]) {
    if (err) err[0] = 0;
    if (!L || !out) {
        if (err) snprintf(err, 256, "phi3_map_keys: invalid args");
        return -1;
    }

    memset(out, 0, sizeof(*out));

    // --- Determine root / variant by probing known Phi-3 sentinels ---
    // Prefer MODEL layout first, since your real dump shows model.layers.*.
    const char *root = NULL;
    if (probe_exists(L, "model.layers.0.self_attn.qkv_proj.weight", err)) {
        root = "model";
        out->variant = PHI3_NAMING_MODEL_LAYERS;
    } else if (probe_exists(L, "transformer.layers.0.self_attn.qkv_proj.weight", err)) {
        root = "transformer";
        out->variant = PHI3_NAMING_TRANSFORMER_LAYERS;
    } else {
        if (err && err[0]) {
            // backend error already set (e.g., not implemented / IO failure)
            snprintf(err, 256, "phi3_map_keys: %s", err);
            return -1;
        }
        if (err) snprintf(err, 256,
                          "phi3_map_keys: could not detect naming variant "
                          "(no sentinel qkv_proj found under model.layers.* or transformer.layers.*)");
        return -1;
    }

    snprintf(out->root, sizeof(out->root), "%s", root);

    // --- Validate globals expected in this checkpoint ---
    // embeddings: model.embed_tokens.weight is typical for Phi-3
    {
        char perr[256] = {0};
        if (!probe_exists(L, "model.embed_tokens.weight", perr)) {
            // allow alternative wte in case some repos differ
            if (!probe_exists(L, "model.wte.weight", perr) && perr[0]) {
                if (err) snprintf(err, 256, "phi3_map_keys: %s", perr);
                return -1;
            }
        }
    }

    // final norm: model.norm.weight (Phi-3)
    {
        char k_norm[256];
        snprintf(k_norm, sizeof(k_norm), "%s.norm.weight", root);
        char perr[256] = {0};
        if (!probe_exists(L, k_norm, perr)) {
            if (perr[0]) {
                if (err) snprintf(err, 256, "phi3_map_keys: %s", perr);
                return -1;
            }
            if (err) snprintf(err, 256, "phi3_map_keys: missing required tensor: %s", k_norm);
            return -1;
        }
    }

    // --- Determine QKV packed vs split ---
    out->qkv_packed = 1;
    {
        char k_q[256];
        snprintf(k_q, sizeof(k_q), "%s.layers.0.self_attn.q_proj.weight", root);
        if (probe_exists(L, k_q, NULL)) {
            out->qkv_packed = 0;
        }
    }

    // --- Determine LM head tied vs explicit ---
    out->lm_head_tied = 1;
    {
        if (probe_exists(L, "lm_head.weight", NULL)) out->lm_head_tied = 0;
    }

    // --- Determine MLP naming (Phi-3 often uses gate_up_proj fused) ---
    // We store this in the keymap using flags. If your Phi3KeyMap doesn’t have it,
    // reuse an existing spare field or add one later. For now we’ll infer at build time
    // by probing layer0.
    out->mlp_gate_up_fused = 0;
    {
        char k_fused[256];
        snprintf(k_fused, sizeof(k_fused), "%s.layers.0.mlp.gate_up_proj.weight", root);
        if (probe_exists(L, k_fused, NULL)) {
            out->mlp_gate_up_fused = 1;
        } else {
            char k_up[256], k_gate[256];
            snprintf(k_up,   sizeof(k_up),   "%s.layers.0.mlp.up_proj.weight", root);
            snprintf(k_gate, sizeof(k_gate), "%s.layers.0.mlp.gate_proj.weight", root);
            if (probe_exists(L, k_up, NULL) && probe_exists(L, k_gate, NULL)) {
                out->mlp_gate_up_fused = 0;
            } else {
                if (err) snprintf(err, 256,
                    "phi3_map_keys: could not detect MLP naming (need either mlp.gate_up_proj.weight or both mlp.up_proj.weight + mlp.gate_proj.weight)");
                return -1;
            }
        }
    }

    return 0;
}

int phi3_build_at_model_view(const HfLoader *L,
                             const Phi3KeyMap *km,
                             AtModelView *out,
                             char err[256])
{
    if (err) err[0] = 0;
    if (!L || !km || !out) {
        if (err) snprintf(err, 256, "phi3_build_at_model_view: invalid args");
        return -1;
    }

    const Phi3Config *cfg = hf_config(L);
    if (!cfg) {
        if (err) snprintf(err, 256, "phi3_build_at_model_view: hf_config returned NULL");
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->n_layers    = cfg->n_layers;
    out->hidden_dim  = cfg->hidden_dim;
    out->n_heads     = cfg->n_heads;
    out->head_dim    = cfg->head_dim;
    out->vocab_size  = cfg->vocab_size;
    out->mlp_dim     = cfg->mlp_dim;

    if (out->n_layers == 0 || out->hidden_dim == 0 || out->n_heads == 0 || out->vocab_size == 0 || out->mlp_dim == 0) {
        if (err) snprintf(err, 256, "phi3_build_at_model_view: config missing required dims");
        return -1;
    }

    out->layers = (AtLayerView*)calloc((size_t)out->n_layers, sizeof(AtLayerView));
    if (!out->layers) {
        if (err) snprintf(err, 256, "phi3_build_at_model_view: OOM layers");
        return -1;
    }

    // -------- Global tensors --------
    {
        AtTensorView tv;

        // embeddings
        char k_emb[256];
        snprintf(k_emb, sizeof(k_emb), "%s.embed_tokens.weight", km->root);
        if (get_tv(L, k_emb, &tv, err) != 0) {
            // fallback common alias
            snprintf(k_emb, sizeof(k_emb), "%s.wte.weight", km->root);
            if (get_tv(L, k_emb, &tv, err) != 0) goto fail;
        }
        if (tv_expect_f32_2d(&tv, out->vocab_size, out->hidden_dim, k_emb, err) != 0) goto fail;
        out->tok_emb = (const float*)tv.data;

        // final norm
        char k_norm[256];
        snprintf(k_norm, sizeof(k_norm), "%s.norm.weight", km->root);
        if (get_tv(L, k_norm, &tv, err) != 0) goto fail;
        if (tv_expect_f32_1d(&tv, out->hidden_dim, k_norm, err) != 0) goto fail;
        out->final_rms_w = (const float*)tv.data;

        // lm_head (optional)
        if (!km->lm_head_tied) {
            const char *k_lm = "lm_head.weight";
            if (get_tv(L, k_lm, &tv, err) != 0) goto fail;
            if (tv_expect_f32_2d(&tv, out->vocab_size, out->hidden_dim, k_lm, err) != 0) goto fail;
            out->lm_head_w = (const float*)tv.data;
        } else {
            out->lm_head_w = out->tok_emb;
        }
    }

    // -------- Per-layer tensors --------
    for (uint32_t i = 0; i < out->n_layers; i++) {
        AtLayerView *lv = &out->layers[i];
        AtTensorView tv;
        char k[256];

        // attn rmsnorm
        snprintf(k, sizeof(k), "%s.layers.%u.input_layernorm.weight", km->root, (unsigned)i);
        if (get_tv(L, k, &tv, err) != 0) goto fail;
        if (tv_expect_f32_1d(&tv, out->hidden_dim, k, err) != 0) goto fail;
        lv->attn_rms_w = (const float*)tv.data;

        // qkv or q/k/v
        if (km->qkv_packed) {
            snprintf(k, sizeof(k), "%s.layers.%u.self_attn.qkv_proj.weight", km->root, (unsigned)i);
            if (get_tv(L, k, &tv, err) != 0) goto fail;
            if (tv_expect_f32_2d(&tv, 3u * out->hidden_dim, out->hidden_dim, k, err) != 0) goto fail;
            lv->qkv_w = (const float*)tv.data;
        } else {
            snprintf(k, sizeof(k), "%s.layers.%u.self_attn.q_proj.weight", km->root, (unsigned)i);
            if (get_tv(L, k, &tv, err) != 0) goto fail;
            if (tv_expect_f32_2d(&tv, out->hidden_dim, out->hidden_dim, k, err) != 0) goto fail;
            lv->q_w = (const float*)tv.data;

            snprintf(k, sizeof(k), "%s.layers.%u.self_attn.k_proj.weight", km->root, (unsigned)i);
            if (get_tv(L, k, &tv, err) != 0) goto fail;
            if (tv_expect_f32_2d(&tv, out->hidden_dim, out->hidden_dim, k, err) != 0) goto fail;
            lv->k_w = (const float*)tv.data;

            snprintf(k, sizeof(k), "%s.layers.%u.self_attn.v_proj.weight", km->root, (unsigned)i);
            if (get_tv(L, k, &tv, err) != 0) goto fail;
            if (tv_expect_f32_2d(&tv, out->hidden_dim, out->hidden_dim, k, err) != 0) goto fail;
            lv->v_w = (const float*)tv.data;
        }

        // o proj
        snprintf(k, sizeof(k), "%s.layers.%u.self_attn.o_proj.weight", km->root, (unsigned)i);
        if (get_tv(L, k, &tv, err) != 0) goto fail;
        if (tv_expect_f32_2d(&tv, out->hidden_dim, out->hidden_dim, k, err) != 0) goto fail;
        lv->o_w = (const float*)tv.data;

        // ffn rmsnorm
        snprintf(k, sizeof(k), "%s.layers.%u.post_attention_layernorm.weight", km->root, (unsigned)i);
        if (get_tv(L, k, &tv, err) != 0) goto fail;
        if (tv_expect_f32_1d(&tv, out->hidden_dim, k, err) != 0) goto fail;
        lv->ffn_rms_w = (const float*)tv.data;

        // ---- MLP ----
        // Phi-3-mini uses fused: mlp.gate_up_proj.weight and mlp.down_proj.weight
        if (km->mlp_gate_up_fused) {
        	snprintf(k, sizeof(k), "%s.layers.%u.mlp.gate_up_proj.weight", km->root, (unsigned)i);
        	if (get_tv(L, k, &tv, err) != 0) goto fail;

        	// Phi-3 fused gate+up projection is stacked: [2*mlp_dim, hidden_dim]
        	if (tv_expect_f32_2d(&tv, 2u * out->mlp_dim, out->hidden_dim, k, err) != 0) goto fail;

        	// Store fused weight (gate||up). Interpretation happens in the AT later.
        	lv->up_w   = (const float*)tv.data;
        	lv->gate_w = NULL;
        } else {
            // split up + gate
            snprintf(k, sizeof(k), "%s.layers.%u.mlp.up_proj.weight", km->root, (unsigned)i);
            if (get_tv(L, k, &tv, err) != 0) goto fail;
            if (tv_expect_f32_2d(&tv, out->mlp_dim, out->hidden_dim, k, err) != 0) goto fail;
            lv->up_w = (const float*)tv.data;

            snprintf(k, sizeof(k), "%s.layers.%u.mlp.gate_proj.weight", km->root, (unsigned)i);
            if (get_tv(L, k, &tv, err) != 0) goto fail;
            if (tv_expect_f32_2d(&tv, out->mlp_dim, out->hidden_dim, k, err) != 0) goto fail;
            lv->gate_w = (const float*)tv.data;
        }

        // down
        snprintf(k, sizeof(k), "%s.layers.%u.mlp.down_proj.weight", km->root, (unsigned)i);
        if (get_tv(L, k, &tv, err) != 0) goto fail;
        if (tv_expect_f32_2d(&tv, out->hidden_dim, out->mlp_dim, k, err) != 0) goto fail;
        lv->down_w = (const float*)tv.data;
    }

    return 0;

fail:
    if (out->layers) free(out->layers);
    memset(out, 0, sizeof(*out));
    return -1;
}
