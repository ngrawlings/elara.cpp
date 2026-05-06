// epa_backend_opengl_nonflow.c
#include "epa_backend_nonflow.h"

#include "opcodes/epa_opcode_values.h"
#include "opcodes/opcode_def.h"
#include "log.h"

#include <string.h>
#include <stdio.h>

#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include "memory/epa_ring_buffer.h"
#include "vm/epa_vm.h"
#include "vm/epa_worker_state.h"
#include "gui/viewport.h"


#define EPA_MAX_WORKERS 256

#define EPA_MAX_RES 4096  // simple fixed table; ids must be < EPA_MAX_RES

typedef struct {
  uint32_t kind;   // EPA_RK_*
  uint32_t id;
  uint32_t in_use;
} ResSlot;

typedef struct {
  // Keep the same impl shape you already have
  IdRing syncq;
  EpaWorkerState workers[EPA_MAX_WORKERS];

  // ---- GPU resource tables ----
  GLuint buffers[EPA_MAX_RES];        // EPA_RK_BUFFER
  GLuint textures[EPA_MAX_RES];       // EPA_RK_TEX
  GLuint samplers[EPA_MAX_RES];       // EPA_RK_SAMPLER

  GLuint vtx_layouts[EPA_MAX_RES];    // VAO (vertex layout)
  GLuint rts[EPA_MAX_RES];            // FBO (render targets)

  GLuint shaders[EPA_MAX_RES];        // shader objects
  GLuint programs[EPA_MAX_RES];       // linked programs

  GLsync fences[EPA_MAX_RES];         // sync objects
  GLuint queries[EPA_MAX_RES];        // query objects

  GLenum tex_targets[EPA_MAX_RES];    // track target per texture id

  // ---- cached bindings (optional, just for sanity/debug) ----
  GLuint bound_array_buf;
  GLuint bound_elem_buf;
  GLuint bound_vao;
  GLuint bound_fbo;
} OpenglImpl;

// Image access (for GPU_BIND_IMAGE_TEX)
#define EPA_IMG_READ   ((uint32_t)0u)
#define EPA_IMG_WRITE  ((uint32_t)1u)
#define EPA_IMG_RW     ((uint32_t)2u)


static inline int res_id_ok(uint32_t id) { return id < (uint32_t)EPA_MAX_RES; }

// ---- Helpers ----
static inline int32_t read_i32_le(const uint8_t *b, size_t off) {
  return (int32_t)EPA_READ_U32_LE(b, off);
}

static inline float read_f32_le(const uint8_t *b, size_t off) {
  float f;
  uint32_t u = EPA_READ_U32_LE(b, off);
  memcpy(&f, &u, 4);
  return f;
}

static EpaWorkerState *get_worker(OpenglImpl *impl, uint32_t wid) {
  if (!impl) return NULL;
  if (wid >= EPA_MAX_WORKERS) return NULL;
  if (!impl->workers[wid].inited) return NULL;
  return &impl->workers[wid];
}

// This backend uses *only* the data stack (legacy).
// If you adopt the split stack (data_sp/call_sp), replace vm->sp with vm->data_sp here.
#define VM_DATA_PUSH(v) do { \
  if (sp >= 256) { snprintf(err,256,"VM data stack overflow"); return EPA_NF_EXEC_ERR; } \
  stack[sp++] = (v); \
} while (0)

#define VM_DATA_POP(outv) do { \
  if (sp <= 0) { snprintf(err,256,"VM data stack underflow"); return EPA_NF_EXEC_ERR; } \
  (outv) = stack[--sp]; \
} while (0)

// Detect flow opcodes (must be handled by FlowLogic, not here)
static int is_flow_opcode(uint16_t op) {
  switch (op) {
    case EPA_OP_END:
    case EPA_OP_YIELD:
    case EPA_OP_JMP_REL32:
    case EPA_OP_JZ_REL32:
    case EPA_OP_JNZ_REL32:
    case EPA_OP_SYNC:
    case EPA_OP_WAIT_ON_SYNC:
    case EPA_OP_ENTRY_EXEC:
    case EPA_OP_ENTRY_HALT:
    case EPA_OP_ENTRY_START:
    case EPA_OP_ENTRY_END:
      return 1;
    default:
      return 0;
  }

  // If you add CALL/RET as opcodes, add them here too:
  // case EPA_OP_CALL:
  // case EPA_OP_RET:
}

// ---- GL proc loader (only for funcs that may be missing in headers) ----
static int g_gl_procs_inited = 0;

static PFNGLGENBUFFERSPROC              p_glGenBuffers = NULL;
static PFNGLDELETEBUFFERSPROC           p_glDeleteBuffers = NULL;
static PFNGLBINDBUFFERPROC              p_glBindBuffer = NULL;
static PFNGLBUFFERDATAPROC              p_glBufferData = NULL;
static PFNGLBUFFERSUBDATAPROC           p_glBufferSubData = NULL;
static PFNGLBINDBUFFERBASEPROC          p_glBindBufferBase = NULL;

static PFNGLACTIVETEXTUREPROC           p_glActiveTexture = NULL;
static PFNGLTEXSTORAGE2DPROC            p_glTexStorage2D = NULL;
static PFNGLTEXSTORAGE3DPROC            p_glTexStorage3D = NULL;
static PFNGLTEXSUBIMAGE3DPROC           p_glTexSubImage3D = NULL;

static PFNGLGENSAMPLERSPROC             p_glGenSamplers = NULL;
static PFNGLDELETESAMPLERSPROC          p_glDeleteSamplers = NULL;
static PFNGLSAMPLERPARAMETERIPROC       p_glSamplerParameteri = NULL;
static PFNGLBINDSAMPLERPROC             p_glBindSampler = NULL;

static PFNGLGENVERTEXARRAYSPROC           p_glGenVertexArrays = NULL;
static PFNGLDELETEVERTEXARRAYSPROC        p_glDeleteVertexArrays = NULL;
static PFNGLBINDVERTEXARRAYPROC           p_glBindVertexArray = NULL;
static PFNGLVERTEXATTRIBPOINTERPROC       p_glVertexAttribPointer = NULL;
static PFNGLENABLEVERTEXATTRIBARRAYPROC   p_glEnableVertexAttribArray = NULL;
static PFNGLDISABLEVERTEXATTRIBARRAYPROC  p_glDisableVertexAttribArray = NULL;

static PFNGLBLENDEQUATIONSEPARATEPROC     p_glBlendEquationSeparate = NULL;

static PFNGLMEMORYBARRIERPROC             p_glMemoryBarrier = NULL;

static PFNGLDRAWELEMENTSBASEVERTEXPROC            p_glDrawElementsBaseVertex = NULL;
static PFNGLDRAWARRAYSINSTANCEDPROC               p_glDrawArraysInstanced = NULL;
static PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXPROC   p_glDrawElementsInstancedBaseVertex = NULL;

static PFNGLCREATESHADERPROC              p_glCreateShader = NULL;
static PFNGLSHADERSOURCEPROC              p_glShaderSource = NULL;
static PFNGLCOMPILESHADERPROC             p_glCompileShader = NULL;
static PFNGLGETSHADERIVPROC               p_glGetShaderiv = NULL;
static PFNGLGETSHADERINFOLOGPROC          p_glGetShaderInfoLog = NULL;
static PFNGLDELETESHADERPROC              p_glDeleteShader = NULL; // avoid name clash with existing glDeleteShader usage

static PFNGLCREATEPROGRAMPROC             p_glCreateProgram = NULL;
static PFNGLATTACHSHADERPROC              p_glAttachShader = NULL;
static PFNGLLINKPROGRAMPROC               p_glLinkProgram = NULL;
static PFNGLGETPROGRAMIVPROC              p_glGetProgramiv = NULL;
static PFNGLGETPROGRAMINFOLOGPROC         p_glGetProgramInfoLog = NULL;
static PFNGLGETUNIFORMLOCATIONPROC        p_glGetUniformLocation = NULL;

static PFNGLUSEPROGRAMPROC                p_glUseProgram = NULL;

static PFNGLGENFRAMEBUFFERSPROC           p_glGenFramebuffers = NULL;
static PFNGLDELETEFRAMEBUFFERSPROC        p_glDeleteFramebuffers = NULL;
static PFNGLBINDFRAMEBUFFERPROC           p_glBindFramebuffer = NULL;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC    p_glCheckFramebufferStatus = NULL;
static PFNGLFRAMEBUFFERTEXTURE2DPROC      p_glFramebufferTexture2D = NULL;
static PFNGLFRAMEBUFFERTEXTURELAYERPROC   p_glFramebufferTextureLayer = NULL;
static PFNGLBLITFRAMEBUFFERPROC           p_glBlitFramebuffer = NULL;

static PFNGLENDQUERYPROC                 p_glEndQuery = NULL;
static PFNGLGETQUERYOBJECTUI64VPROC       p_glGetQueryObjectui64v = NULL;
static PFNGLOBJECTLABELPROC              p_glObjectLabel = NULL;
static PFNGLGENERATEMIPMAPPROC            p_glGenerateMipmap = NULL;
static PFNGLBINDIMAGETEXTUREPROC          p_glBindImageTexture = NULL;

static PFNGLUNIFORM1IPROC                 p_glUniform1i = NULL;
static PFNGLUNIFORM1FPROC                 p_glUniform1f = NULL;
static PFNGLUNIFORM4FPROC                 p_glUniform4f = NULL;
static PFNGLUNIFORMMATRIX4FVPROC          p_glUniformMatrix4fv = NULL;
static PFNGLBEGINQUERYPROC            p_glBeginQuery = NULL;
static PFNGLGENQUERIESPROC            p_glGenQueries = NULL;
static PFNGLDELETEQUERIESPROC         p_glDeleteQueries = NULL;

static PFNGLFENCESYNCPROC             p_glFenceSync = NULL;
static PFNGLCLIENTWAITSYNCPROC        p_glClientWaitSync = NULL;
static PFNGLDELETESYNCPROC            p_glDeleteSync = NULL;

static PFNGLDISPATCHCOMPUTEPROC       p_glDispatchCompute = NULL;
static PFNGLDETACHSHADERPROC          p_glDetachShader = NULL;
static PFNGLDELETEPROGRAMPROC         p_glDeleteProgram = NULL;

