// src/weights/hf_tensor_index.h
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HfTensorIndex HfTensorIndex;

// Same callback shape as hf_list_tensors()
typedef void (*hf_list_cb)(void *user, const char *key);

// Load Hugging Face *.index.json (safetensors/pytorch sharded)
int hf_index_load_json(HfTensorIndex **out, const char *index_json_path, char err[256]);

// For safetensors sharded layout: map tensor_name -> shard relative filename (e.g. model-00001-of-00005.safetensors)
int hf_index_find_safetensors_shard(const HfTensorIndex *ix,
                                    const char *tensor_name,
                                    char *out_relpath,
                                    size_t out_sz,
                                    char err[256]);

// Iterate all tensor names known to the index (typically the keys of weight_map)
int hf_index_list_names(const HfTensorIndex *ix, hf_list_cb cb, void *user, char err[256]);

// If you already have these, keep them:
void hf_index_destroy(HfTensorIndex *ix);

#ifdef __cplusplus
}
#endif
