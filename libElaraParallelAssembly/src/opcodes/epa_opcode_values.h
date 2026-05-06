#pragma once
#include <stdint.h>

//
// Address space policy unchanged
//
#define EPA_OP_COMMON_MIN   ((uint16_t)0x0000u)
#define EPA_OP_COMMON_MAX   ((uint16_t)0x7FFFu)
#define EPA_OP_GL_MIN       ((uint16_t)0x8000u)
#define EPA_OP_GL_MAX       ((uint16_t)0xBFFFu)
#define EPA_OP_CUDA_MIN     ((uint16_t)0xC000u)
#define EPA_OP_CUDA_MAX     ((uint16_t)0xFFFFu)

#define EPA_OP_IS_COMMON(op) ((uint16_t)(op) <= EPA_OP_COMMON_MAX)
#define EPA_OP_IS_GL(op)     ((uint16_t)(op) >= EPA_OP_GL_MIN   && (uint16_t)(op) <= EPA_OP_GL_MAX)
#define EPA_OP_IS_CUDA(op)   ((uint16_t)(op) >= EPA_OP_CUDA_MIN)

// SET_MODE values
#define EPA_MODE_OPENGL   ((uint8_t)0u)
#define EPA_MODE_CUDA     ((uint8_t)1u)
#define EPA_IS_VALID_MODE(m) ((m) == EPA_MODE_OPENGL || (m) == EPA_MODE_CUDA)

//
// Opcode numeric values
//
#define X(name, value, mnemonic, plen) \
  enum { EPA_OP_##name = (uint16_t)(value) };

#include "epa_opcodes.x"

#undef X
