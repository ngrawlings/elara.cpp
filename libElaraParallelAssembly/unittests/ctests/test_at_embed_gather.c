// unittests/ctests/test_at_embed_gather.c
#include "../ctest.h"

#include <stdint.h>
#include <string.h>

// Adjust include paths if needed (depends on how you wire include dirs in Makefile)
#include "../../src/memory/epa_ghs.h"
#include "../../src/atomic_tasks/epa_atomic_tasks.h"
#include "../../src/atomic_tasks/at_embed_gather.h"

// Exported test entrypoint (add this to ctest_main.c's table)
CTEST(test_at_embed_gather_basic)
{
  // ----------------------------
  // 1) Create GHS
  // ----------------------------
  epa_ghs_t *ghs = epa_ghs_create(/*max_entries=*/32, /*alloc=*/NULL, /*free=*/NULL, /*user=*/NULL);
  ASSERT_TRUE(ghs != NULL);

  // ----------------------------
  // 2) Register AT + embedding table (global RO)
  // ----------------------------
  at_embed_gather_register();

  // Small deterministic embedding table
  // vocab=8, d_model=4
  enum { VOCAB = 8, D = 4 };
  static float EMB[VOCAB * D];

  for (uint32_t tok = 0; tok < VOCAB; tok++) {
    for (uint32_t j = 0; j < D; j++) {
      // Easy-to-check pattern:
      // row tok = [tok+0.01, tok+0.02, tok+0.03, tok+0.04]
      EMB[tok * D + j] = (float)tok + 0.01f * (float)(j + 1);
    }
  }

  ASSERT_TRUE(epa_at_register_embed_table(/*table_id=*/1, EMB, VOCAB, D) == EPA_AT_OK);

  // ----------------------------
  // 3) Build a GHS payload:
  //    [desc][tokens][out]
  // ----------------------------
  const uint32_t token_count = 3;
  const uint32_t tokens_off = (uint32_t)sizeof(AtEmbedGatherDesc);
  const uint32_t out_off = tokens_off + token_count * 4u;

  const uint32_t payload_bytes =
      (uint32_t)sizeof(AtEmbedGatherDesc) +
      token_count * 4u +
      token_count * D * 4u;

  epa_ghs_handle_t h = 0;
  ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, /*owner=*/0, payload_bytes, &h) == EPA_GHS_OK);

  void *base = NULL;
  ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
  ASSERT_TRUE(base != NULL);

  // Descriptor at offset 0
  AtEmbedGatherDesc desc;
  memset(&desc, 0, sizeof(desc));
  desc.embed_table_id = 1;
  desc.token_count    = token_count;
  desc.tokens_off     = tokens_off;
  desc.out_off        = out_off;
  desc.d_model        = D;        // optional sanity field

  memcpy((uint8_t*)base + EPA_AT_DESC_OFFSET, &desc, sizeof(desc));

  // Tokens
  uint32_t *tokens = (uint32_t*)((uint8_t*)base + tokens_off);
  tokens[0] = 2;
  tokens[1] = 7;
  tokens[2] = 1;

  // Clear out buffer
  float *out = (float*)((uint8_t*)base + out_off);
  memset(out, 0, token_count * D * sizeof(float));

  // ----------------------------
  // 4) Run AT_EMBED_GATHER
  // ----------------------------
  ASSERT_TRUE(epa_at_run(AT_EMBED_GATHER, ghs, h) == EPA_AT_OK);

  // ----------------------------
  // 5) Validate output
  // ----------------------------
  for (uint32_t i = 0; i < token_count; i++) {
    uint32_t tok = tokens[i];
    const float *row = &EMB[tok * D];

    for (uint32_t j = 0; j < D; j++) {
      // exact equality is fine here (copies only)
      ASSERT_TRUE(out[i * D + j] == row[j]);
    }
  }

  // ----------------------------
  // 6) Cleanup
  // ----------------------------
  ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
  epa_ghs_destroy(ghs);

  return 0;
}

void ctest_register_test_at_embed_gather(void) {
    const char *F = "test_at_embed_gather.c";
    REG(F, test_at_embed_gather_basic);
}
