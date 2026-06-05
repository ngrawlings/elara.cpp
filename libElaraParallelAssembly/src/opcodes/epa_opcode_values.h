#pragma once
#include <stdint.h>

//
// Address space policy unchanged
//
#define EPA_OP_RESERVED_MIN ((uint8_t)111u)
#define EPA_OP_EXT_ESCAPE   ((uint8_t)255u)

#define EPA_OP_IS_COMMON(op) ((uint8_t)(op) < EPA_OP_RESERVED_MIN)
#define EPA_OP_IS_GL(op)     (0)
#define EPA_OP_IS_CUDA(op)   (0)

// SET_MODE values
#define EPA_MODE_OPENGL   ((uint8_t)0u)
#define EPA_MODE_CUDA     ((uint8_t)1u)
#define EPA_IS_VALID_MODE(m) ((m) == EPA_MODE_OPENGL || (m) == EPA_MODE_CUDA)

//
// Opcode numeric values
//
#define X(name, value, mnemonic, plen) \
  enum { EPA_OP_##name = (uint8_t)(value) };

#include "epa_opcodes.x"

#undef X
