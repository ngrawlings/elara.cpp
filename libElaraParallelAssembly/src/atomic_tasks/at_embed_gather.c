// atomic_tasks/at_embed_gather.c
#include "at_embed_gather.h"
#include <string.h>

static inline uint8_t* ptr_add_u8(void* p, uint32_t off) {
  return ((uint8_t*)p) + off;
}

EpaAtRc at_embed_gather_cpu(epa_ghs_t* ghs, epa_ghs_handle_t h) {
  if (!ghs) return EPA_AT_ERR_NULL;

  // Validate & get payload pointer
  if (epa_ghs_validate(ghs, h) != EPA_GHS_OK) return EPA_AT_ERR_BAD_HANDLE;

  void* base = NULL;
  if (epa_ghs_get_ptr(ghs, h, &base) != EPA_GHS_OK || !base) return EPA_AT_ERR_BAD_HANDLE;

  // Read descriptor
  AtEmbedGatherDesc desc;
  memcpy(&desc, ptr_add_u8(base, EPA_AT_DESC_OFFSET), sizeof(desc));

  if (desc.token_count == 0) return EPA_AT_ERR_BAD_DESC;
  if (desc.tokens_off == 0 && desc.token_count) return EPA_AT_ERR_BAD_DESC;
  if (desc.out_off == 0) return EPA_AT_ERR_BAD_DESC;

  // Lookup embedding table (global RO weights)
  EpaAtEmbedTable t;
  EpaAtRc rc = epa_at_get_embed_table(desc.embed_table_id, &t);
  if (rc != EPA_AT_OK) return rc;

  if (desc.d_model != 0 && desc.d_model != t.d_model) return EPA_AT_ERR_BAD_DESC;

  // Bounds check against GHS size
  epa_ghs_meta_t meta;
  if (epa_ghs_get_meta(ghs, h, &meta) != EPA_GHS_OK) return EPA_AT_ERR_BAD_HANDLE;

  uint64_t tokens_bytes = (uint64_t)desc.token_count * 4ull;
  uint64_t out_bytes    = (uint64_t)desc.token_count * (uint64_t)t.d_model * 4ull;

  if ((uint64_t)desc.tokens_off + tokens_bytes > meta.size_bytes) return EPA_AT_ERR_OOB;
  if ((uint64_t)desc.out_off + out_bytes > meta.size_bytes) return EPA_AT_ERR_OOB;

  const uint32_t* tokens = (const uint32_t*)ptr_add_u8(base, desc.tokens_off);
  float* out            = (float*)ptr_add_u8(base, desc.out_off);

  // Gather rows
  for (uint32_t i = 0; i < desc.token_count; i++) {
    uint32_t tok = tokens[i];
    if (tok >= t.vocab) return EPA_AT_ERR_OOB;

    const float* row = t.data + ((uint64_t)tok * (uint64_t)t.d_model);
    memcpy(out + ((uint64_t)i * (uint64_t)t.d_model), row, (size_t)t.d_model * 4u);
  }

  return EPA_AT_OK;
}
