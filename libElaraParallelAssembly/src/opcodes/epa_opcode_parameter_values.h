#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* SET_MODE values */
#define EPA_MODE_OPENGL   ((uint8_t)0u)
#define EPA_MODE_CUDA     ((uint8_t)1u)
#define EPA_IS_VALID_MODE(m) ((m) == EPA_MODE_OPENGL || (m) == EPA_MODE_CUDA)


#define EPA_YIELD_SOFT            ((uint8_t)0u)
#define EPA_YIELD_HARD            ((uint8_t)1u)


/* ---------- GPU enums (portable) ---------- */

// primitive topology
#define EPA_PRIM_POINTS        ((uint32_t)0u)
#define EPA_PRIM_LINES         ((uint32_t)1u)
#define EPA_PRIM_TRIANGLES     ((uint32_t)2u)
#define EPA_PRIM_TRI_STRIP     ((uint32_t)3u)

// index type
#define EPA_INDEX_U16          ((uint32_t)0u)
#define EPA_INDEX_U32          ((uint32_t)1u)

// buffer usage
#define EPA_BU_STATIC          ((uint32_t)0u)
#define EPA_BU_DYNAMIC         ((uint32_t)1u)
#define EPA_BU_STREAM          ((uint32_t)2u)

// buffer bind targets
#define EPA_BT_ARRAY           ((uint32_t)0u)  // VBO
#define EPA_BT_ELEMENT         ((uint32_t)1u)  // EBO/IBO
#define EPA_BT_UNIFORM         ((uint32_t)2u)  // UBO
#define EPA_BT_STORAGE         ((uint32_t)3u)  // SSBO

// shader stages
#define EPA_SH_VERT            ((uint32_t)0u)
#define EPA_SH_FRAG            ((uint32_t)1u)
#define EPA_SH_GEOM            ((uint32_t)2u)
#define EPA_SH_TESC            ((uint32_t)3u)
#define EPA_SH_TESE            ((uint32_t)4u)
#define EPA_SH_COMP            ((uint32_t)5u)

// texture dims
#define EPA_TEX_2D             ((uint32_t)0u)
#define EPA_TEX_3D             ((uint32_t)1u)
#define EPA_TEX_CUBE           ((uint32_t)2u)
#define EPA_TEX_2D_ARRAY       ((uint32_t)3u)

// texture formats (expand as needed)
#define EPA_TF_RGBA8           ((uint32_t)0u)
#define EPA_TF_SRGBA8          ((uint32_t)1u)
#define EPA_TF_RGBA16F         ((uint32_t)2u)
#define EPA_TF_RGBA32F         ((uint32_t)3u)
#define EPA_TF_R8              ((uint32_t)4u)
#define EPA_TF_RG8             ((uint32_t)5u)
#define EPA_TF_R16F            ((uint32_t)6u)
#define EPA_TF_RG16F           ((uint32_t)7u)
#define EPA_TF_D24S8           ((uint32_t)8u)
#define EPA_TF_D32F            ((uint32_t)9u)

// pixel upload element type
#define EPA_PX_U8              ((uint32_t)0u)
#define EPA_PX_F16             ((uint32_t)1u)
#define EPA_PX_F32             ((uint32_t)2u)

// compare func
#define EPA_CF_NEVER           ((uint32_t)0u)
#define EPA_CF_LESS            ((uint32_t)1u)
#define EPA_CF_LEQUAL          ((uint32_t)2u)
#define EPA_CF_EQUAL           ((uint32_t)3u)
#define EPA_CF_GEQUAL          ((uint32_t)4u)
#define EPA_CF_GREATER         ((uint32_t)5u)
#define EPA_CF_NOTEQUAL        ((uint32_t)6u)
#define EPA_CF_ALWAYS          ((uint32_t)7u)

// blend factors
#define EPA_BF_ZERO            ((uint32_t)0u)
#define EPA_BF_ONE             ((uint32_t)1u)
#define EPA_BF_SRC_ALPHA       ((uint32_t)2u)
#define EPA_BF_ONE_MINUS_SRC_A ((uint32_t)3u)
#define EPA_BF_DST_ALPHA       ((uint32_t)4u)
#define EPA_BF_ONE_MINUS_DST_A ((uint32_t)5u)

// blend equation
#define EPA_BE_ADD             ((uint32_t)0u)
#define EPA_BE_SUB             ((uint32_t)1u)
#define EPA_BE_REV_SUB         ((uint32_t)2u)

// cull face / front face
#define EPA_CULL_NONE          ((uint32_t)0u)
#define EPA_CULL_BACK          ((uint32_t)1u)
#define EPA_CULL_FRONT         ((uint32_t)2u)
#define EPA_FF_CCW             ((uint32_t)0u)
#define EPA_FF_CW              ((uint32_t)1u)

// memory barrier bits
#define EPA_MB_VERTEX_ATTRIB   ((uint32_t)(1u<<0))
#define EPA_MB_ELEMENT_ARRAY   ((uint32_t)(1u<<1))
#define EPA_MB_UNIFORM         ((uint32_t)(1u<<2))
#define EPA_MB_TEXTURE_FETCH   ((uint32_t)(1u<<3))
#define EPA_MB_SHADER_IMAGE    ((uint32_t)(1u<<4))
#define EPA_MB_SHADER_STORAGE  ((uint32_t)(1u<<5))
#define EPA_MB_FRAMEBUFFER     ((uint32_t)(1u<<6))
#define EPA_MB_ALL             ((uint32_t)0xFFFFFFFFu)

// bool
#define EPA_FALSE              ((uint32_t)0u)
#define EPA_TRUE               ((uint32_t)1u)

// resource kinds (portable)
#define EPA_RK_BUFFER          ((uint32_t)0u)
#define EPA_RK_TEX             ((uint32_t)1u)
#define EPA_RK_SAMPLER         ((uint32_t)2u)
#define EPA_RK_SHADER          ((uint32_t)3u)
#define EPA_RK_PROGRAM         ((uint32_t)4u)
#define EPA_RK_PIPELINE        ((uint32_t)5u)
#define EPA_RK_VTX_LAYOUT      ((uint32_t)6u)
#define EPA_RK_RT              ((uint32_t)7u)
#define EPA_RK_QUERY           ((uint32_t)8u)
#define EPA_RK_FENCE           ((uint32_t)9u)

// shader blob kinds (portable container; backend chooses what it can consume)
#define EPA_SB_EPAIL           ((uint32_t)0u)  // your future IR (ideal)
#define EPA_SB_GLSL            ((uint32_t)1u)  // text blob (GL may compile; others may reject)
#define EPA_SB_SPIRV           ((uint32_t)2u)  // binary blob (optional)
#define EPA_SB_PTX             ((uint32_t)3u)  // CUDA
#define EPA_SB_CUBIN           ((uint32_t)4u)  // CUDA
