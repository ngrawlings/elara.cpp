// src/atomic_tasks/at_state.h
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifndef AT_MAX_MODELS
#define AT_MAX_MODELS 8
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  AT_DTYPE_INVALID = 0,
  AT_DTYPE_F32     = 1,
  AT_DTYPE_F16     = 2,
  AT_DTYPE_BF16    = 3,
  AT_DTYPE_I32     = 4,
  AT_DTYPE_U8      = 5,
} AtDType;

// Lightweight tensor view: shape + pointer into some backing store (mmap/heap).
typedef struct {
  AtDType dtype;
  uint32_t rank;
  int64_t  dims[8];
  const void *data;
  size_t nbytes;
} AtTensorView;

// Per-layer pointers for Phi-3 style blocks.
typedef struct {
  const float *attn_rms_w;   // [hidden_dim]
  const float *qkv_w;        // packed: [3*hidden_dim, hidden_dim] OR NULL if separate
  const float *q_w;          // [hidden_dim, hidden_dim] optional
  const float *k_w;          // [hidden_dim, hidden_dim] optional
  const float *v_w;          // [hidden_dim, hidden_dim] optional
  const float *o_w;          // [hidden_dim, hidden_dim]

  const float *ffn_rms_w;    // [hidden_dim]
  const float *mlp_up_w;     // [mlp_dim, hidden_dim]
  const float *mlp_gate_w;   // [mlp_dim, hidden_dim] optional (gated MLP)
  const float *mlp_down_w;   // [hidden_dim, mlp_dim]

  // Optional biases (usually NULL)
  const float *qkv_b;
  const float *o_b;
  const float *mlp_up_b;
  const float *mlp_gate_b;
  const float *mlp_down_b;
} AtLayerWeights;

// atomic_tasks/at_state.h (or weights/phi3_map.h if you keep it local)
typedef struct {
    const float *attn_rms_w;   // [hidden]
    const float *ffn_rms_w;    // [hidden]

    // attention projections
    const float *qkv_w;        // [3*hidden, hidden] if packed, else NULL
    const float *q_w;          // [hidden, hidden] if split
    const float *k_w;
    const float *v_w;
    const float *o_w;          // [hidden, hidden]

    // MLP
    const float *up_w;         // [mlp, hidden]
    const float *gate_w;       // [mlp, hidden] (optional but Phi-3 uses gated)
    const float *down_w;       // [hidden, mlp]
} AtLayerView;

typedef struct AtModelView {
    uint32_t n_layers, n_heads, head_dim, hidden_dim, vocab_size, mlp_dim;

    const float *tok_emb;
    const float *final_rms_w;
    const float *lm_head_w;

    AtLayerView *layers;
} AtModelView;

// Global AT state table = CPU pointer cache for v1.
typedef struct {
  uint32_t valid;
  AtModelView view;      // shallow copy of pointers
} AtModelSlot;

typedef struct {
  AtModelSlot models[AT_MAX_MODELS];
} AtState;

extern AtState g_at_state;

// Publish API (kernel calls only at quiescence).
// This does a shallow copy of pointers and constants into the global table.
// Caller must ensure backing store stays alive as long as slot is in use.
int at_state_publish_model(uint32_t model_id, const AtModelView *view, char err[256]);

const AtModelView *at_state_get_model(uint32_t model_id);

#ifdef __cplusplus
}
#endif
