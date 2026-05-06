// src/weights/phi3_map.h
#pragma once

#include "hf_loader.h"
#include "../atomic_tasks/at_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PHI3_NAMING_UNKNOWN = 0,
  PHI3_NAMING_TRANSFORMER_LAYERS = 1, // transformer.layers.{i}.*
  PHI3_NAMING_MODEL_LAYERS       = 2, // model.layers.{i}.*
} Phi3NamingVariant;

typedef struct Phi3KeyMap {
    Phi3NamingVariant variant;
    char root[32];
    int qkv_packed;
    int lm_head_tied;
    int mlp_gate_up_fused;   // <-- add this
} Phi3KeyMap;

// Probe keyset (via hf_get_tensor probes / hf_list_tensors later) and decide naming variant.
// Skeleton: returns UNKNOWN until implemented.
int phi3_map_keys(const HfLoader *L, Phi3KeyMap *out_map, char err[256]);

// Build AtModelView by resolving all required tensors and validating shapes.
// Skeleton: structure is complete; tensor resolves are TODO.
int phi3_build_at_model_view(const HfLoader *L, const Phi3KeyMap *map,
                             AtModelView *out_view, char err[256]);

#ifdef __cplusplus
}
#endif
