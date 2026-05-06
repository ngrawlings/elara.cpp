// src/weights/hf_loader.h
#pragma once

#include "../atomic_tasks/at_state.h"
#include "hf_tensor_index.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HfBackend {
    HF_BACKEND_NONE = 0,
    HF_BACKEND_SAFETENSORS = 1,
    HF_BACKEND_PYTORCH_BIN = 2
} HfBackend;

typedef struct {
  uint32_t n_layers;
  uint32_t hidden_dim;
  uint32_t n_heads;
  uint32_t head_dim;
  uint32_t vocab_size;
  uint32_t mlp_dim;

  // Optional
  float rope_theta;
  uint32_t max_position_embeddings;
} Phi3Config;

typedef struct HfLoader HfLoader;

// Open a HF folder: reads config.json, determines backend, and prepares shard access.
// Skeleton: config parsing is stubbed; backend detection is stubbed.
int hf_open_model_dir(HfLoader **out, const char *model_dir, char err[256]);
void hf_close(HfLoader *L);

const Phi3Config *hf_config(const HfLoader *L);

// Lookup a tensor by HF key.
// Skeleton: always returns not found until safetensors parsing added.
int hf_get_tensor(const HfLoader *L, const char *tensor_name, AtTensorView *out_tv, char err[256]);

// Enumerate available tensor keys.
// Skeleton: returns 0 keys until implemented.
typedef void (*hf_list_cb)(void *user, const char *key);
int hf_list_tensors(const HfLoader *L, hf_list_cb cb, void *user, char err[256]);

#ifdef __cplusplus
}
#endif