static void gl_init_procs(void) {
   if (g_gl_procs_inited) return;
   g_gl_procs_inited = 1;

  // Buffers
  p_glGenBuffers = (PFNGLGENBUFFERSPROC)glfwGetProcAddress("glGenBuffers");
  p_glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)glfwGetProcAddress("glDeleteBuffers");
  p_glBindBuffer = (PFNGLBINDBUFFERPROC)glfwGetProcAddress("glBindBuffer");
  p_glBufferData = (PFNGLBUFFERDATAPROC)glfwGetProcAddress("glBufferData");
  p_glBufferSubData = (PFNGLBUFFERSUBDATAPROC)glfwGetProcAddress("glBufferSubData");
  p_glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)glfwGetProcAddress("glBindBufferBase");

  // Textures
  p_glActiveTexture = (PFNGLACTIVETEXTUREPROC)glfwGetProcAddress("glActiveTexture");
  p_glTexStorage2D = (PFNGLTEXSTORAGE2DPROC)glfwGetProcAddress("glTexStorage2D");
  p_glTexStorage3D = (PFNGLTEXSTORAGE3DPROC)glfwGetProcAddress("glTexStorage3D");
  p_glTexSubImage3D = (PFNGLTEXSUBIMAGE3DPROC)glfwGetProcAddress("glTexSubImage3D");

  // Samplers
  p_glGenSamplers = (PFNGLGENSAMPLERSPROC)glfwGetProcAddress("glGenSamplers");
  p_glDeleteSamplers = (PFNGLDELETESAMPLERSPROC)glfwGetProcAddress("glDeleteSamplers");
  p_glSamplerParameteri = (PFNGLSAMPLERPARAMETERIPROC)glfwGetProcAddress("glSamplerParameteri");
  p_glBindSampler = (PFNGLBINDSAMPLERPROC)glfwGetProcAddress("glBindSampler");

  // VAO / attribs
  p_glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)glfwGetProcAddress("glGenVertexArrays");
  p_glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)glfwGetProcAddress("glDeleteVertexArrays");
  p_glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)glfwGetProcAddress("glBindVertexArray");
  p_glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)glfwGetProcAddress("glVertexAttribPointer");
  p_glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)glfwGetProcAddress("glEnableVertexAttribArray");
  p_glDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)glfwGetProcAddress("glDisableVertexAttribArray");

  // FBO
  p_glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)glfwGetProcAddress("glGenFramebuffers");
  p_glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)glfwGetProcAddress("glDeleteFramebuffers");
  p_glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)glfwGetProcAddress("glBindFramebuffer");
  p_glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)glfwGetProcAddress("glCheckFramebufferStatus");
  p_glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)glfwGetProcAddress("glFramebufferTexture2D");
  p_glFramebufferTextureLayer = (PFNGLFRAMEBUFFERTEXTURELAYERPROC)glfwGetProcAddress("glFramebufferTextureLayer");
  p_glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)glfwGetProcAddress("glBlitFramebuffer");

  // Barriers + draws
  p_glMemoryBarrier = (PFNGLMEMORYBARRIERPROC)glfwGetProcAddress("glMemoryBarrier");
  p_glDrawElementsBaseVertex = (PFNGLDRAWELEMENTSBASEVERTEXPROC)glfwGetProcAddress("glDrawElementsBaseVertex");
  p_glDrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)glfwGetProcAddress("glDrawArraysInstanced");
  p_glDrawElementsInstancedBaseVertex = (PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXPROC)glfwGetProcAddress("glDrawElementsInstancedBaseVertex");

  // Shaders/programs
  p_glCreateShader = (PFNGLCREATESHADERPROC)glfwGetProcAddress("glCreateShader");
  p_glShaderSource = (PFNGLSHADERSOURCEPROC)glfwGetProcAddress("glShaderSource");
  p_glCompileShader = (PFNGLCOMPILESHADERPROC)glfwGetProcAddress("glCompileShader");
  p_glGetShaderiv = (PFNGLGETSHADERIVPROC)glfwGetProcAddress("glGetShaderiv");
  p_glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)glfwGetProcAddress("glGetShaderInfoLog");
  p_glDeleteShader = (PFNGLDELETESHADERPROC)glfwGetProcAddress("glDeleteShader");

  p_glCreateProgram = (PFNGLCREATEPROGRAMPROC)glfwGetProcAddress("glCreateProgram");
  p_glAttachShader = (PFNGLATTACHSHADERPROC)glfwGetProcAddress("glAttachShader");
  p_glLinkProgram = (PFNGLLINKPROGRAMPROC)glfwGetProcAddress("glLinkProgram");
  p_glGetProgramiv = (PFNGLGETPROGRAMIVPROC)glfwGetProcAddress("glGetProgramiv");
  p_glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)glfwGetProcAddress("glGetProgramInfoLog");
  p_glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)glfwGetProcAddress("glGetUniformLocation");
  p_glUseProgram = (PFNGLUSEPROGRAMPROC)glfwGetProcAddress("glUseProgram");

  p_glEndQuery = (PFNGLENDQUERYPROC)glfwGetProcAddress("glEndQuery");
  p_glGetQueryObjectui64v = (PFNGLGETQUERYOBJECTUI64VPROC)glfwGetProcAddress("glGetQueryObjectui64v");
  p_glObjectLabel = (PFNGLOBJECTLABELPROC)glfwGetProcAddress("glObjectLabel");
  p_glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)glfwGetProcAddress("glGenerateMipmap");
  p_glBindImageTexture = (PFNGLBINDIMAGETEXTUREPROC)glfwGetProcAddress("glBindImageTexture");

  p_glFenceSync = (PFNGLFENCESYNCPROC)glfwGetProcAddress("glFenceSync");
  p_glClientWaitSync = (PFNGLCLIENTWAITSYNCPROC)glfwGetProcAddress("glClientWaitSync");
  p_glDeleteSync = (PFNGLDELETESYNCPROC)glfwGetProcAddress("glDeleteSync");
   
  p_glDispatchCompute = (PFNGLDISPATCHCOMPUTEPROC)glfwGetProcAddress("glDispatchCompute");
  p_glDetachShader = (PFNGLDETACHSHADERPROC)glfwGetProcAddress("glDetachShader");
  p_glDeleteProgram = (PFNGLDELETEPROGRAMPROC)glfwGetProcAddress("glDeleteProgram");
 }


static GLenum map_buf_target(uint32_t t) {
  switch (t) {
    case EPA_BT_ARRAY:   return GL_ARRAY_BUFFER;
    case EPA_BT_ELEMENT: return GL_ELEMENT_ARRAY_BUFFER;
    case EPA_BT_UNIFORM: return GL_UNIFORM_BUFFER;
    case EPA_BT_STORAGE: return GL_SHADER_STORAGE_BUFFER;
    default:             return 0;
  }
}

static GLenum map_buf_usage(uint32_t u) {
  switch (u) {
    case EPA_BU_STATIC:  return GL_STATIC_DRAW;
    case EPA_BU_DYNAMIC: return GL_DYNAMIC_DRAW;
    case EPA_BU_STREAM:  return GL_STREAM_DRAW;
    default:             return GL_STATIC_DRAW;
  }
}

static GLenum map_tex_dim(uint32_t d) {
  switch (d) {
    case EPA_TEX_2D:       return GL_TEXTURE_2D;
    case EPA_TEX_3D:       return GL_TEXTURE_3D;
    case EPA_TEX_CUBE:     return GL_TEXTURE_CUBE_MAP;
    case EPA_TEX_2D_ARRAY: return GL_TEXTURE_2D_ARRAY;
    default:               return 0;
  }
}

static int map_tex_format(uint32_t fmt, GLint *out_internal, GLenum *out_format, GLenum *out_type) {
  // For now: pick a reasonable upload format/type. Expand as needed.
  switch (fmt) {
    case EPA_TF_RGBA8:
      *out_internal = GL_RGBA8; *out_format = GL_RGBA; *out_type = GL_UNSIGNED_BYTE; return 1;
    case EPA_TF_SRGBA8:
      *out_internal = GL_SRGB8_ALPHA8; *out_format = GL_RGBA; *out_type = GL_UNSIGNED_BYTE; return 1;
    case EPA_TF_RGBA16F:
      *out_internal = GL_RGBA16F; *out_format = GL_RGBA; *out_type = GL_HALF_FLOAT; return 1;
    case EPA_TF_RGBA32F:
      *out_internal = GL_RGBA32F; *out_format = GL_RGBA; *out_type = GL_FLOAT; return 1;
    case EPA_TF_R8:
      *out_internal = GL_R8; *out_format = GL_RED; *out_type = GL_UNSIGNED_BYTE; return 1;
    case EPA_TF_RG8:
      *out_internal = GL_RG8; *out_format = GL_RG; *out_type = GL_UNSIGNED_BYTE; return 1;
    case EPA_TF_R16F:
      *out_internal = GL_R16F; *out_format = GL_RED; *out_type = GL_HALF_FLOAT; return 1;
    case EPA_TF_RG16F:
      *out_internal = GL_RG16F; *out_format = GL_RG; *out_type = GL_HALF_FLOAT; return 1;
    case EPA_TF_D24S8:
      *out_internal = GL_DEPTH24_STENCIL8; *out_format = GL_DEPTH_STENCIL; *out_type = GL_UNSIGNED_INT_24_8; return 1;
    case EPA_TF_D32F:
      *out_internal = GL_DEPTH_COMPONENT32F; *out_format = GL_DEPTH_COMPONENT; *out_type = GL_FLOAT; return 1;
    default:
      return 0;
  }
}

static GLenum map_px_type(uint32_t px) {
  switch (px) {
    case EPA_PX_U8:  return GL_UNSIGNED_BYTE;
    case EPA_PX_F16: return GL_HALF_FLOAT;
    case EPA_PX_F32: return GL_FLOAT;
    default:         return GL_UNSIGNED_BYTE;
  }
}

static GLenum map_compare_func(uint32_t f) {
  switch (f) {
    case EPA_CF_NEVER:    return GL_NEVER;
    case EPA_CF_LESS:     return GL_LESS;
    case EPA_CF_LEQUAL:   return GL_LEQUAL;
    case EPA_CF_EQUAL:    return GL_EQUAL;
    case EPA_CF_GEQUAL:   return GL_GEQUAL;
    case EPA_CF_GREATER:  return GL_GREATER;
    case EPA_CF_NOTEQUAL: return GL_NOTEQUAL;
    case EPA_CF_ALWAYS:   return GL_ALWAYS;
    default:              return 0;
  }
}

static GLenum map_blend_factor(uint32_t f) {
  switch (f) {
    case EPA_BF_ZERO:            return GL_ZERO;
    case EPA_BF_ONE:             return GL_ONE;
    case EPA_BF_SRC_ALPHA:       return GL_SRC_ALPHA;
    case EPA_BF_ONE_MINUS_SRC_A: return GL_ONE_MINUS_SRC_ALPHA;
    case EPA_BF_DST_ALPHA:       return GL_DST_ALPHA;
    case EPA_BF_ONE_MINUS_DST_A: return GL_ONE_MINUS_DST_ALPHA;
    default:                     return 0;
  }
}

static GLenum map_blend_eq(uint32_t e) {
  switch (e) {
    case EPA_BE_ADD:     return GL_FUNC_ADD;
    case EPA_BE_SUB:     return GL_FUNC_SUBTRACT;
    case EPA_BE_REV_SUB: return GL_FUNC_REVERSE_SUBTRACT;
    default:             return 0;
  }
}

static GLenum map_cull_face(uint32_t c) {
  switch (c) {
    case EPA_CULL_BACK:  return GL_BACK;
    case EPA_CULL_FRONT: return GL_FRONT;
    default:             return 0;
  }
}

static GLenum map_front_face(uint32_t ff) {
  switch (ff) {
    case EPA_FF_CCW: return GL_CCW;
    case EPA_FF_CW:  return GL_CW;
    default:         return 0;
  }
}

static int map_attrib_type(uint32_t elem_type, GLenum *out_type, GLint *out_size_bytes) {
  // elem_type uses EPA_PX_* in your header (portable).
  switch (elem_type) {
    case EPA_PX_U8:  *out_type = GL_UNSIGNED_BYTE; *out_size_bytes = 1; return 1;
    case EPA_PX_F16: *out_type = GL_HALF_FLOAT;    *out_size_bytes = 2; return 1;
    case EPA_PX_F32: *out_type = GL_FLOAT;         *out_size_bytes = 4; return 1;
    default: return 0;
  }
}

static GLenum map_prim(uint32_t prim) {
  switch (prim) {
    case EPA_PRIM_POINTS:     return GL_POINTS;
    case EPA_PRIM_LINES:      return GL_LINES;
    case EPA_PRIM_TRIANGLES:  return GL_TRIANGLES;
    case EPA_PRIM_TRI_STRIP:  return GL_TRIANGLE_STRIP;
    default:                  return 0;
  }
}

static GLenum map_index_type(uint32_t t) {
  switch (t) {
    case EPA_INDEX_U16: return GL_UNSIGNED_SHORT;
    case EPA_INDEX_U32: return GL_UNSIGNED_INT;
    default:            return 0;
  }
}

// RT attachment mapping:
// attachment: 0..7 => COLOR0..COLOR7, 100 => DEPTH, 101 => STENCIL, 102 => DEPTH_STENCIL
// (You can later formalize these enums in opcode_def.h; this keeps you moving now.)
#define EPA_ATTACH_COLOR0        ((uint32_t)0u)
#define EPA_ATTACH_DEPTH         ((uint32_t)100u)
#define EPA_ATTACH_STENCIL       ((uint32_t)101u)
#define EPA_ATTACH_DEPTH_STENCIL ((uint32_t)102u)

static GLenum map_rt_attachment(uint32_t a) {
  if (a <= 7u) return (GLenum)(GL_COLOR_ATTACHMENT0 + (GLenum)a);
  switch (a) {
    case EPA_ATTACH_DEPTH:         return GL_DEPTH_ATTACHMENT;
    case EPA_ATTACH_STENCIL:       return GL_STENCIL_ATTACHMENT;
    case EPA_ATTACH_DEPTH_STENCIL: return GL_DEPTH_STENCIL_ATTACHMENT;
    default:                       return 0;
  }
}

// RT_BLIT mask_bits: bit0=color, bit1=depth, bit2=stencil
#define EPA_BLIT_COLOR   ((uint32_t)(1u<<0))
#define EPA_BLIT_DEPTH   ((uint32_t)(1u<<1))
#define EPA_BLIT_STENCIL ((uint32_t)(1u<<2))

