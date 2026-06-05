#pragma once
#include <stdint.h>
#include <stddef.h>

#include "epa_opcode_parameter_values.h"

typedef struct {
  uint8_t opcode;
  const char *name;     // mnemonic
  uint8_t param_len;    // bytes AFTER opcode; 0xFF means variable length
} EpaOpcodeDef;

#define EPA_OPCODE_BYTES 1u
#define EPA_OPCODE_EXT_ESCAPE 255u

/* Safe little-endian reads (no unaligned loads / aliasing UB) */
#define EPA_READ_U16_LE(buf, off) \
  ((uint16_t)((uint8_t)(buf)[(off)]) | ((uint16_t)((uint8_t)(buf)[(off) + 1]) << 8))

#define EPA_READ_U32_LE(buf, off) \
  ((uint32_t)((uint8_t)(buf)[(off)]) | ((uint32_t)((uint8_t)(buf)[(off) + 1]) << 8) | \
   ((uint32_t)((uint8_t)(buf)[(off) + 2]) << 16) | ((uint32_t)((uint8_t)(buf)[(off) + 3]) << 24))

#define X(name, value, mnemonic, plen) \
  { (uint8_t)(value), (mnemonic), (uint8_t)(plen) },

static const EpaOpcodeDef EPA_OPCODE_TABLE[] = {
#include "epa_opcodes.x"
};

#undef X

static const size_t EPA_OPCODE_TABLE_COUNT =
    sizeof(EPA_OPCODE_TABLE) / sizeof(EPA_OPCODE_TABLE[0]);

static inline const EpaOpcodeDef *epa_find_opcode(uint8_t op) {
  for (size_t i = 0; i < EPA_OPCODE_TABLE_COUNT; i++) {
    if (EPA_OPCODE_TABLE[i].opcode == op) return &EPA_OPCODE_TABLE[i];
  }
  return NULL;
}

static inline const EpaOpcodeDef *epa_find_opcode_by_name(const char *name) {
  if (!name) return NULL;
  for (size_t i = 0; i < EPA_OPCODE_TABLE_COUNT; i++) {
    if (EPA_OPCODE_TABLE[i].name && strcmp(EPA_OPCODE_TABLE[i].name, name) == 0)
      return &EPA_OPCODE_TABLE[i];
  }
  return NULL;
}
