#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
  const uint8_t *code;
  size_t code_len;
  uint32_t abs_base;   // debug only
} EpaCodeView;

typedef struct {
  uint32_t func_id;
  uint16_t frame_words; // required arg stack words (verification only)
  uint16_t _pad;
  EpaCodeView code;
} EpaFuncDesc;

typedef enum {
  EPA_CONST_NONE  = 0,
  EPA_CONST_STR   = 1,
  EPA_CONST_I32   = 2,
  EPA_CONST_U32   = 3,
  EPA_CONST_I64   = 4,
  EPA_CONST_U64   = 5,
  EPA_CONST_F32   = 6,
  EPA_CONST_F64   = 7,
  EPA_CONST_BYTES = 8,
  EPA_CONST_TMP_STR = 9
} EpaConstKind;

typedef struct {
  uint32_t id;
  uint8_t  kind;
  uint8_t  flags;
  uint16_t aux;     // e.g. string length (optional) or reserved
  uint32_t a;       // meaning depends on kind (offset / low bits / value)
  uint32_t b;       // meaning depends on kind (len / high bits / value)
} EpaConst;


typedef struct {
  EpaCodeView entries[256];   // entry slots (present -> code_len > 0)
  uint8_t entry_present[256];

  // ENTRY_START metadata (declared word capacities for sync payloads)
  uint16_t entry_in_words[256];
  uint16_t entry_out_words[256];
  uint32_t signal_mailbox_size[256];

  EpaConst *consts;
  size_t nconsts;

  EpaFuncDesc *funcs;         // dense
  size_t nfuncs;

  // ---- NEW: backing image for absolute offsets (DATA_BLOCK strings/bytes) ----
  const uint8_t *image_base;   // caller-owned; must remain alive
  size_t         image_size;
} EpaProgramDesc;

static inline int epa_prog_resolve(
    const EpaProgramDesc *p,
    uint8_t block_type, uint16_t block_id,
    const uint8_t **out_code, size_t *out_len
) {
  if (!p || !out_code || !out_len) return 0;

  if (block_type == 0 /*EPA_BLOCK_ENTRY*/) {
    if (block_id >= 256) return 0;
    if (!p->entry_present[block_id]) return 0;
    *out_code = p->entries[block_id].code;
    *out_len  = p->entries[block_id].code_len;
    return 1;
  } else if (block_type == 1 /*EPA_BLOCK_FUNC*/) {
    if (block_id >= p->nfuncs) return 0;
    *out_code = p->funcs[block_id].code.code;
    *out_len  = p->funcs[block_id].code.code_len;
    return 1;
  }
  return 0;
}

// Resolve function by func_id -> returns dense func index and metadata.
static inline int epa_prog_find_func(
    const EpaProgramDesc *p,
    uint32_t func_id,
    uint16_t *out_func_index,
    uint16_t *out_frame_words
) {
  if (!p) return 0;
  for (size_t i = 0; i < p->nfuncs; i++) {
    if (p->funcs[i].func_id == func_id) {
      if (out_func_index) *out_func_index = (uint16_t)i;
      if (out_frame_words) *out_frame_words = p->funcs[i].frame_words;
      return 1;
    }
  }
  return 0;
}