static GLbitfield map_blit_mask(uint32_t m) {
  GLbitfield out = 0;
  if (m & EPA_BLIT_COLOR)   out |= GL_COLOR_BUFFER_BIT;
  if (m & EPA_BLIT_DEPTH)   out |= GL_DEPTH_BUFFER_BIT;
  if (m & EPA_BLIT_STENCIL) out |= GL_STENCIL_BUFFER_BIT;
  return out;
}

// filter: 0=nearest, 1=linear
static GLenum map_blit_filter(uint32_t f) {
  switch (f) {
    case 0u: return GL_NEAREST;
    case 1u: return GL_LINEAR;
    default: return GL_NEAREST;
  }
}

static GLbitfield map_barrier_bits(uint32_t b) {
  // Map your EPA_MB_* bits to GL memory barrier bits.
  // Some are approximate; refine as needed per your engine semantics.
  GLbitfield out = 0;
  if (b & EPA_MB_VERTEX_ATTRIB)  out |= GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;
  if (b & EPA_MB_ELEMENT_ARRAY)  out |= GL_ELEMENT_ARRAY_BARRIER_BIT;
  if (b & EPA_MB_UNIFORM)        out |= GL_UNIFORM_BARRIER_BIT;
  if (b & EPA_MB_TEXTURE_FETCH)  out |= GL_TEXTURE_FETCH_BARRIER_BIT;
  if (b & EPA_MB_SHADER_IMAGE)   out |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
  if (b & EPA_MB_SHADER_STORAGE) out |= GL_SHADER_STORAGE_BARRIER_BIT;
  if (b & EPA_MB_FRAMEBUFFER)    out |= GL_FRAMEBUFFER_BARRIER_BIT;
  if (b == EPA_MB_ALL)           out  = GL_ALL_BARRIER_BITS;
  return out;
}

static GLenum map_shader_stage(uint32_t st) {
  switch (st) {
    case EPA_SH_VERT: return GL_VERTEX_SHADER;
    case EPA_SH_FRAG: return GL_FRAGMENT_SHADER;
    case EPA_SH_COMP: return GL_COMPUTE_SHADER;
    // (optional later) GEOM/TESS stages
    default: return 0;
  }
}

static int gl_compile_shader_from_glsl(
    GLenum gl_stage,
    const char *src, size_t src_len,
    GLuint *out_shader,
    char err[EPA_MAX_ERR]
) {
  GLuint sh = p_glCreateShader(gl_stage);
  if (!sh) { snprintf(err, EPA_MAX_ERR, "glCreateShader failed"); return 0; }

  const GLchar *strings[1] = { (const GLchar*)src };
  GLint lens[1] = { (GLint)src_len };
  p_glShaderSource(sh, 1, strings, lens);
  p_glCompileShader(sh);

  GLint ok = 0;
  p_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char logbuf[2048];
    GLsizei n = 0;
    p_glGetShaderInfoLog(sh, (GLsizei)sizeof(logbuf), &n, logbuf);
    snprintf(err, EPA_MAX_ERR, "shader compile failed: %.*s", (int)n, logbuf);
    p_glDeleteShader(sh);
    return 0;
  }

  *out_shader = sh;
  return 1;
}

static GLint gl_get_uniform_loc(GLuint program, const char *name, size_t n) {
  // name bytes are not guaranteed NUL-terminated
  char tmp[256];
  if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
  memcpy(tmp, name, n);
  tmp[n] = 0;
  return p_glGetUniformLocation(program, tmp);
}

static GLenum map_image_access(uint32_t a) {
  switch (a) {
    case EPA_IMG_READ:  return GL_READ_ONLY;
    case EPA_IMG_WRITE: return GL_WRITE_ONLY;
    case EPA_IMG_RW:    return GL_READ_WRITE;
    default:            return 0;
  }
}

static GLenum map_image_format(uint32_t fmt) {
  switch (fmt) {
    case EPA_TF_RGBA8:    return GL_RGBA8;
    case EPA_TF_RGBA16F:  return GL_RGBA16F;
    case EPA_TF_RGBA32F:  return GL_RGBA32F;

    // Enable only if you actually define EPA_TF_R32F in opcode_def.h
    // case EPA_TF_R32F:   return GL_R32F;

    default:              return 0;
  }
}

// Compute per-instruction total bytes (need). Supports fixed and variable-length ops.
static int compute_need(
    uint16_t op, const EpaOpcodeDef *def,
    const uint8_t *code, size_t pc, size_t code_len,
    size_t *out_need,
    char err[EPA_MAX_ERR]
) {
  if (!def || !out_need) return 0;
  if (def->param_len != 0xFFu) {
    *out_need = 2u + (size_t)def->param_len;
    return 1;
  }

  // variable-length: depends on opcode
  if (op == EPA_OP_GPU_BUF_SUBDATA) {
    // header(16): id,target,offset,byte_len  => byte_len at +2+12
    if (pc + 2 + 16 > code_len) { snprintf(err, EPA_MAX_ERR, "trunc GPU_BUF_SUBDATA header"); return 0; }
    uint32_t byte_len = EPA_READ_U32_LE(code, pc + 2 + 12);
    *out_need = 2u + 16u + (size_t)byte_len;
    return 1;
  }

  if (op == EPA_OP_GPU_TEX_SUBIMAGE) {
    // header(44): byte_len at +2+40
    if (pc + 2 + 44 > code_len) { snprintf(err, EPA_MAX_ERR, "trunc GPU_TEX_SUBIMAGE header"); return 0; }
    uint32_t byte_len = EPA_READ_U32_LE(code, pc + 2 + 40);
    *out_need = 2u + 44u + (size_t)byte_len;
    return 1;
  }

  if (op == EPA_OP_GPU_SHADER_LOAD_BLOB) {
    // header(16): byte_len at +2+12
    if (pc + 2 + 16 > code_len) { snprintf(err, EPA_MAX_ERR, "trunc GPU_SHADER_LOAD_BLOB header"); return 0; }
    uint32_t byte_len = EPA_READ_U32_LE(code, pc + 2 + 12);
    *out_need = 2u + 16u + (size_t)byte_len;
    return 1;
  }

  if (op == EPA_OP_GPU_DEBUG_LABEL) {
    // header(12): u32 kind, u32 id, u32 byte_len + bytes
    if (pc + 2 + 12 > code_len) { snprintf(err, EPA_MAX_ERR, "trunc GPU_DEBUG_LABEL header"); return 0; }
    uint32_t byte_len = EPA_READ_U32_LE(code, pc + 2 + 8);
    *out_need = 2u + 12u + (size_t)byte_len;
    return 1;
  }

  // In later batches:
  // GPU_TEX_SUBIMAGE: header(40) byte_len at +2+36
  // GPU_SHADER_LOAD_BLOB: header(16) byte_len at +2+12
  // GPU_DEBUG_LABEL: header(12) byte_len at +2+8

  snprintf(err, EPA_MAX_ERR, "variable opcode %s not supported in compute_need yet", def->name);
  return 0;
}

static EpaNonFlowRc opengl_exec_one(
    void *impl_v,
    Viewport *vp,
    const EpaProgramDesc *prog,
    EpaWorkerState *w,
    EpaEip *eip,
    char err[EPA_MAX_ERR]
) {
  if (err) err[0] = 0;
  if (!impl_v || !vp || !prog || !w || !eip) {
    snprintf(err, EPA_MAX_ERR, "opengl_nonflow: NULL arg");
    return EPA_NF_EXEC_ERR;
  }

  gl_init_procs();

  OpenglImpl *impl = (OpenglImpl*)impl_v;

  // Resolve current code view by EIP (entry/func)
  const uint8_t *code = NULL;
  size_t code_len = 0;
  if (!epa_prog_resolve_view(prog, eip->block_type, eip->block_id, &code, &code_len)) {
    snprintf(err, EPA_MAX_ERR, "opengl_nonflow: EIP resolve failed type=%u id=%u",
             (unsigned)eip->block_type, (unsigned)eip->block_id);
    return EPA_NF_EXEC_ERR;
  }

  size_t pc = (size_t)eip->rel_pc;
  if (pc + 2 > code_len) {
    snprintf(err, EPA_MAX_ERR, "opengl_nonflow: pc out of range pc=%zu len=%zu", pc, code_len);
    return EPA_NF_EXEC_ERR;
  }

  uint16_t op = EPA_READ_U16_LE(code, pc);
  const EpaOpcodeDef *def = epa_find_opcode(op);
  if (!def) {
    snprintf(err, EPA_MAX_ERR, "opengl_nonflow: unknown opcode 0x%04x at pc=%zu", op, pc);
    return EPA_NF_EXEC_ERR;
  }

  // If flow owns it, bounce
  if (is_flow_opcode(op)) {
    return EPA_NF_EXEC_NOT_MINE;
  }

  size_t need = 0;
  if (!compute_need(op, def, code, pc, code_len, &need, err)) {
    return EPA_NF_EXEC_ERR;
  }
  if (pc + need > code_len) {
    snprintf(err, EPA_MAX_ERR, "opengl_nonflow: truncated %s at pc=%zu need=%zu len=%zu",
             def->name, pc, need, code_len);
    return EPA_NF_EXEC_ERR;
  }

  // Thread-local VM state (legacy)
  EpaVM *vm = &w->vm;
  uint32_t *csc = w->vm.csc;            // Common State Control (r0..r3)
  int32_t *stack = (int32_t*)vm->stack.words; // silence signedness warning; stack is treated as i32
  int sp = vm->stack.sp;              // swap to vm->data_sp when you split stacks
  int32_t *locals = vm->locals;

  size_t p = pc + 2;

  TRACE("[GL-NF] slot=%u %s[%u] pc=%zu op=0x%04x %s\n",
        (unsigned)w->id,
        (eip->block_type==EPA_BLOCK_ENTRY)?"entry":"func",
        (unsigned)eip->block_id,
        pc, op, def->name);

  switch (op) {
    case EPA_OP_NOOP:
      break;

    case EPA_OP_SET_MODE: {
      // ignored: already running OpenGL backend
      break;
    }

    case EPA_OP_VIEWPORT_I32: {
      int32_t x = (int32_t)EPA_READ_U32_LE(code, p + 0);
      int32_t y = (int32_t)EPA_READ_U32_LE(code, p + 4);
      int32_t ww = (int32_t)EPA_READ_U32_LE(code, p + 8);
      int32_t hh = (int32_t)EPA_READ_U32_LE(code, p + 12);
      glViewport(x, y, ww, hh);
      break;
    }

    case EPA_OP_CLEAR_RGBA_DEPTH_F32: {
      float r = read_f32_le(code, p + 0);
      float g = read_f32_le(code, p + 4);
      float b = read_f32_le(code, p + 8);
      float a = read_f32_le(code, p + 12);
      float d = read_f32_le(code, p + 16);
      glClearColor(r, g, b, a);
      glClearDepth((double)d);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      break;
    }

    case EPA_OP_DRAW: {
      uint32_t prim  = EPA_READ_U32_LE(code, p + 0);
      uint32_t first = EPA_READ_U32_LE(code, p + 4);
      uint32_t count = EPA_READ_U32_LE(code, p + 8);
      (void)first;

      if (prim == EPA_PRIM_TRIANGLES && count >= 3) {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glBegin(GL_TRIANGLES);
          glColor3f(1.f, 0.f, 0.f); glVertex2f( 0.0f,  0.7f);
          glColor3f(0.f, 1.f, 0.f); glVertex2f(-0.7f, -0.6f);
          glColor3f(0.f, 0.f, 1.f); glVertex2f( 0.7f, -0.6f);
        glEnd();
      } else if (prim == EPA_PRIM_LINES && count >= 2) {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glBegin(GL_LINES);
          glColor3f(1.f, 1.f, 0.f); glVertex2f(-0.9f, 0.0f);
          glColor3f(0.f, 1.f, 1.f); glVertex2f( 0.9f, 0.0f);
        glEnd();
      } else {
        snprintf(err, EPA_MAX_ERR, "OpenGL: DRAW unsupported prim=%u count=%u", prim, count);
        return EPA_NF_EXEC_ERR;
      }
      break;
    }

    // ---- arithmetic / locals (legacy VM) ----
    case EPA_OP_PUSH_I32: {
      int32_t v = read_i32_le(code, p);
      VM_DATA_PUSH(v);
      break;
    }

    case EPA_OP_PUSH_R: {
      uint8_t ridx = code[p];
      if (ridx >= 4u) {
        snprintf(err, EPA_MAX_ERR, "PUSH_R bad reg %u", (unsigned)ridx);
        return EPA_NF_EXEC_ERR;
      }
      VM_DATA_PUSH((int32_t)csc[ridx]);
      break;
    }

    case EPA_OP_POP_R: {
      uint8_t ridx = code[p];
      if (ridx >= 4u) {
        snprintf(err, EPA_MAX_ERR, "POP_R bad reg %u", (unsigned)ridx);
        return EPA_NF_EXEC_ERR;
      }
      int32_t v; VM_DATA_POP(v);
      csc[ridx] = (uint32_t)v;
      break;
    }

    case EPA_OP_ADD_I32: {
      int32_t b,a; VM_DATA_POP(b); VM_DATA_POP(a);
      VM_DATA_PUSH(a + b);
      break;
    }

    case EPA_OP_SUB_I32: {
      int32_t b,a; VM_DATA_POP(b); VM_DATA_POP(a);
      VM_DATA_PUSH(a - b);
      break;
    }

    case EPA_OP_MUL_I32: {
      int32_t b,a; VM_DATA_POP(b); VM_DATA_POP(a);
      VM_DATA_PUSH(a * b);
      break;
    }

    case EPA_OP_LT_I32: {
      int32_t b,a; VM_DATA_POP(b); VM_DATA_POP(a);
      csc[0] = (uint32_t)((a < b) ? 1u : 0u);
      break;
    }

    case EPA_OP_STORE_L: {
      uint8_t idx = code[p];
      if (idx >= 32) { snprintf(err, EPA_MAX_ERR, "STORE_L idx out of range: %u", (unsigned)idx); return EPA_NF_EXEC_ERR; }
      int32_t v; VM_DATA_POP(v);
      locals[idx] = v;
      break;
    }

    case EPA_OP_LOAD_L: {
      uint8_t idx = code[p];
      if (idx >= 32) { snprintf(err, EPA_MAX_ERR, "LOAD_L idx out of range: %u", (unsigned)idx); return EPA_NF_EXEC_ERR; }
      VM_DATA_PUSH(locals[idx]);
      break;
    }

    case EPA_OP_GPU_PRESENT: {
      vp_present_gl(vp);
      if (!vp_pump(vp)) {
        // treat as "halt"
        eip->rel_pc = (uint32_t)(pc + need);
        vm->stack.sp = sp;
        return EPA_NF_EXEC_HALT;
      }
      break;
    }

    case EPA_OP_GPU_RES_DELETE: {
      uint32_t kind = EPA_READ_U32_LE(code, p + 0);
      uint32_t id   = EPA_READ_U32_LE(code, p + 4);
      if (!res_id_ok(id)) { snprintf(err, EPA_MAX_ERR, "GPU_RES_DELETE id out of range: %u", id); return EPA_NF_EXEC_ERR; }

      if (kind == EPA_RK_BUFFER) {
        GLuint h = impl->buffers[id];
        if (h) { p_glDeleteBuffers(1, &h); impl->buffers[id] = 0; }
      } else if (kind == EPA_RK_TEX) {
        GLuint h = impl->textures[id];
        if (h) { glDeleteTextures(1, &h); impl->textures[id] = 0; }
      } else if (kind == EPA_RK_SAMPLER) {
        GLuint h = impl->samplers[id];
        if (h) { p_glDeleteSamplers(1, &h); impl->samplers[id] = 0; }
      } else if (kind == EPA_RK_SHADER) {
              GLuint h = impl->shaders[id];
              if (h) { p_glDeleteShader(h); impl->shaders[id] = 0; }
      } else if (kind == EPA_RK_PROGRAM) {
              GLuint h = impl->programs[id];
              if (h) { p_glDeleteProgram(h); impl->programs[id] = 0; }
      } else if (kind == EPA_RK_QUERY) {
    	  GLuint q = impl->queries[id];
    	  if (q) { if (!p_glDeleteQueries) { snprintf(err, EPA_MAX_ERR, "glDeleteQueries missing"); return EPA_NF_EXEC_ERR; }
    	           p_glDeleteQueries(1, &q); impl->queries[id] = 0; }
    	} else if (kind == EPA_RK_FENCE) {
    	  GLsync s = impl->fences[id];
    	  if (s) { if (!p_glDeleteSync) { snprintf(err, EPA_MAX_ERR, "glDeleteSync missing"); return EPA_NF_EXEC_ERR; }
    	           p_glDeleteSync(s); impl->fences[id] = 0; }
     } else {
        snprintf(err, EPA_MAX_ERR, "GPU_RES_DELETE unsupported kind=%u (batch1 supports buffer/tex/sampler)", kind);
        return EPA_NF_EXEC_ERR;
      }
      impl->tex_targets[id] = 0;

      break;
    }

    case EPA_OP_GPU_BUF_CREATE: {
      uint32_t id     = EPA_READ_U32_LE(code, p + 0);
      uint32_t target = EPA_READ_U32_LE(code, p + 4);
      uint32_t usage  = EPA_READ_U32_LE(code, p + 8);
      uint32_t sizeb  = EPA_READ_U32_LE(code, p + 12);

      if (!res_id_ok(id)) { snprintf(err, EPA_MAX_ERR, "GPU_BUF_CREATE id out of range: %u", id); return EPA_NF_EXEC_ERR; }

      GLenum glt = map_buf_target(target);
      if (!glt) { snprintf(err, EPA_MAX_ERR, "GPU_BUF_CREATE invalid target=%u", target); return EPA_NF_EXEC_ERR; }

      if (impl->buffers[id]) {
        GLuint old = impl->buffers[id];
        p_glDeleteBuffers(1, &old);
        impl->buffers[id] = 0;
      }

      GLuint h = 0;
      p_glGenBuffers(1, &h);
      p_glBindBuffer(glt, h);
      p_glBufferData(glt, (GLsizeiptr)sizeb, NULL, map_buf_usage(usage));
      p_glBindBuffer(glt, 0);

      impl->buffers[id] = h;
      break;
    }

    case EPA_OP_GPU_BUF_BIND: {
      uint32_t target = EPA_READ_U32_LE(code, p + 0);
      uint32_t id     = EPA_READ_U32_LE(code, p + 4);
      if (!res_id_ok(id)) { snprintf(err, EPA_MAX_ERR, "GPU_BUF_BIND id out of range: %u", id); return EPA_NF_EXEC_ERR; }

      GLenum glt = map_buf_target(target);
      if (!glt) { snprintf(err, EPA_MAX_ERR, "GPU_BUF_BIND invalid target=%u", target); return EPA_NF_EXEC_ERR; }

      GLuint h = impl->buffers[id];
      if (!h) { snprintf(err, EPA_MAX_ERR, "GPU_BUF_BIND missing buffer id=%u", id); return EPA_NF_EXEC_ERR; }

      p_glBindBuffer(glt, h);
      if (glt == GL_ARRAY_BUFFER) impl->bound_array_buf = h;
      if (glt == GL_ELEMENT_ARRAY_BUFFER) impl->bound_elem_buf = h;
      break;
    }

    case EPA_OP_GPU_BUF_BIND_BASE: {
      uint32_t target = EPA_READ_U32_LE(code, p + 0);
      uint32_t index  = EPA_READ_U32_LE(code, p + 4);
      uint32_t id     = EPA_READ_U32_LE(code, p + 8);
      if (!res_id_ok(id)) { snprintf(err, EPA_MAX_ERR, "GPU_BUF_BIND_BASE id out of range: %u", id); return EPA_NF_EXEC_ERR; }

      GLenum glt = map_buf_target(target);
      if (!(glt == GL_UNIFORM_BUFFER || glt == GL_SHADER_STORAGE_BUFFER)) {
        snprintf(err, EPA_MAX_ERR, "GPU_BUF_BIND_BASE target must be UNIFORM/SSBO, got %u", target);
        return EPA_NF_EXEC_ERR;
      }

      GLuint h = impl->buffers[id];
      if (!h) { snprintf(err, EPA_MAX_ERR, "GPU_BUF_BIND_BASE missing buffer id=%u", id); return EPA_NF_EXEC_ERR; }

      p_glBindBufferBase(glt, (GLuint)index, h);
      break;
    }

    case EPA_OP_GPU_BUF_SUBDATA: {
      // variable: header(16) u32 id, u32 target, u32 offset, u32 byte_len + bytes
      uint32_t id       = EPA_READ_U32_LE(code, p + 0);
      uint32_t target   = EPA_READ_U32_LE(code, p + 4);
      uint32_t offset   = EPA_READ_U32_LE(code, p + 8);
      uint32_t byte_len = EPA_READ_U32_LE(code, p + 12);

      if (!res_id_ok(id)) { snprintf(err, EPA_MAX_ERR, "GPU_BUF_SUBDATA id out of range: %u", id); return EPA_NF_EXEC_ERR; }

      GLenum glt = map_buf_target(target);
      if (!glt) { snprintf(err, EPA_MAX_ERR, "GPU_BUF_SUBDATA invalid target=%u", target); return EPA_NF_EXEC_ERR; }

      GLuint h = impl->buffers[id];
      if (!h) { snprintf(err, EPA_MAX_ERR, "GPU_BUF_SUBDATA missing buffer id=%u", id); return EPA_NF_EXEC_ERR; }

      const uint8_t *bytes = code + (pc + 2 + 16);
      p_glBindBuffer(glt, h);
      p_glBufferSubData(glt, (GLintptr)offset, (GLsizeiptr)byte_len, bytes);
      // leave bound as-is (common to not unbind)
      break;
    }

    case EPA_OP_GPU_TEX_CREATE: {
      uint32_t tex_id  = EPA_READ_U32_LE(code, p + 0);
      uint32_t dim     = EPA_READ_U32_LE(code, p + 4);
      uint32_t w_      = EPA_READ_U32_LE(code, p + 8);
      uint32_t h_      = EPA_READ_U32_LE(code, p + 12);
      uint32_t d_or_l  = EPA_READ_U32_LE(code, p + 16);
      uint32_t levels  = EPA_READ_U32_LE(code, p + 20);
      uint32_t fmt     = EPA_READ_U32_LE(code, p + 24);

      if (!res_id_ok(tex_id)) { snprintf(err, EPA_MAX_ERR, "GPU_TEX_CREATE id out of range: %u", tex_id); return EPA_NF_EXEC_ERR; }
      GLenum tgt = map_tex_dim(dim);
      if (!tgt) { snprintf(err, EPA_MAX_ERR, "GPU_TEX_CREATE invalid dim=%u", dim); return EPA_NF_EXEC_ERR; }

      GLint internal = 0; GLenum upload_fmt = 0; GLenum upload_type = 0;
      if (!map_tex_format(fmt, &internal, &upload_fmt, &upload_type)) {
        snprintf(err, EPA_MAX_ERR, "GPU_TEX_CREATE unsupported format=%u", fmt);
        return EPA_NF_EXEC_ERR;
      }

      if (impl->textures[tex_id]) {
        GLuint old = impl->textures[tex_id];
        glDeleteTextures(1, &old);
        impl->textures[tex_id] = 0;
      }

      GLuint th = 0;
      glGenTextures(1, &th);
      glBindTexture(tgt, th);

      // minimal allocation (no data)
      if (tgt == GL_TEXTURE_2D) {
        p_glTexStorage2D(GL_TEXTURE_2D, (GLint)levels, internal, (GLsizei)w_, (GLsizei)h_);
      } else if (tgt == GL_TEXTURE_3D) {
        p_glTexStorage3D(GL_TEXTURE_3D, (GLint)levels, internal, (GLsizei)w_, (GLsizei)h_, (GLsizei)d_or_l);
      } else if (tgt == GL_TEXTURE_2D_ARRAY) {
        p_glTexStorage3D(GL_TEXTURE_2D_ARRAY, (GLint)levels, internal, (GLsizei)w_, (GLsizei)h_, (GLsizei)d_or_l);
      } else if (tgt == GL_TEXTURE_CUBE_MAP) {
        // d_or_l ignored; allocate cubemap faces via TexStorage2D
        p_glTexStorage2D(GL_TEXTURE_CUBE_MAP, (GLint)levels, internal, (GLsizei)w_, (GLsizei)h_);
      } else {
        snprintf(err, EPA_MAX_ERR, "GPU_TEX_CREATE dim target unsupported in batch1");
        glDeleteTextures(1, &th);
        return EPA_NF_EXEC_ERR;
      }

      // sensible defaults
      glTexParameteri(tgt, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(tgt, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(tgt, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(tgt, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      impl->textures[tex_id] = th;
      impl->tex_targets[tex_id] = tgt;

      break;
    }

    case EPA_OP_GPU_TEX_BIND_UNIT: {
      uint32_t unit       = EPA_READ_U32_LE(code, p + 0);
      uint32_t tex_id     = EPA_READ_U32_LE(code, p + 4);
      uint32_t sampler_id = EPA_READ_U32_LE(code, p + 8);

      if (!res_id_ok(tex_id)) { snprintf(err, EPA_MAX_ERR, "GPU_TEX_BIND_UNIT tex id out of range: %u", tex_id); return EPA_NF_EXEC_ERR; }
      if (sampler_id && !res_id_ok(sampler_id)) { snprintf(err, EPA_MAX_ERR, "GPU_TEX_BIND_UNIT sampler id out of range: %u", sampler_id); return EPA_NF_EXEC_ERR; }

      GLuint th = impl->textures[tex_id];
      if (!th) { snprintf(err, EPA_MAX_ERR, "GPU_TEX_BIND_UNIT missing texture id=%u", tex_id); return EPA_NF_EXEC_ERR; }

      p_glActiveTexture(GL_TEXTURE0 + (GLenum)unit);

      GLenum tgt = impl->tex_targets[tex_id];
      if (!tgt) tgt = GL_TEXTURE_2D;
      glBindTexture(tgt, th);

      if (sampler_id) {
        GLuint sh = impl->samplers[sampler_id];
        if (!sh) { snprintf(err, EPA_MAX_ERR, "GPU_TEX_BIND_UNIT missing sampler id=%u", sampler_id); return EPA_NF_EXEC_ERR; }
        p_glBindSampler((GLuint)unit, sh);
      } else {
        p_glBindSampler((GLuint)unit, 0);
      }
      break;
    }

    case EPA_OP_GPU_SAMPLER_CREATE: {
      uint32_t sid = EPA_READ_U32_LE(code, p + 0);
      if (!res_id_ok(sid)) { snprintf(err, EPA_MAX_ERR, "GPU_SAMPLER_CREATE id out of range: %u", sid); return EPA_NF_EXEC_ERR; }

      if (impl->samplers[sid]) {
        GLuint old = impl->samplers[sid];
        p_glDeleteSamplers(1, &old);
        impl->samplers[sid] = 0;
      }

      GLuint s = 0;
      p_glGenSamplers(1, &s);

      // defaults
      p_glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      p_glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      p_glSamplerParameteri(s, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      p_glSamplerParameteri(s, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      impl->samplers[sid] = s;
      break;
    }

    case EPA_OP_GPU_SAMPLER_PARAM: {
      uint32_t sid   = EPA_READ_U32_LE(code, p + 0);
      uint32_t pname = EPA_READ_U32_LE(code, p + 4);
      uint32_t v0    = EPA_READ_U32_LE(code, p + 8);
      uint32_t v1    = EPA_READ_U32_LE(code, p + 12);
      (void)v1;

      if (!res_id_ok(sid)) { snprintf(err, EPA_MAX_ERR, "GPU_SAMPLER_PARAM id out of range: %u", sid); return EPA_NF_EXEC_ERR; }
      GLuint s = impl->samplers[sid];
      if (!s) { snprintf(err, EPA_MAX_ERR, "GPU_SAMPLER_PARAM missing sampler id=%u", sid); return EPA_NF_EXEC_ERR; }

      // Minimal pname mapping (extend as you lock down your sampler param enums)
      // For now interpret pname as:
      // 0=min_filter, 1=mag_filter, 2=wrap_s, 3=wrap_t
      switch (pname) {
        case 0: p_glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, (GLint)v0); break;
        case 1: p_glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, (GLint)v0); break;
        case 2: p_glSamplerParameteri(s, GL_TEXTURE_WRAP_S, (GLint)v0); break;
        case 3: p_glSamplerParameteri(s, GL_TEXTURE_WRAP_T, (GLint)v0); break;
        default:
          snprintf(err, EPA_MAX_ERR, "GPU_SAMPLER_PARAM unsupported pname=%u (batch1)", pname);
          return EPA_NF_EXEC_ERR;
      }
      break;
    }

    // -------- Batch 2: vertex layout (portable VAO) --------

    case EPA_OP_GPU_VTX_LAYOUT_CREATE: {
      uint32_t layout_id = EPA_READ_U32_LE(code, p + 0);
      if (!res_id_ok(layout_id)) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_LAYOUT_CREATE id out of range: %u", layout_id); return EPA_NF_EXEC_ERR; }

      if (impl->vtx_layouts[layout_id]) {
        GLuint old = impl->vtx_layouts[layout_id];
        p_glDeleteVertexArrays(1, &old);
        impl->vtx_layouts[layout_id] = 0;
      }

      GLuint vao = 0;
      p_glGenVertexArrays(1, &vao);
      impl->vtx_layouts[layout_id] = vao;
      break;
    }

    case EPA_OP_GPU_VTX_LAYOUT_BIND: {
      uint32_t layout_id = EPA_READ_U32_LE(code, p + 0);
      if (!res_id_ok(layout_id)) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_LAYOUT_BIND id out of range: %u", layout_id); return EPA_NF_EXEC_ERR; }

      GLuint vao = impl->vtx_layouts[layout_id];
      if (!vao) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_LAYOUT_BIND missing layout id=%u", layout_id); return EPA_NF_EXEC_ERR; }

      p_glBindVertexArray(vao);
      impl->bound_vao = vao;
      break;
    }

    case EPA_OP_GPU_VTX_BIND_VBO: {
      uint32_t layout_id = EPA_READ_U32_LE(code, p + 0);
      uint32_t vbo_id    = EPA_READ_U32_LE(code, p + 4);

      if (!res_id_ok(layout_id) || !res_id_ok(vbo_id)) {
        snprintf(err, EPA_MAX_ERR, "GPU_VTX_BIND_VBO id out of range layout=%u vbo=%u", layout_id, vbo_id);
        return EPA_NF_EXEC_ERR;
      }

      GLuint vao = impl->vtx_layouts[layout_id];
      if (!vao) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_BIND_VBO missing layout id=%u", layout_id); return EPA_NF_EXEC_ERR; }

      GLuint vbo = impl->buffers[vbo_id];
      if (!vbo) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_BIND_VBO missing buffer id=%u", vbo_id); return EPA_NF_EXEC_ERR; }

      p_glBindVertexArray(vao);
      p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
      impl->bound_vao = vao;
      impl->bound_array_buf = vbo;
      break;
    }

    case EPA_OP_GPU_VTX_BIND_EBO: {
      uint32_t layout_id = EPA_READ_U32_LE(code, p + 0);
      uint32_t ebo_id    = EPA_READ_U32_LE(code, p + 4);

      if (!res_id_ok(layout_id) || !res_id_ok(ebo_id)) {
        snprintf(err, EPA_MAX_ERR, "GPU_VTX_BIND_EBO id out of range layout=%u ebo=%u", layout_id, ebo_id);
        return EPA_NF_EXEC_ERR;
      }

      GLuint vao = impl->vtx_layouts[layout_id];
      if (!vao) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_BIND_EBO missing layout id=%u", layout_id); return EPA_NF_EXEC_ERR; }

      GLuint ebo = impl->buffers[ebo_id];
      if (!ebo) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_BIND_EBO missing buffer id=%u", ebo_id); return EPA_NF_EXEC_ERR; }

      p_glBindVertexArray(vao);
      p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
      impl->bound_vao = vao;
      impl->bound_elem_buf = ebo;
      break;
    }

    case EPA_OP_GPU_VTX_ATTRIB_FMT: {
      uint32_t layout_id   = EPA_READ_U32_LE(code, p + 0);
      uint32_t attr_index  = EPA_READ_U32_LE(code, p + 4);
      uint32_t components  = EPA_READ_U32_LE(code, p + 8);
      uint32_t elem_type   = EPA_READ_U32_LE(code, p + 12);
      uint32_t normalized  = EPA_READ_U32_LE(code, p + 16);
      uint32_t stride      = EPA_READ_U32_LE(code, p + 20);
      uint32_t offset      = EPA_READ_U32_LE(code, p + 24);

      if (!res_id_ok(layout_id)) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_ATTRIB_FMT layout id out of range: %u", layout_id); return EPA_NF_EXEC_ERR; }
      if (components < 1 || components > 4) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_ATTRIB_FMT components invalid: %u", components); return EPA_NF_EXEC_ERR; }
      if (attr_index > 31) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_ATTRIB_FMT attr_index too large: %u", attr_index); return EPA_NF_EXEC_ERR; }

      GLuint vao = impl->vtx_layouts[layout_id];
      if (!vao) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_ATTRIB_FMT missing layout id=%u", layout_id); return EPA_NF_EXEC_ERR; }

      GLenum gl_type = 0;
      GLint type_bytes = 0;
      if (!map_attrib_type(elem_type, &gl_type, &type_bytes)) {
        snprintf(err, EPA_MAX_ERR, "GPU_VTX_ATTRIB_FMT unsupported elem_type=%u", elem_type);
        return EPA_NF_EXEC_ERR;
      }

      p_glBindVertexArray(vao);

      // Requires a VBO bound on GL_ARRAY_BUFFER for VAO state; we assume user bound it via GPU_VTX_BIND_VBO.
      p_glVertexAttribPointer((GLuint)attr_index,
                            (GLint)components,
                            gl_type,
                            normalized ? GL_TRUE : GL_FALSE,
                            (GLsizei)stride,
                            (const void*)(uintptr_t)offset);
      // Do not auto-enable; that’s GPU_VTX_ATTRIB_ENABLE.
      impl->bound_vao = vao;
      break;
    }

    case EPA_OP_GPU_VTX_ATTRIB_ENABLE: {
      uint32_t layout_id  = EPA_READ_U32_LE(code, p + 0);
      uint32_t attr_index = EPA_READ_U32_LE(code, p + 4);
      uint32_t enable     = EPA_READ_U32_LE(code, p + 8);

      if (!res_id_ok(layout_id)) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_ATTRIB_ENABLE layout id out of range: %u", layout_id); return EPA_NF_EXEC_ERR; }
      if (attr_index > 31) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_ATTRIB_ENABLE attr_index too large: %u", attr_index); return EPA_NF_EXEC_ERR; }

      GLuint vao = impl->vtx_layouts[layout_id];
      if (!vao) { snprintf(err, EPA_MAX_ERR, "GPU_VTX_ATTRIB_ENABLE missing layout id=%u", layout_id); return EPA_NF_EXEC_ERR; }

      p_glBindVertexArray(vao);
      if (enable) p_glEnableVertexAttribArray((GLuint)attr_index);
      else        p_glDisableVertexAttribArray((GLuint)attr_index);
      impl->bound_vao = vao;
      break;
    }

    // -------- Batch 2: state (AAA baseline) --------

    case EPA_OP_GPU_SET_DEPTH: {
      uint32_t enable = EPA_READ_U32_LE(code, p + 0);
      uint32_t func   = EPA_READ_U32_LE(code, p + 4);
      uint32_t writee = EPA_READ_U32_LE(code, p + 8);

      if (enable) glEnable(GL_DEPTH_TEST);
      else        glDisable(GL_DEPTH_TEST);

      if (enable) {
        GLenum glf = map_compare_func(func);
        if (!glf) { snprintf(err, EPA_MAX_ERR, "GPU_SET_DEPTH invalid func=%u", func); return EPA_NF_EXEC_ERR; }
        glDepthFunc(glf);
      }

      glDepthMask(writee ? GL_TRUE : GL_FALSE);
      break;
    }

    case EPA_OP_GPU_SET_CULL: {
      uint32_t cull_mode  = EPA_READ_U32_LE(code, p + 0);
      uint32_t front_face = EPA_READ_U32_LE(code, p + 4);
      (void)EPA_READ_U32_LE(code, p + 8); // unused

      if (cull_mode == EPA_CULL_NONE) {
        glDisable(GL_CULL_FACE);
      } else {
        GLenum glc = map_cull_face(cull_mode);
        GLenum glff = map_front_face(front_face);
        if (!glc || !glff) {
          snprintf(err, EPA_MAX_ERR, "GPU_SET_CULL invalid cull=%u front=%u", cull_mode, front_face);
          return EPA_NF_EXEC_ERR;
        }
        glEnable(GL_CULL_FACE);
        glCullFace(glc);
        glFrontFace(glff);
      }
      break;
    }

    case EPA_OP_GPU_SET_BLEND: {
      uint32_t enable  = EPA_READ_U32_LE(code, p + 0);
      uint32_t src_rgb = EPA_READ_U32_LE(code, p + 4);
      uint32_t dst_rgb = EPA_READ_U32_LE(code, p + 8);
      uint32_t eq_rgb  = EPA_READ_U32_LE(code, p + 12);
      uint32_t eq_a    = EPA_READ_U32_LE(code, p + 16);

      if (enable) glEnable(GL_BLEND);
      else        glDisable(GL_BLEND);

      if (enable) {
        GLenum gls = map_blend_factor(src_rgb);
        GLenum gld = map_blend_factor(dst_rgb);
        GLenum gleq_rgb = map_blend_eq(eq_rgb);
        GLenum gleq_a   = map_blend_eq(eq_a);
        if (!gls || !gld || !gleq_rgb || !gleq_a) {
          snprintf(err, EPA_MAX_ERR, "GPU_SET_BLEND invalid factors/eq (src=%u dst=%u eqrgb=%u eqa=%u)",
                   src_rgb, dst_rgb, eq_rgb, eq_a);
          return EPA_NF_EXEC_ERR;
        }
        glBlendFunc(gls, gld);
        p_glBlendEquationSeparate(gleq_rgb, gleq_a);
      }
      break;
    }

    case EPA_OP_GPU_SET_SCISSOR: {
      uint32_t enable = EPA_READ_U32_LE(code, p + 0);
      int32_t x  = (int32_t)EPA_READ_U32_LE(code, p + 4);
      int32_t y  = (int32_t)EPA_READ_U32_LE(code, p + 8);
      int32_t ww = (int32_t)EPA_READ_U32_LE(code, p + 12);
      int32_t hh = (int32_t)EPA_READ_U32_LE(code, p + 16);

      if (enable) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, y, ww, hh);
      } else {
        glDisable(GL_SCISSOR_TEST);
      }
      break;
    }

    // -------- Batch 3: Render targets (portable FBO) --------

    case EPA_OP_GPU_RT_CREATE: {
      uint32_t rt_id = EPA_READ_U32_LE(code, p + 0);
      if (!res_id_ok(rt_id)) { snprintf(err, EPA_MAX_ERR, "GPU_RT_CREATE id out of range: %u", rt_id); return EPA_NF_EXEC_ERR; }
      if (rt_id == 0) { /* 0 is default/backbuffer; nothing to create */ break; }

      if (impl->rts[rt_id]) {
        GLuint old = impl->rts[rt_id];
        p_glDeleteFramebuffers(1, &old);
        impl->rts[rt_id] = 0;
      }

      GLuint fbo = 0;
      p_glGenFramebuffers(1, &fbo);
      impl->rts[rt_id] = fbo;
      break;
    }

    case EPA_OP_GPU_RT_BIND: {
      uint32_t rt_id = EPA_READ_U32_LE(code, p + 0);
      if (!res_id_ok(rt_id)) { snprintf(err, EPA_MAX_ERR, "GPU_RT_BIND id out of range: %u", rt_id); return EPA_NF_EXEC_ERR; }

      if (rt_id == 0) {
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        impl->bound_fbo = 0;
        break;
      }

      GLuint fbo = impl->rts[rt_id];
      if (!fbo) { snprintf(err, EPA_MAX_ERR, "GPU_RT_BIND missing rt id=%u", rt_id); return EPA_NF_EXEC_ERR; }

      p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      impl->bound_fbo = fbo;
      break;
    }

    case EPA_OP_GPU_RT_ATTACH_TEX: {
      uint32_t rt_id      = EPA_READ_U32_LE(code, p + 0);
      uint32_t attachment = EPA_READ_U32_LE(code, p + 4);
      uint32_t tex_id     = EPA_READ_U32_LE(code, p + 8);
      uint32_t level      = EPA_READ_U32_LE(code, p + 12);
      uint32_t layer      = EPA_READ_U32_LE(code, p + 16);

      if (!res_id_ok(rt_id) || !res_id_ok(tex_id)) {
        snprintf(err, EPA_MAX_ERR, "GPU_RT_ATTACH_TEX id out of range rt=%u tex=%u", rt_id, tex_id);
        return EPA_NF_EXEC_ERR;
      }
      if (rt_id == 0) { snprintf(err, EPA_MAX_ERR, "GPU_RT_ATTACH_TEX cannot attach to default RT (0)"); return EPA_NF_EXEC_ERR; }

      GLuint fbo = impl->rts[rt_id];
      if (!fbo) { snprintf(err, EPA_MAX_ERR, "GPU_RT_ATTACH_TEX missing rt id=%u", rt_id); return EPA_NF_EXEC_ERR; }

      GLuint th = impl->textures[tex_id];
      if (!th) { snprintf(err, EPA_MAX_ERR, "GPU_RT_ATTACH_TEX missing texture id=%u", tex_id); return EPA_NF_EXEC_ERR; }

      GLenum att = map_rt_attachment(attachment);
      if (!att) { snprintf(err, EPA_MAX_ERR, "GPU_RT_ATTACH_TEX invalid attachment=%u", attachment); return EPA_NF_EXEC_ERR; }

      p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);

      // NOTE: we currently assume TEXTURE_2D. If you need arrays/3D/cube, we’ll store per-tex dim in Batch 4.
      if (layer != 0) {
        // For array/3D use cases; works for TEXTURE_2D_ARRAY / 3D.
        p_glFramebufferTextureLayer(GL_FRAMEBUFFER, att, th, (GLint)level, (GLint)layer);
      } else {
        p_glFramebufferTexture2D(GL_FRAMEBUFFER, att, GL_TEXTURE_2D, th, (GLint)level);
      }

      GLenum st = p_glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (st != GL_FRAMEBUFFER_COMPLETE) {
        snprintf(err, EPA_MAX_ERR, "GPU_RT_ATTACH_TEX framebuffer incomplete status=0x%04x", (unsigned)st);
        return EPA_NF_EXEC_ERR;
      }

      impl->bound_fbo = fbo;
      break;
    }

    case EPA_OP_GPU_RT_BLIT: {
      uint32_t src_rt   = EPA_READ_U32_LE(code, p + 0);
      uint32_t dst_rt   = EPA_READ_U32_LE(code, p + 4);
      int32_t src_x0    = (int32_t)EPA_READ_U32_LE(code, p + 8);
      int32_t src_y0    = (int32_t)EPA_READ_U32_LE(code, p + 12);
      int32_t src_x1    = (int32_t)EPA_READ_U32_LE(code, p + 16);
      int32_t src_y1    = (int32_t)EPA_READ_U32_LE(code, p + 20);
      int32_t dst_x0    = (int32_t)EPA_READ_U32_LE(code, p + 24);
      int32_t dst_y0    = (int32_t)EPA_READ_U32_LE(code, p + 28);
      int32_t dst_x1    = (int32_t)EPA_READ_U32_LE(code, p + 32);
      int32_t dst_y1    = (int32_t)EPA_READ_U32_LE(code, p + 36);
      uint32_t mask_bits= EPA_READ_U32_LE(code, p + 40);
      uint32_t filter   = EPA_READ_U32_LE(code, p + 44);

      if (!res_id_ok(src_rt) || !res_id_ok(dst_rt)) {
        snprintf(err, EPA_MAX_ERR, "GPU_RT_BLIT id out of range src=%u dst=%u", src_rt, dst_rt);
        return EPA_NF_EXEC_ERR;
      }

      GLuint src_fbo = (src_rt == 0) ? 0 : impl->rts[src_rt];
      GLuint dst_fbo = (dst_rt == 0) ? 0 : impl->rts[dst_rt];
      if (src_rt != 0 && !src_fbo) { snprintf(err, EPA_MAX_ERR, "GPU_RT_BLIT missing src rt id=%u", src_rt); return EPA_NF_EXEC_ERR; }
      if (dst_rt != 0 && !dst_fbo) { snprintf(err, EPA_MAX_ERR, "GPU_RT_BLIT missing dst rt id=%u", dst_rt); return EPA_NF_EXEC_ERR; }

      p_glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo);
      p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);

      GLbitfield mask = map_blit_mask(mask_bits);
      if (!mask) { snprintf(err, EPA_MAX_ERR, "GPU_RT_BLIT mask_bits produced 0 (mask=%u)", mask_bits); return EPA_NF_EXEC_ERR; }

      p_glBlitFramebuffer(src_x0, src_y0, src_x1, src_y1,
                        dst_x0, dst_y0, dst_x1, dst_y1,
                        mask,
                        map_blit_filter(filter));

      // Restore to “normal” binding (optional). Keep draw framebuffer as current.
      p_glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
      impl->bound_fbo = dst_fbo;
      break;
    }

    // -------- Batch 3: missing state --------

    case EPA_OP_GPU_SET_COLOR_MASK: {
      uint32_t r = EPA_READ_U32_LE(code, p + 0);
      uint32_t g = EPA_READ_U32_LE(code, p + 4);
      uint32_t b = EPA_READ_U32_LE(code, p + 8);
      uint32_t a = EPA_READ_U32_LE(code, p + 12);
      glColorMask(r ? GL_TRUE : GL_FALSE,
                  g ? GL_TRUE : GL_FALSE,
                  b ? GL_TRUE : GL_FALSE,
                  a ? GL_TRUE : GL_FALSE);
      break;
    }

    case EPA_OP_GPU_SET_DEPTH_BIAS: {
      float factor = read_f32_le(code, p + 0);
      float units  = read_f32_le(code, p + 4);

      // Semantics: if both are 0, disable polygon offset fill
      if (factor == 0.0f && units == 0.0f) {
        glDisable(GL_POLYGON_OFFSET_FILL);
      } else {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(factor, units);
      }
      break;
    }

    // -------- Batch 3: real draw paths (VAO + program) --------

    case EPA_OP_GPU_DRAW_INDEXED: {
      uint32_t prim       = EPA_READ_U32_LE(code, p + 0);
      uint32_t index_type = EPA_READ_U32_LE(code, p + 4);
      uint32_t count      = EPA_READ_U32_LE(code, p + 8);
      uint32_t index_off  = EPA_READ_U32_LE(code, p + 12);
      uint32_t base_vert  = EPA_READ_U32_LE(code, p + 16);

      GLenum glprim = map_prim(prim);
      GLenum glidx  = map_index_type(index_type);
      if (!glprim) { snprintf(err, EPA_MAX_ERR, "GPU_DRAW_INDEXED invalid prim=%u", prim); return EPA_NF_EXEC_ERR; }
      if (!glidx)  { snprintf(err, EPA_MAX_ERR, "GPU_DRAW_INDEXED invalid index_type=%u", index_type); return EPA_NF_EXEC_ERR; }

      // Requires: VAO bound, EBO bound in VAO state, program in use.
      p_glDrawElementsBaseVertex(glprim,
                               (GLsizei)count,
                               glidx,
                               (const void*)(uintptr_t)index_off,
                               (GLint)(int32_t)base_vert);
      break;
    }

    case EPA_OP_GPU_DRAW_INSTANCED: {
      uint32_t prim      = EPA_READ_U32_LE(code, p + 0);
      uint32_t first     = EPA_READ_U32_LE(code, p + 4);
      uint32_t count     = EPA_READ_U32_LE(code, p + 8);
      uint32_t instances = EPA_READ_U32_LE(code, p + 12);

      GLenum glprim = map_prim(prim);
      if (!glprim) { snprintf(err, EPA_MAX_ERR, "GPU_DRAW_INSTANCED invalid prim=%u", prim); return EPA_NF_EXEC_ERR; }

      p_glDrawArraysInstanced(glprim,
                            (GLint)first,
                            (GLsizei)count,
                            (GLsizei)instances);
      break;
    }

    case EPA_OP_GPU_DRAW_INDEXED_INST: {
      uint32_t prim       = EPA_READ_U32_LE(code, p + 0);
      uint32_t index_type = EPA_READ_U32_LE(code, p + 4);
      uint32_t count      = EPA_READ_U32_LE(code, p + 8);
      uint32_t index_off  = EPA_READ_U32_LE(code, p + 12);
      uint32_t instances  = EPA_READ_U32_LE(code, p + 16);
      uint32_t base_vert  = EPA_READ_U32_LE(code, p + 20);

      GLenum glprim = map_prim(prim);
      GLenum glidx  = map_index_type(index_type);
      if (!glprim) { snprintf(err, EPA_MAX_ERR, "GPU_DRAW_INDEXED_INST invalid prim=%u", prim); return EPA_NF_EXEC_ERR; }
      if (!glidx)  { snprintf(err, EPA_MAX_ERR, "GPU_DRAW_INDEXED_INST invalid index_type=%u", index_type); return EPA_NF_EXEC_ERR; }

      p_glDrawElementsInstancedBaseVertex(glprim,
                                        (GLsizei)count,
                                        glidx,
                                        (const void*)(uintptr_t)index_off,
                                        (GLsizei)instances,
                                        (GLint)(int32_t)base_vert);
      break;
    }

    // -------- Batch 3: barrier --------

    case EPA_OP_GPU_MEMORY_BARRIER: {
      uint32_t bits = EPA_READ_U32_LE(code, p + 0);
      GLbitfield glb = map_barrier_bits(bits);
      if (!glb) {
        // Allow 0 as no-op; treat invalid bits as error only if nonzero and mapping failed
        if (bits != 0) { snprintf(err, EPA_MAX_ERR, "GPU_MEMORY_BARRIER invalid bits=0x%08x", bits); return EPA_NF_EXEC_ERR; }
        break;
      }
      p_glMemoryBarrier(glb);
      break;
    }

    // -------- Batch 4: shaders / programs --------

    case EPA_OP_GPU_SHADER_LOAD_BLOB: {
      // variable: header(16) u32 shader_id, u32 stage, u32 blob_kind, u32 byte_len + bytes
      uint32_t shader_id = EPA_READ_U32_LE(code, p + 0);
      uint32_t stage     = EPA_READ_U32_LE(code, p + 4);
      uint32_t blob_kind = EPA_READ_U32_LE(code, p + 8);
      uint32_t byte_len  = EPA_READ_U32_LE(code, p + 12);

      if (!res_id_ok(shader_id)) { snprintf(err, EPA_MAX_ERR, "GPU_SHADER_LOAD_BLOB id out of range: %u", shader_id); return EPA_NF_EXEC_ERR; }

      GLenum gl_stage = map_shader_stage(stage);
      if (!gl_stage) { snprintf(err, EPA_MAX_ERR, "GPU_SHADER_LOAD_BLOB unsupported stage=%u", stage); return EPA_NF_EXEC_ERR; }

      const uint8_t *bytes = code + (pc + 2 + 16);

      // For now: only GLSL text blobs are supported in GL backend.
      if (blob_kind != EPA_SB_GLSL) {
        snprintf(err, EPA_MAX_ERR, "GPU_SHADER_LOAD_BLOB blob_kind=%u not supported in GL backend (use EPA_SB_GLSL for now)", blob_kind);
        return EPA_NF_EXEC_ERR;
      }

      // Replace if exists
      if (impl->shaders[shader_id]) {
        p_glDeleteShader(impl->shaders[shader_id]);
        impl->shaders[shader_id] = 0;
      }

      GLuint sh = 0;
      if (!gl_compile_shader_from_glsl(gl_stage, (const char*)bytes, (size_t)byte_len, &sh, err)) {
        return EPA_NF_EXEC_ERR;
      }

      impl->shaders[shader_id] = sh;
      break;
    }

    case EPA_OP_GPU_PROGRAM_LINK_VF: {
      uint32_t program_id = EPA_READ_U32_LE(code, p + 0);
      uint32_t vs_id      = EPA_READ_U32_LE(code, p + 4);
      uint32_t fs_id      = EPA_READ_U32_LE(code, p + 8);

      if (!res_id_ok(program_id) || !res_id_ok(vs_id) || !res_id_ok(fs_id)) {
        snprintf(err, EPA_MAX_ERR, "GPU_PROGRAM_LINK_VF id out of range prog=%u vs=%u fs=%u", program_id, vs_id, fs_id);
        return EPA_NF_EXEC_ERR;
      }

      GLuint vs = impl->shaders[vs_id];
      GLuint fs = impl->shaders[fs_id];
      if (!vs || !fs) {
        snprintf(err, EPA_MAX_ERR, "GPU_PROGRAM_LINK_VF missing shader(s) vs=%u(fs=%u)", vs_id, fs_id);
        return EPA_NF_EXEC_ERR;
      }

      if (impl->programs[program_id]) {
        p_glDeleteProgram(impl->programs[program_id]);
        impl->programs[program_id] = 0;
      }

      GLuint pr = p_glCreateProgram();
      if (!pr) { snprintf(err, EPA_MAX_ERR, "glCreateProgram failed"); return EPA_NF_EXEC_ERR; }

      p_glAttachShader(pr, vs);
      p_glAttachShader(pr, fs);
      p_glLinkProgram(pr);

      GLint ok = 0;
      p_glGetProgramiv(pr, GL_LINK_STATUS, &ok);
      if (!ok) {
        char logbuf[2048];
        GLsizei n = 0;
        p_glGetProgramInfoLog(pr, (GLsizei)sizeof(logbuf), &n, logbuf);
        snprintf(err, EPA_MAX_ERR, "program link failed: %.*s", (int)n, logbuf);
        p_glDeleteProgram(pr);
        return EPA_NF_EXEC_ERR;
      }

      // Optional: detach after link
      p_glDetachShader(pr, vs);
      p_glDetachShader(pr, fs);

      impl->programs[program_id] = pr;
      break;
    }

    case EPA_OP_GPU_PROGRAM_USE: {
      uint32_t program_id = EPA_READ_U32_LE(code, p + 0);
      if (!res_id_ok(program_id)) { snprintf(err, EPA_MAX_ERR, "GPU_PROGRAM_USE id out of range: %u", program_id); return EPA_NF_EXEC_ERR; }
      GLuint pr = impl->programs[program_id];
      if (!pr) { snprintf(err, EPA_MAX_ERR, "GPU_PROGRAM_USE missing program id=%u", program_id); return EPA_NF_EXEC_ERR; }
      p_glUseProgram(pr);
      break;
    }

    // -------- Batch 4: texture upload --------

    case EPA_OP_GPU_TEX_SUBIMAGE: {
      uint32_t tex_id   = EPA_READ_U32_LE(code, p + 0);
      uint32_t level    = EPA_READ_U32_LE(code, p + 4);
      uint32_t x        = EPA_READ_U32_LE(code, p + 8);
      uint32_t y        = EPA_READ_U32_LE(code, p + 12);
      uint32_t zlayer   = EPA_READ_U32_LE(code, p + 16);
      uint32_t w_       = EPA_READ_U32_LE(code, p + 20);
      uint32_t h_       = EPA_READ_U32_LE(code, p + 24);
      uint32_t d_       = EPA_READ_U32_LE(code, p + 28);
      uint32_t fmt      = EPA_READ_U32_LE(code, p + 32);
      uint32_t px_type  = EPA_READ_U32_LE(code, p + 36);
      uint32_t byte_len = EPA_READ_U32_LE(code, p + 40);

      if (!res_id_ok(tex_id)) { snprintf(err, EPA_MAX_ERR, "GPU_TEX_SUBIMAGE id out of range: %u", tex_id); return EPA_NF_EXEC_ERR; }
      GLuint th = impl->textures[tex_id];
      if (!th) { snprintf(err, EPA_MAX_ERR, "GPU_TEX_SUBIMAGE missing texture id=%u", tex_id); return EPA_NF_EXEC_ERR; }

      GLint internal = 0; GLenum upload_fmt = 0; GLenum upload_type0 = 0;
      if (!map_tex_format(fmt, &internal, &upload_fmt, &upload_type0)) {
        snprintf(err, EPA_MAX_ERR, "GPU_TEX_SUBIMAGE unsupported format=%u", fmt);
        return EPA_NF_EXEC_ERR;
      }
      GLenum upload_type = map_px_type(px_type);

      const uint8_t *bytes = code + (pc + 2 + 44);

      GLenum tgt = impl->tex_targets[tex_id];
      if (!tgt) tgt = GL_TEXTURE_2D;

      glBindTexture(tgt, th);

      if (tgt == GL_TEXTURE_2D) {
        glTexSubImage2D(GL_TEXTURE_2D, (GLint)level, (GLint)x, (GLint)y,
                        (GLsizei)w_, (GLsizei)h_, upload_fmt, upload_type, bytes);
      } else if (tgt == GL_TEXTURE_3D) {
        p_glTexSubImage3D(GL_TEXTURE_3D, (GLint)level, (GLint)x, (GLint)y, (GLint)zlayer,
                        (GLsizei)w_, (GLsizei)h_, (GLsizei)d_, upload_fmt, upload_type, bytes);
      } else if (tgt == GL_TEXTURE_2D_ARRAY) {
        p_glTexSubImage3D(GL_TEXTURE_2D_ARRAY, (GLint)level, (GLint)x, (GLint)y, (GLint)zlayer,
                        (GLsizei)w_, (GLsizei)h_, (GLsizei)d_, upload_fmt, upload_type, bytes);
      } else if (tgt == GL_TEXTURE_CUBE_MAP) {
        // zlayer selects face 0..5
        GLenum face = (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)zlayer);
        glTexSubImage2D(face, (GLint)level, (GLint)x, (GLint)y,
                        (GLsizei)w_, (GLsizei)h_, upload_fmt, upload_type, bytes);
      } else {
        snprintf(err, EPA_MAX_ERR, "GPU_TEX_SUBIMAGE unsupported tex target=%u", (unsigned)tgt);
        return EPA_NF_EXEC_ERR;
      }

      (void)byte_len; // trusted by bounds-check already
      break;
    }

    // -------- Batch 4: compute --------
    case EPA_OP_GPU_DISPATCH: {
      uint32_t gx = EPA_READ_U32_LE(code, p + 0);
      uint32_t gy = EPA_READ_U32_LE(code, p + 4);
      uint32_t gz = EPA_READ_U32_LE(code, p + 8);
      p_glDispatchCompute((GLuint)gx, (GLuint)gy, (GLuint)gz);
      break;
    }

    // -------- Batch 4: fences --------
    case EPA_OP_GPU_FENCE_INSERT: {
      uint32_t fid = EPA_READ_U32_LE(code, p + 0);
      if (!res_id_ok(fid)) { snprintf(err, EPA_MAX_ERR, "GPU_FENCE_INSERT id out of range: %u", fid); return EPA_NF_EXEC_ERR; }
      if (impl->fences[fid]) { p_glDeleteSync(impl->fences[fid]); impl->fences[fid] = 0; }
      impl->fences[fid] = p_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      if (!impl->fences[fid]) { snprintf(err, EPA_MAX_ERR, "glFenceSync failed"); return EPA_NF_EXEC_ERR; }
      break;
    }

    case EPA_OP_GPU_FENCE_WAIT: {
      uint32_t fid = EPA_READ_U32_LE(code, p + 0);
      uint32_t timeout_ms = EPA_READ_U32_LE(code, p + 4);
      uint32_t flags = EPA_READ_U32_LE(code, p + 8);
      (void)flags;

      if (!res_id_ok(fid)) { snprintf(err, EPA_MAX_ERR, "GPU_FENCE_WAIT id out of range: %u", fid); return EPA_NF_EXEC_ERR; }
      GLsync s = impl->fences[fid];
      if (!s) { snprintf(err, EPA_MAX_ERR, "GPU_FENCE_WAIT missing fence id=%u", fid); return EPA_NF_EXEC_ERR; }

      // timeout in nanoseconds for GL; clamp
      GLuint64 timeout_ns = (timeout_ms == 0xFFFFFFFFu) ? 0xFFFFFFFFFFFFFFFFull : (GLuint64)timeout_ms * 1000000ull;
      GLenum rc = p_glClientWaitSync(s, GL_SYNC_FLUSH_COMMANDS_BIT, timeout_ns);

      // push a tiny status code: 0=timeout, 1=signaled, 2=failed
      int32_t v = 2;
      if (rc == GL_ALREADY_SIGNALED || rc == GL_CONDITION_SATISFIED) v = 1;
      else if (rc == GL_TIMEOUT_EXPIRED) v = 0;
      VM_DATA_PUSH(v);
      break;
    }

    case EPA_OP_GPU_FENCE_DELETE: {
      uint32_t fid = EPA_READ_U32_LE(code, p + 0);
      if (!res_id_ok(fid)) { snprintf(err, EPA_MAX_ERR, "GPU_FENCE_DELETE id out of range: %u", fid); return EPA_NF_EXEC_ERR; }
      if (impl->fences[fid]) { p_glDeleteSync(impl->fences[fid]); impl->fences[fid] = 0; }
      break;
    }

    // -------- Batch 4: queries (minimal: TIME_ELAPSED only) --------
    // kind: 0 = TIME_ELAPSED
    case EPA_OP_GPU_QUERY_BEGIN: {
      uint32_t qid  = EPA_READ_U32_LE(code, p + 0);
      uint32_t kind = EPA_READ_U32_LE(code, p + 4);
      if (!res_id_ok(qid)) { snprintf(err, EPA_MAX_ERR, "GPU_QUERY_BEGIN id out of range: %u", qid); return EPA_NF_EXEC_ERR; }

      if (!impl->queries[qid]) {
        GLuint q = 0;
        p_glGenQueries(1, &q);
        impl->queries[qid] = q;
      }

      if (kind != 0u) { snprintf(err, EPA_MAX_ERR, "GPU_QUERY_BEGIN unsupported kind=%u (only TIME_ELAPSED=0)", kind); return EPA_NF_EXEC_ERR; }
      p_glBeginQuery(GL_TIME_ELAPSED, impl->queries[qid]);
      break;
    }

    case EPA_OP_GPU_QUERY_END: {
      uint32_t kind = EPA_READ_U32_LE(code, p + 0);
      if (kind != 0u) { snprintf(err, EPA_MAX_ERR, "GPU_QUERY_END unsupported kind=%u (only TIME_ELAPSED=0)", kind); return EPA_NF_EXEC_ERR; }
      p_glEndQuery(GL_TIME_ELAPSED);
      break;
    }

    // 1) Compute program link
    case EPA_OP_GPU_PROGRAM_LINK_COMP: {
      uint32_t program_id = EPA_READ_U32_LE(code, p + 0);
      uint32_t cs_id      = EPA_READ_U32_LE(code, p + 4);

      if (!res_id_ok(program_id) || !res_id_ok(cs_id)) {
        snprintf(err, EPA_MAX_ERR, "GPU_PROGRAM_LINK_COMP id out of range prog=%u cs=%u", program_id, cs_id);
        return EPA_NF_EXEC_ERR;
      }

      GLuint cs = impl->shaders[cs_id];
      if (!cs) { snprintf(err, EPA_MAX_ERR, "GPU_PROGRAM_LINK_COMP missing shader cs=%u", cs_id); return EPA_NF_EXEC_ERR; }

      if (impl->programs[program_id]) {
        p_glDeleteProgram(impl->programs[program_id]);
        impl->programs[program_id] = 0;
      }

      GLuint pr = p_glCreateProgram();
      if (!pr) { snprintf(err, EPA_MAX_ERR, "glCreateProgram failed"); return EPA_NF_EXEC_ERR; }

      p_glAttachShader(pr, cs);
      p_glLinkProgram(pr);

      GLint ok = 0;
      p_glGetProgramiv(pr, GL_LINK_STATUS, &ok);
      if (!ok) {
        char logbuf[2048]; GLsizei n = 0;
        p_glGetProgramInfoLog(pr, (GLsizei)sizeof(logbuf), &n, logbuf);
        snprintf(err, EPA_MAX_ERR, "compute program link failed: %.*s", (int)n, logbuf);
        p_glDeleteProgram(pr);
        return EPA_NF_EXEC_ERR;
      }

      p_glDetachShader(pr, cs);
      impl->programs[program_id] = pr;
      break;
    }

    // 2) Query result u64 -> pushes low/high i32 words
    case EPA_OP_GPU_QUERY_RESULT_U64: {
      uint32_t qid = EPA_READ_U32_LE(code, p + 0);
      if (!res_id_ok(qid)) { snprintf(err, EPA_MAX_ERR, "GPU_QUERY_RESULT_U64 id out of range: %u", qid); return EPA_NF_EXEC_ERR; }
      GLuint q = impl->queries[qid];
      if (!q) { snprintf(err, EPA_MAX_ERR, "GPU_QUERY_RESULT_U64 missing query id=%u", qid); return EPA_NF_EXEC_ERR; }

      GLuint64 v = 0;
      p_glGetQueryObjectui64v(q, GL_QUERY_RESULT, &v);

      int32_t lo = (int32_t)(uint32_t)(v & 0xFFFFFFFFull);
      int32_t hi = (int32_t)(uint32_t)((v >> 32) & 0xFFFFFFFFull);
      VM_DATA_PUSH(lo);
      VM_DATA_PUSH(hi);
      break;
    }

    // 3) Debug label (variable)
    case EPA_OP_GPU_DEBUG_LABEL: {
      // header(12): u32 kind, u32 id, u32 byte_len + bytes
      uint32_t kind     = EPA_READ_U32_LE(code, p + 0);
      uint32_t id       = EPA_READ_U32_LE(code, p + 4);
      uint32_t byte_len = EPA_READ_U32_LE(code, p + 8);
      const char *txt   = (const char*)(code + (pc + 2 + 12));

      // kind mapping idea:
      // 0=program,1=shader,2=buffer,3=texture,4=vao,5=fbo
      // If you already have EPA_RK_* use those instead.
      GLenum obj = 0; GLuint name = 0;

      switch (kind) {
        case 0: obj = GL_PROGRAM;          name = res_id_ok(id) ? impl->programs[id] : 0; break;
        case 1: obj = GL_SHADER;           name = res_id_ok(id) ? impl->shaders[id]  : 0; break;
        case 2: obj = GL_BUFFER;           name = res_id_ok(id) ? impl->buffers[id]  : 0; break;
        case 3: obj = GL_TEXTURE;          name = res_id_ok(id) ? impl->textures[id] : 0; break;
        case 4: obj = GL_VERTEX_ARRAY;     name = res_id_ok(id) ? impl->vtx_layouts[id] : 0; break;
        case 5: obj = GL_FRAMEBUFFER;      name = res_id_ok(id) ? impl->rts[id] : 0; break;
        default:
          // allow no-op if unknown
          break;
      }

      if (obj && name) {
        // KHR_debug path; if unavailable this may be a link-time issue on some platforms
        p_glObjectLabel(obj, name, (GLsizei)byte_len, txt);
      }
      break;
    }

    // 4) Generate mipmaps
    case EPA_OP_GPU_TEX_GEN_MIPMAPS: {
      uint32_t tex_id = EPA_READ_U32_LE(code, p + 0);
      if (!res_id_ok(tex_id)) { snprintf(err, EPA_MAX_ERR, "GPU_TEX_GEN_MIPMAPS id out of range: %u", tex_id); return EPA_NF_EXEC_ERR; }
      GLuint th = impl->textures[tex_id];
      if (!th) { snprintf(err, EPA_MAX_ERR, "GPU_TEX_GEN_MIPMAPS missing tex id=%u", tex_id); return EPA_NF_EXEC_ERR; }

      GLenum tgt = impl->tex_targets[tex_id];
      if (!tgt) tgt = GL_TEXTURE_2D;
      glBindTexture(tgt, th);
      p_glGenerateMipmap(tgt);
      break;
    }

    // 5) Bind image texture (compute)
    case EPA_OP_GPU_BIND_IMAGE_TEX: {
      uint32_t unit   = EPA_READ_U32_LE(code, p + 0);
      uint32_t tex_id = EPA_READ_U32_LE(code, p + 4);
      uint32_t level  = EPA_READ_U32_LE(code, p + 8);
      uint32_t layer  = EPA_READ_U32_LE(code, p + 12);
      uint32_t access = EPA_READ_U32_LE(code, p + 16);
      uint32_t fmt    = EPA_READ_U32_LE(code, p + 20);

      if (!res_id_ok(tex_id)) { snprintf(err, EPA_MAX_ERR, "GPU_BIND_IMAGE_TEX tex id out of range: %u", tex_id); return EPA_NF_EXEC_ERR; }
      GLuint th = impl->textures[tex_id];
      if (!th) { snprintf(err, EPA_MAX_ERR, "GPU_BIND_IMAGE_TEX missing tex id=%u", tex_id); return EPA_NF_EXEC_ERR; }

      GLenum gl_access = map_image_access(access);
      GLenum gl_fmt    = map_image_format(fmt);
      if (!gl_access || !gl_fmt) {
        snprintf(err, EPA_MAX_ERR, "GPU_BIND_IMAGE_TEX invalid access=%u fmt=%u", access, fmt);
        return EPA_NF_EXEC_ERR;
      }

      GLboolean layered = (layer != 0) ? GL_TRUE : GL_FALSE;
      p_glBindImageTexture((GLuint)unit, th, (GLint)level, layered, (GLint)layer, gl_access, gl_fmt);
      break;
    }

    // 6) Uniform location lookup: pushes i32 loc
    case EPA_OP_GPU_UNIFORM_LOC: {
      uint32_t program_id = EPA_READ_U32_LE(code, p + 0);
      uint32_t name_len   = EPA_READ_U32_LE(code, p + 4);
      const char *name    = (const char*)(code + (pc + 2 + 8));

      if (!res_id_ok(program_id)) { snprintf(err, EPA_MAX_ERR, "GPU_UNIFORM_LOC prog id out of range: %u", program_id); return EPA_NF_EXEC_ERR; }
      GLuint pr = impl->programs[program_id];
      if (!pr) { snprintf(err, EPA_MAX_ERR, "GPU_UNIFORM_LOC missing program id=%u", program_id); return EPA_NF_EXEC_ERR; }

      GLint loc = gl_get_uniform_loc(pr, name, (size_t)name_len);
      VM_DATA_PUSH((int32_t)loc);
      break;
    }

    // 7) Uniform 1i
    case EPA_OP_GPU_UNIFORM_1I: {
      int32_t loc = (int32_t)EPA_READ_U32_LE(code, p + 0);
      int32_t v0  = (int32_t)EPA_READ_U32_LE(code, p + 4);
      p_glUniform1i((GLint)loc, (GLint)v0);
      break;
    }

    // 8) Uniform 1f
    case EPA_OP_GPU_UNIFORM_1F: {
      int32_t loc = (int32_t)EPA_READ_U32_LE(code, p + 0);
      float v0    = read_f32_le(code, p + 4);
      p_glUniform1f((GLint)loc, (GLfloat)v0);
      break;
    }

    // 9) Uniform 4f
    case EPA_OP_GPU_UNIFORM_4F: {
      int32_t loc = (int32_t)EPA_READ_U32_LE(code, p + 0);
      float x = read_f32_le(code, p + 4);
      float y = read_f32_le(code, p + 8);
      float z = read_f32_le(code, p + 12);
      float w0= read_f32_le(code, p + 16);
      p_glUniform4f((GLint)loc, x, y, z, w0);
      break;
    }

    // 10) Uniform mat4f (16 floats)
    case EPA_OP_GPU_UNIFORM_MAT4F: {
      int32_t loc = (int32_t)EPA_READ_U32_LE(code, p + 0);
      uint32_t transpose = EPA_READ_U32_LE(code, p + 4);
      const float *m = (const float*)(code + (p + 8));
      p_glUniformMatrix4fv((GLint)loc, 1, transpose ? GL_TRUE : GL_FALSE, m);
      break;
    }

    default:
      snprintf(err, EPA_MAX_ERR, "OpenGL nonflow: opcode %s not implemented", def->name);
      return EPA_NF_EXEC_ERR;
  }

  // commit
  vm->stack.sp = sp; // (or vm->data_sp)
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_NF_EXEC_OK;
}

// vtable
static const EpaNonFlowBackendVTable opengl_nf_vt = {
  .exec_one = opengl_exec_one
};

// factory
EpaNonFlowBackend epa_opengl_nonflow_backend(OpenglImpl *impl) {
  EpaNonFlowBackend b;
  memset(&b, 0, sizeof(b));
  b.vt = &opengl_nf_vt;
  b.impl = impl;
  return b;
}
