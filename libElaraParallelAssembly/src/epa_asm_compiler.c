#include "epa_asm_compiler.h"

#include "opcodes/epa_opcode_values.h"
// Opcode metadata table (EpaOpcodeDef/EPA_OPCODE_TABLE)
#include "opcodes/opcode_def.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

enum {
  DB_STR   = 1,
  DB_I32   = 2,
  DB_U32   = 3,
  DB_I64   = 4,
  DB_U64   = 5,
  DB_F32   = 6,
  DB_F64   = 7,
  DB_BYTES = 8
};


typedef struct {
  uint8_t *buf;
  size_t len;
  size_t cap;
} Out;

typedef struct { char name[64]; size_t pc; } Label;
typedef struct { char name[64]; size_t imm_off; size_t next_pc; int line_no; } Fixup;

typedef struct {
  Label  *labels;
  size_t  nlabels, cap_labels;

  Fixup  *fixups;
  size_t  nfix, cap_fix;
} AsmCtx;

static void strip_comment(char *line) {
    char *p = strstr(line, "//");
    if (p) {
        *p = '\0';
    }
    p = strstr(line, ";");
    if (p) {
    	*p = '\0';
    }
}

static int ctx_add_label(AsmCtx *ctx, const char *name, size_t pc, int line_no, char err[EPA_MAX_ERR]) {
  if (!name || !*name) {
    snprintf(err, EPA_MAX_ERR, "line %d: empty label", line_no);
    return 0;
  }

  // disallow duplicate labels
  for (size_t i = 0; i < ctx->nlabels; i++) {
    if (strcmp(ctx->labels[i].name, name) == 0) {
      snprintf(err, EPA_MAX_ERR, "line %d: duplicate label '%s'", line_no, name);
      return 0;
    }
  }

  if (ctx->nlabels + 1 > ctx->cap_labels) {
    size_t ncap = ctx->cap_labels ? ctx->cap_labels * 2 : 32;
    Label *nl = (Label *)realloc(ctx->labels, ncap * sizeof(Label));
    if (!nl) {
      snprintf(err, EPA_MAX_ERR, "line %d: OOM adding label", line_no);
      return 0;
    }
    ctx->labels = nl;
    ctx->cap_labels = ncap;
  }

  Label *L = &ctx->labels[ctx->nlabels++];
  memset(L, 0, sizeof(*L));
  strncpy(L->name, name, sizeof(L->name) - 1);
  L->pc = pc;
  return 1;
}

static int ctx_add_fixup(AsmCtx *ctx, const char *name, size_t imm_off, size_t next_pc, int line_no, char err[EPA_MAX_ERR]) {
  if (!name || !*name) {
    snprintf(err, EPA_MAX_ERR, "line %d: empty jump target", line_no);
    return 0;
  }

  if (ctx->nfix + 1 > ctx->cap_fix) {
    size_t ncap = ctx->cap_fix ? ctx->cap_fix * 2 : 32;
    Fixup *nf = (Fixup *)realloc(ctx->fixups, ncap * sizeof(Fixup));
    if (!nf) {
      snprintf(err, EPA_MAX_ERR, "line %d: OOM adding fixup", line_no);
      return 0;
    }
    ctx->fixups = nf;
    ctx->cap_fix = ncap;
  }

  Fixup *F = &ctx->fixups[ctx->nfix++];
  memset(F, 0, sizeof(*F));
  strncpy(F->name, name, sizeof(F->name) - 1);
  F->imm_off = imm_off;
  F->next_pc = next_pc;
  F->line_no = line_no;
  return 1;
}

static int ctx_find_label(const AsmCtx *ctx, const char *name, size_t *out_pc) {
  for (size_t i = 0; i < ctx->nlabels; i++) {
    if (strcmp(ctx->labels[i].name, name) == 0) {
      *out_pc = ctx->labels[i].pc;
      return 1;
    }
  }
  return 0;
}

static void ctx_free(AsmCtx *ctx) {
  free(ctx->labels);
  free(ctx->fixups);
  memset(ctx, 0, sizeof(*ctx));
}


static int out_reserve(Out *o, size_t add) {
  if (o->len + add <= o->cap) return 1;
  size_t ncap = o->cap ? o->cap : 256;
  while (ncap < o->len + add) ncap *= 2;
  uint8_t *nb = (uint8_t *)realloc(o->buf, ncap);
  if (!nb) return 0;
  o->buf = nb;
  o->cap = ncap;
  return 1;
}

static int out_u8(Out *o, uint8_t v) {
  if (!out_reserve(o, 1)) return 0;
  o->buf[o->len++] = v;
  return 1;
}

static int out_u16_le(Out *o, uint16_t v) {
  if (!out_reserve(o, 2)) return 0;
  o->buf[o->len++] = (uint8_t)(v & 0xFFu);
  o->buf[o->len++] = (uint8_t)((v >> 8) & 0xFFu);
  return 1;
}

static int out_u32_le(Out *o, uint32_t v) {
  if (!out_reserve(o, 4)) return 0;
  o->buf[o->len++] = (uint8_t)(v & 0xFFu);
  o->buf[o->len++] = (uint8_t)((v >> 8) & 0xFFu);
  o->buf[o->len++] = (uint8_t)((v >> 16) & 0xFFu);
  o->buf[o->len++] = (uint8_t)((v >> 24) & 0xFFu);
  return 1;
}

static int out_f32_le(Out *o, float f) {
  uint32_t u = 0;
  memcpy(&u, &f, 4);
  return out_u32_le(o, u);
}

static void trim_comment(char *s) {
  // remove // comments
  char *p = strstr(s, "//");
  if (p) *p = 0;
  // remove # comments
  p = strchr(s, '#');
  if (p) *p = 0;
}

static char *ltrim(char *s) {
  while (*s && isspace((unsigned char)*s)) s++;
  return s;
}

static void rtrim(char *s) {
  size_t n = strlen(s);
  while (n > 0 && isspace((unsigned char)s[n-1])) s[--n] = 0;
}

static int ieq(const char *a, const char *b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    a++; b++;
  }
  return *a == 0 && *b == 0;
}

static int parse_u32(const char *t, uint32_t *out) {
  if (!t || !*t) return 0;
  char *end = NULL;
  unsigned long v = strtoul(t, &end, 0);
  if (!end || *end != 0) return 0;
  *out = (uint32_t)v;
  return 1;
}

// Register token: R0..R3 (case-insensitive).
static int parse_reg_tok(const char *t, uint32_t *out_idx) {
  if (!t || !out_idx || !*t) return 0;
  if (!(t[0] == 'R' || t[0] == 'r')) return 0;
  if (!t[1]) return 0;
  char *end = NULL;
  long v = strtol(t + 1, &end, 10);
  if (!end || *end != 0) return 0;
  if (v < 0 || v > 255) return 0;
  *out_idx = (uint32_t)v;
  return 1;
}

static int parse_i32(const char *t, int32_t *out) {
  if (!t || !*t) return 0;
  char *end = NULL;
  long v = strtol(t, &end, 0);
  if (!end || *end != 0) return 0;
  *out = (int32_t)v;
  return 1;
}

static int parse_f32(const char *t, float *out) {
  if (!t || !*t) return 0;
  char *end = NULL;
  float v = strtof(t, &end);
  if (!end || *end != 0) return 0;
  *out = v;
  return 1;
}

static int parse_mode_tok(const char *t, uint8_t *mode) {
  if (ieq(t, "opengl") || ieq(t, "gl")) { *mode = EPA_MODE_OPENGL; return 1; }
  if (ieq(t, "cuda") || ieq(t, "gpu")) { *mode = EPA_MODE_CUDA; return 1; }
  uint32_t v;
  if (parse_u32(t, &v) && (v == 0u || v == 1u)) { *mode = (uint8_t)v; return 1; }
  return 0;
}

typedef struct {
  uint32_t id;
  uint8_t *bytes;
  uint16_t len;
  int line_no;
} SStr;

typedef struct {
  SStr *v;
  size_t n, cap;
} SStrVec;

static void sstrvec_free(SStrVec *sv) {
  if (!sv) return;
  for (size_t i = 0; i < sv->n; i++) free(sv->v[i].bytes);
  free(sv->v);
  sv->v = NULL; sv->n = sv->cap = 0;
}

static int sstrvec_add(SStrVec *sv, uint32_t id, uint8_t *bytes, uint16_t len, int line_no, char err[EPA_MAX_ERR]) {
  // Disallow duplicate ids (simple + predictable)
  for (size_t i = 0; i < sv->n; i++) {
    if (sv->v[i].id == id) {
      snprintf(err, EPA_MAX_ERR, "line %d: duplicate .SSTR id %u (previous at line %d)", line_no, id, sv->v[i].line_no);
      return 0;
    }
  }
  if (sv->n + 1 > sv->cap) {
    size_t ncap = sv->cap ? sv->cap * 2 : 16;
    SStr *nv = (SStr*)realloc(sv->v, ncap * sizeof(SStr));
    if (!nv) { snprintf(err, EPA_MAX_ERR, "line %d: OOM adding .SSTR", line_no); return 0; }
    sv->v = nv; sv->cap = ncap;
  }
  sv->v[sv->n++] = (SStr){ .id = id, .bytes = bytes, .len = len, .line_no = line_no };
  return 1;
}

static int is_const_directive(const char *s) {
  if (!s) return 0;

  // skip leading whitespace
  while (*s == ' ' || *s == '\t') s++;

  if (*s != '.') return 0;

  // legacy alias
  if (strncmp(s, ".SSTR", 5) == 0)
    return 1;

  // canonical const directives
  if (strncmp(s, ".CONST_", 7) == 0)
    return 1;

  return 0;
}


static int is_sstr_directive(const char *s) {
  // Accept ".SSTR" or "SSTR" (case-insensitive)
  if (!s) return 0;
  while (*s && isspace((unsigned char)*s)) s++;
  if (*s == '.') s++;
  return (tolower((unsigned char)s[0])=='s' &&
          tolower((unsigned char)s[1])=='s' &&
          tolower((unsigned char)s[2])=='t' &&
          tolower((unsigned char)s[3])=='r' &&
          (s[4]==0 || isspace((unsigned char)s[4])));
}

static int is_const_str_directive(const char *s) {
  if (!s) return 0;
  while (*s && isspace((unsigned char)*s)) s++;
  if (*s == '.') s++;
  // CONST_STR
  return (tolower((unsigned char)s[0])=='c' &&
          tolower((unsigned char)s[1])=='o' &&
          tolower((unsigned char)s[2])=='n' &&
          tolower((unsigned char)s[3])=='s' &&
          tolower((unsigned char)s[4])=='t' &&
          s[5]=='_' &&
          tolower((unsigned char)s[6])=='s' &&
          tolower((unsigned char)s[7])=='t' &&
          tolower((unsigned char)s[8])=='r' &&
          (s[9]==0 || isspace((unsigned char)s[9])));
}

static int parse_string_lit(const char *p, uint8_t **out_bytes, uint16_t *out_len, char err[EPA_MAX_ERR], int line_no) {
  // p should point at the first quote '"'
  if (*p != '"') { snprintf(err, EPA_MAX_ERR, "line %d: expected string literal", line_no); return 0; }
  p++; // skip opening quote

  size_t cap = 64, len = 0;
  uint8_t *buf = (uint8_t*)malloc(cap);
  if (!buf) { snprintf(err, EPA_MAX_ERR, "line %d: OOM parsing string", line_no); return 0; }

  while (*p && *p != '"') {
    unsigned char c = (unsigned char)*p++;
    if (c == '\\') {
      unsigned char e = (unsigned char)*p++;
      if (!e) { free(buf); snprintf(err, EPA_MAX_ERR, "line %d: unterminated escape", line_no); return 0; }
      switch (e) {
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case '\\': c = '\\'; break;
        case '"': c = '"'; break;
        default:
          // keep it strict so bugs show up early
          free(buf);
          snprintf(err, EPA_MAX_ERR, "line %d: unsupported escape \\%c", line_no, (char)e);
          return 0;
      }
    }
    if (len + 1 > cap) {
      cap *= 2;
      uint8_t *nb = (uint8_t*)realloc(buf, cap);
      if (!nb) { free(buf); snprintf(err, EPA_MAX_ERR, "line %d: OOM parsing string", line_no); return 0; }
      buf = nb;
    }
    buf[len++] = (uint8_t)c;
    if (len > 65535u) { free(buf); snprintf(err, EPA_MAX_ERR, "line %d: .SSTR too long (>65535)", line_no); return 0; }
  }

  if (*p != '"') { free(buf); snprintf(err, EPA_MAX_ERR, "line %d: unterminated string literal", line_no); return 0; }

  *out_bytes = buf;
  *out_len = (uint16_t)len;
  return 1;
}

static int parse_sstr_directive_line(char *line, uint32_t *out_id, uint8_t **out_bytes, uint16_t *out_len,
                                     int line_no, char err[EPA_MAX_ERR]) {
  // Accept:
  //   .SSTR <id> "...."
  //   .CONST_STR <id> "...."
  // Note: line already comment-stripped.

  char *s = ltrim(line);
  rtrim(s);

  int is_sstr = is_sstr_directive(s);
  int is_cstr = is_const_str_directive(s);
  if (!is_sstr && !is_cstr) return 0; // not a string-const line we handle

  // advance past directive token
  if (*s == '.') s++;

  if (is_sstr) {
    s += 4; // "SSTR"
  } else {
    s += 9; // "CONST_STR"
  }

  while (*s && isspace((unsigned char)*s)) s++;

  // parse id token
  char idtok[64] = {0};
  int k = 0;
  while (*s && !isspace((unsigned char)*s) && k < (int)sizeof(idtok)-1) idtok[k++] = *s++;
  idtok[k] = 0;

  if (k == 0) {
    snprintf(err, EPA_MAX_ERR, "line %d: %s missing id", line_no, is_sstr ? ".SSTR" : ".CONST_STR");
    return -1;
  }

  uint32_t id = 0;
  if (!parse_u32(idtok, &id)) {
    snprintf(err, EPA_MAX_ERR, "line %d: invalid %s id '%s'", line_no, is_sstr ? ".SSTR" : ".CONST_STR", idtok);
    return -1;
  }

  while (*s && isspace((unsigned char)*s)) s++;
  if (*s != '"') {
    snprintf(err, EPA_MAX_ERR, "line %d: %s expected string literal", line_no, is_sstr ? ".SSTR" : ".CONST_STR");
    return -1;
  }

  uint8_t *bytes = NULL;
  uint16_t slen = 0;
  if (!parse_string_lit(s, &bytes, &slen, err, line_no)) return -1;

  *out_id = id;
  *out_bytes = bytes;
  *out_len = slen;
  return 1;
}

// ---------------------------
// Descriptor-driven emitter
// ---------------------------

typedef enum {
  AK_NONE = 0,
  AK_U8,
  AK_U16,
  AK_U32,
  AK_I32,
  AK_F32,
  AK_MODE,
  AK_REGU8,         // accepts Rn only
  AK_REGU8_OR_U8,   // accepts Rn or numeric u8
  AK_REG4_OR_U8,    // accepts Rn or numeric u8, but must be 0..3 (current register file)
} ArgKind;

typedef struct AsmInsnDesc AsmInsnDesc;
typedef int (*AsmEmitHelper)(AsmCtx *ctx, Out *o, int line_no, int nt, char *tok[16], const AsmInsnDesc *d, char err[EPA_MAX_ERR]);

struct AsmInsnDesc {
  const char     *mnemonic;
  uint16_t        opcode;
  uint8_t         min_args;     // excluding mnemonic
  uint8_t         max_args;     // excluding mnemonic
  uint8_t         nkinds;
  uint8_t         kinds[8];
  AsmEmitHelper   helper;       // NULL => generic
  const char     *usage;        // for error messages
};

static int parse_u8_tok(const char *t, uint8_t *out) {
  uint32_t v = 0;
  if (!parse_u32(t, &v) || v > 255u) return 0;
  *out = (uint8_t)v;
  return 1;
}

static int parse_u16_tok(const char *t, uint16_t *out) {
  uint32_t v = 0;
  if (!parse_u32(t, &v) || v > 65535u) return 0;
  *out = (uint16_t)v;
  return 1;
}

static int parse_reg_u8_tok(const char *t, uint8_t *out_idx) {
  uint32_t idx = 0;
  if (!parse_reg_tok(t, &idx) || idx > 255u) return 0;
  *out_idx = (uint8_t)idx;
  return 1;
}

static int parse_reg_or_u8_tok(const char *t, uint8_t *out) {
  uint32_t idx = 0;
  if (parse_reg_tok(t, &idx)) {
    if (idx > 255u) return 0;
    *out = (uint8_t)idx;
    return 1;
  }
  return parse_u8_tok(t, out);
}

static int parse_reg4_or_u8_tok(const char *t, uint8_t *out) {
  uint8_t v = 0;
  if (!parse_reg_or_u8_tok(t, &v)) return 0;
  if (v >= 4u) return 0;
  *out = v;
  return 1;
}

static int emit_generic(AsmCtx *ctx, Out *o, int line_no, int nt, char *tok[16], const AsmInsnDesc *d, char err[EPA_MAX_ERR]) {
  (void)ctx;
  int nargs = nt - 1;
  if (nargs < (int)d->min_args || nargs > (int)d->max_args) {
    snprintf(err, EPA_MAX_ERR, "line %d: %s", line_no, d->usage ? d->usage : "wrong number of operands");
    return 0;
  }
  if (!out_u16_le(o, d->opcode)) return 0;

  // Fixed packing driven by kinds[]
  for (uint8_t i = 0; i < d->nkinds; i++) {
    const char *arg = tok[1 + i];
    switch ((ArgKind)d->kinds[i]) {
      case AK_NONE: break;
      case AK_U8: {
        uint8_t v = 0;
        if (!parse_u8_tok(arg, &v)) { snprintf(err, EPA_MAX_ERR, "line %d: invalid u8 '%s'", line_no, arg); return 0; }
        if (!out_u8(o, v)) return 0;
      } break;
      case AK_U16: {
        uint16_t v = 0;
        if (!parse_u16_tok(arg, &v)) { snprintf(err, EPA_MAX_ERR, "line %d: invalid u16 '%s'", line_no, arg); return 0; }
        if (!out_u16_le(o, v)) return 0;
      } break;
      case AK_U32: {
        uint32_t v = 0;
        if (!parse_u32(arg, &v)) { snprintf(err, EPA_MAX_ERR, "line %d: invalid u32 '%s'", line_no, arg); return 0; }
        if (!out_u32_le(o, v)) return 0;
      } break;
      case AK_I32: {
        int32_t v = 0;
        if (!parse_i32(arg, &v)) { snprintf(err, EPA_MAX_ERR, "line %d: invalid i32 '%s'", line_no, arg); return 0; }
        if (!out_u32_le(o, (uint32_t)v)) return 0;
      } break;
      case AK_F32: {
        float f = 0.0f;
        if (!parse_f32(arg, &f)) { snprintf(err, EPA_MAX_ERR, "line %d: invalid f32 '%s'", line_no, arg); return 0; }
        if (!out_f32_le(o, f)) return 0;
      } break;
      case AK_MODE: {
        uint8_t m = 0;
        if (!parse_mode_tok(arg, &m)) { snprintf(err, EPA_MAX_ERR, "line %d: invalid mode '%s'", line_no, arg); return 0; }
        if (!out_u8(o, m)) return 0;
      } break;
      case AK_REGU8: {
        uint8_t r = 0;
        if (!parse_reg_u8_tok(arg, &r)) { snprintf(err, EPA_MAX_ERR, "line %d: invalid register '%s'", line_no, arg); return 0; }
        if (!out_u8(o, r)) return 0;
      } break;
      case AK_REGU8_OR_U8: {
        uint8_t r = 0;
        if (!parse_reg_or_u8_tok(arg, &r)) { snprintf(err, EPA_MAX_ERR, "line %d: invalid register/u8 '%s'", line_no, arg); return 0; }
        if (!out_u8(o, r)) return 0;
      } break;
      case AK_REG4_OR_U8: {
        uint8_t r = 0;
        if (!parse_reg4_or_u8_tok(arg, &r)) { snprintf(err, EPA_MAX_ERR, "line %d: invalid reg (0..3) '%s'", line_no, arg); return 0; }
        if (!out_u8(o, r)) return 0;
      } break;
      default:
        snprintf(err, EPA_MAX_ERR, "line %d: internal: unknown ArgKind", line_no);
        return 0;
    }
  }
  return 1;
}

static int helper_emit_jump_rel32(AsmCtx *ctx, Out *o, int line_no, int nt, char *tok[16], const AsmInsnDesc *d, char err[EPA_MAX_ERR]) {
  if (nt != 2) {
    snprintf(err, EPA_MAX_ERR, "line %d: %s", line_no, d->usage ? d->usage : "JMP/JZ/JNZ requires 1 operand");
    return 0;
  }

  if (!out_u16_le(o, d->opcode)) { snprintf(err, EPA_MAX_ERR, "line %d: OOM", line_no); return 0; }

  // Reserve 4 bytes for rel32 (patch later if label)
  size_t imm_off = o->len;
  if (!out_u32_le(o, 0)) { snprintf(err, EPA_MAX_ERR, "line %d: OOM", line_no); return 0; }
  size_t next_pc = o->len;

  const char *t = tok[1];
  int numeric = 0;
  if (t[0] == '+' || t[0] == '-' || isdigit((unsigned char)t[0])) numeric = 1;

  if (numeric) {
    char *end = NULL;
    long rel = strtol(t, &end, 0);
    if (!end || *end != 0) {
      snprintf(err, EPA_MAX_ERR, "line %d: invalid rel32 '%s'", line_no, t);
      return 0;
    }
    int32_t r32 = (int32_t)rel;
    o->buf[imm_off + 0] = (uint8_t)(r32 & 0xFF);
    o->buf[imm_off + 1] = (uint8_t)((r32 >> 8) & 0xFF);
    o->buf[imm_off + 2] = (uint8_t)((r32 >> 16) & 0xFF);
    o->buf[imm_off + 3] = (uint8_t)((r32 >> 24) & 0xFF);
  } else {
    if (!ctx_add_fixup(ctx, t, imm_off, next_pc, line_no, err)) return 0;
  }
  return 1;
}

static int helper_emit_yield(AsmCtx *ctx, Out *o, int line_no, int nt, char *tok[16], const AsmInsnDesc *d, char err[EPA_MAX_ERR]) {
  (void)ctx;
  (void)d;
  // YIELD [soft|hard|0|1]  (default: soft)
  if (nt != 1 && nt != 2) {
    snprintf(err, EPA_MAX_ERR, "line %d: YIELD takes 0 or 1 param (soft|hard|0|1)", line_no);
    return 0;
  }

  uint8_t pol = EPA_YIELD_SOFT;
  if (nt == 2) {
    if (ieq(tok[1], "soft")) pol = EPA_YIELD_SOFT;
    else if (ieq(tok[1], "hard")) pol = EPA_YIELD_HARD;
    else {
      uint32_t v = 0;
      if (!parse_u32(tok[1], &v) || (v != 0u && v != 1u)) {
        snprintf(err, EPA_MAX_ERR, "line %d: invalid YIELD policy '%s'", line_no, tok[1]);
        return 0;
      }
      pol = (uint8_t)v;
    }
  }

  if (!out_u16_le(o, EPA_OP_YIELD)) return 0;
  if (!out_u8(o, pol)) return 0;
  return 1;
}

static int helper_emit_push(AsmCtx *ctx, Out *o, int line_no, int nt, char *tok[16], const AsmInsnDesc *d, char err[EPA_MAX_ERR]) {
  (void)ctx;
  (void)d;
  if (nt != 2) {
    snprintf(err, EPA_MAX_ERR, "line %d: PUSH requires 1 param (Rn or i32)", line_no);
    return 0;
  }

  uint32_t ridx = 0;
  if (parse_reg_tok(tok[1], &ridx)) {
    if (ridx >= 4u) { snprintf(err, EPA_MAX_ERR, "line %d: PUSH reg out of range '%s'", line_no, tok[1]); return 0; }
    if (!out_u16_le(o, EPA_OP_PUSH_R)) return 0;
    if (!out_u8(o, (uint8_t)ridx)) return 0;
    return 1;
  }

  int32_t v = 0;
  if (!parse_i32(tok[1], &v)) { snprintf(err, EPA_MAX_ERR, "line %d: invalid i32 '%s'", line_no, tok[1]); return 0; }
  if (!out_u16_le(o, EPA_OP_PUSH_I32)) return 0;
  if (!out_u32_le(o, (uint32_t)v)) return 0;
  return 1;
}

static const AsmInsnDesc *find_desc(const char *mn) {
  // NOTE: keep this list minimal; most opcodes should be descriptor-only.
  static const AsmInsnDesc kDesc[] = {
    // Core / mode
    {"NOOP",       EPA_OP_NOOP,                  0,0, 0,{0}, NULL, "NOOP takes no params"},
    {"END",        EPA_OP_END,                   0,0, 0,{0}, NULL, "END takes no params"},
    {"SET_MODE",   EPA_OP_SET_MODE,              1,1, 1,{AK_MODE}, NULL, "SET_MODE <opengl|cuda|0|1>"},

    // Render-ish
    {"CLEAR",      EPA_OP_CLEAR_RGBA_DEPTH_F32,  5,5, 5,{AK_F32,AK_F32,AK_F32,AK_F32,AK_F32}, NULL, "CLEAR <r> <g> <b> <a> <depth>"},
    {"VIEWPORT",   EPA_OP_VIEWPORT_I32,          4,4, 4,{AK_I32,AK_I32,AK_I32,AK_I32}, NULL, "VIEWPORT <x> <y> <w> <h>"},
    {"DRAW",       EPA_OP_DRAW,                  3,3, 3,{AK_U32,AK_U32,AK_U32}, NULL, "DRAW <prim> <first> <count>"},

    // Flow
    {"YIELD",      EPA_OP_YIELD,                 0,1, 0,{0}, helper_emit_yield, "YIELD [soft|hard|0|1]"},

    // Jumps (rel32 patched to labels)
    {"JMP",        EPA_OP_JMP_REL32,             1,1, 0,{0}, helper_emit_jump_rel32, "JMP <label|rel32>"},
    {"JZ",         EPA_OP_JZ_REL32,              1,1, 0,{0}, helper_emit_jump_rel32,  "JZ <label|rel32>"},
    {"JNZ",        EPA_OP_JNZ_REL32,             1,1, 0,{0}, helper_emit_jump_rel32, "JNZ <label|rel32>"},
    {"JLZ",        EPA_OP_JLZ_REL32,             1,1, 0,{0}, helper_emit_jump_rel32, "JLZ <label|rel32>"},
    {"JGZ",        EPA_OP_JGZ_REL32,             1,1, 0,{0}, helper_emit_jump_rel32, "JGZ <label|rel32>"},

    // Stack + locals
    {"PUSH",       0,                            1,1, 0,{0}, helper_emit_push, "PUSH <Rn|i32>"},
    {"POP",        EPA_OP_POP_R,                 1,1, 1,{AK_REG4_OR_U8}, NULL, "POP <Rn|u8>"},
    {"STORE_L",    EPA_OP_STORE_L,               1,1, 1,{AK_U8}, NULL, "STORE_L <u8_idx>"},
    {"LOAD_L",     EPA_OP_LOAD_L,                1,1, 1,{AK_U8}, NULL, "LOAD_L <u8_idx>"},

    // ALU / compare
    {"ADD_I32",    EPA_OP_ADD_I32,               0,0, 0,{0}, NULL, "ADD_I32 takes no params"},
    {"SUB_I32",    EPA_OP_SUB_I32,               0,0, 0,{0}, NULL, "SUB_I32 takes no params"},
    {"MUL_I32",    EPA_OP_MUL_I32,               0,0, 0,{0}, NULL, "MUL_I32 takes no params"},
    {"EQ_I32",     EPA_OP_EQ_I32,                0,0, 0,{0}, NULL, "EQ_I32 takes no params"},
    {"LT_I32",     EPA_OP_LT_I32,                0,0, 0,{0}, NULL, "LT_I32 takes no params"},
    {"NE_I32",     EPA_OP_NE_I32,                0,0, 0,{0}, NULL, "NE_I32 takes no params"},
    {"LE_I32",     EPA_OP_LE_I32,                0,0, 0,{0}, NULL, "LE_I32 takes no params"},
    {"GT_I32",     EPA_OP_GT_I32,                0,0, 0,{0}, NULL, "GT_I32 takes no params"},
    {"GE_I32",     EPA_OP_GE_I32,                0,0, 0,{0}, NULL, "GE_I32 takes no params"},
    {"DIV_I32",    EPA_OP_DIV_I32,               0,0, 0,{0}, NULL, "DIV_I32 takes no params"},
    {"CMP",        EPA_OP_CMP,                   0,0, 0,{0}, NULL, "CMP takes no params"},
    {"CMPZ",       EPA_OP_CMPZ,                  0,0, 0,{0}, NULL, "CMPZ takes no params"},

    // Register ops
    {"MV",         EPA_OP_MV,                    2,2, 2,{AK_REG4_OR_U8,AK_REG4_OR_U8}, NULL, "MV <dst_reg> <src_reg>"},
    {"SET_R",      EPA_OP_SET_R,                 2,2, 2,{AK_REG4_OR_U8,AK_I32}, NULL, "SET_R <reg> <i32>"},
    {"INC",        EPA_OP_INC,                   1,1, 1,{AK_REG4_OR_U8}, NULL, "INC <reg>"},
    {"DEC",        EPA_OP_DEC,                   1,1, 1,{AK_REG4_OR_U8}, NULL, "DEC <reg>"},

	{ "RLB_MOV1", EPA_OP_RLB_MOV1, 2,  2, 2, { AK_REG4_OR_U8, AK_REG4_OR_U8 }, NULL, "RLB_MOV1 <reg> <lb_reg>  ; store low byte of reg into local byte heap at offset in lb_reg" },
	{ "LBR_MOV1", EPA_OP_LBR_MOV1, 2, 2, 2, { AK_REG4_OR_U8, AK_REG4_OR_U8 }, NULL, "LBR_MOV1 <reg> <lb_reg>  ; load byte from local byte heap at offset in lb_reg into reg" },
	{ "RLB_MOV4", EPA_OP_RLB_MOV4, 2,  2, 2, { AK_REG4_OR_U8, AK_REG4_OR_U8 }, NULL, "RLB_MOV4 <reg> <lb_reg>  ; store 4 bytes of reg (LE) into local byte heap at offset in lb_reg" },
	{ "LBR_MOV4", EPA_OP_LBR_MOV4, 2, 2, 2, { AK_REG4_OR_U8, AK_REG4_OR_U8 }, NULL, "LBR_MOV4 <reg> <lb_reg>  ; load 4 bytes from local byte heap at offset in lb_reg into reg (LE)" },

	{"FUNC_START", EPA_OP_FUNC_START, 2,2,2, {AK_U32, AK_U16}, NULL, "FUNC_START <func_id:u32> <frame_words:u16>"},
	{"FUNC_END",   EPA_OP_FUNC_END,   0,0,0, {0},             NULL, "FUNC_END"},
	{"CALL",       EPA_OP_CALL,       1,1,1, {AK_U32},        NULL, "CALL <func_id:u32>"},
	{"RET",        EPA_OP_RET,        0,0,0, {0},             NULL, "RET"},

    // Entry scheduler
    {"ENTRY_START",EPA_OP_ENTRY_START,           4,4, 4,{AK_U32, AK_U16, AK_U16, AK_U32}, NULL, "ENTRY_START <id> <in_words:u16> <out_words:u16> <signal_mail_box_size:u32>"},
    {"ENTRY_END",  EPA_OP_ENTRY_END,             0,0, 0,{0}, NULL, "ENTRY_END takes no params"},
    {"ENTRY_EXEC", EPA_OP_ENTRY_EXEC,            1,1, 1,{AK_U8}, NULL, "ENTRY_EXEC <worker_id:u8>"},
    {"ENTRY_HALT", EPA_OP_ENTRY_HALT,            1,1, 1,{AK_U8}, NULL, "ENTRY_HALT <worker_id:u8>"},
    {"DYNAMIC_POOL",EPA_OP_DYNAMIC_POOL,         5,5, 5,{AK_U32, AK_U32, AK_U32, AK_U32, AK_U32}, NULL, "DYNAMIC_POOL <pool_id:u32> <element_size:u32> <min_free:u32> <max_free:u32> <grow_by:u32>"},
    {"SYNC",       EPA_OP_SYNC,                  0,0, 0,{0}, NULL, "SYNC takes no params"},
    {"WAIT_ON_SYNC",EPA_OP_WAIT_ON_SYNC,         0,0, 0,{0}, NULL, "WAIT_ON_SYNC takes no params"},

    // Data / wakeups
    {"WAIT_FOR_DATA",EPA_OP_WAIT_FOR_DATA,       0,0, 0,{0}, NULL, "WAIT_FOR_DATA takes no params"},
    {"DATA_READY", EPA_OP_DATA_READY,            1,1, 1,{AK_U8}, NULL, "DATA_READY <worker_id:u8>"},
	{"WAIT_FOR_AT", EPA_OP_WAIT_FOR_AT,          0,0, 0,{0}, NULL, "WAIT_FOR_AT takes no params"},

	// Ring-buffer transfers
	{ "WORKER_TRX",        EPA_OP_WORKER_TRX,        3, 3, 3, 	{ AK_U32, AK_U32, AK_U16 }, NULL, "" }, // src_laddr, dst_laddr, len
	{ "KERNEL_TRX_IN_L",   EPA_OP_KERNEL_TRX_IN_L,   3, 3, 3,  	{ AK_U8,  AK_U32, AK_U16 }, NULL, "" }, // worker_id, laddr, len
	{ "KERNEL_TRX_OUT_L",  EPA_OP_KERNEL_TRX_OUT_L,  3, 3, 3,  	{ AK_U8,  AK_U32, AK_U16 }, NULL, "" }, // worker_id, laddr, len
	{ "WORKER_TRX_IN_L",   EPA_OP_WORKER_TRX_IN_L,   2, 2, 2, 	{ AK_U32, AK_U16 }, NULL, "" },         // laddr, len
	{ "WORKER_TRX_OUT_L",  EPA_OP_WORKER_TRX_OUT_L,  2, 2, 2, 	{ AK_U32, AK_U16 }, NULL, "" },         // laddr, len
	{ "WORKER_TRX_IN_R",   EPA_OP_WORKER_TRX_IN_R,   1, 1, 1, 	{ AK_U8 }, NULL, "" },         // laddr, len
	{ "WORKER_TRX_OUT_R",  EPA_OP_WORKER_TRX_OUT_R,  1, 1, 1, 	{ AK_U8 }, NULL, "" },         // laddr, len
	{ "KERNEL_GHS_IN_R",   EPA_OP_KERNEL_GHS_IN_R,   1, 1, 1,   { AK_U32 }, NULL, "KERNEL_GHS_IN_R <wid_local:u32> (returns r0=idx, r1=gen, r2=ok, r3=0)" },

    // ---------------------------
    // Global Handle Space (GHS) - register convention, no immediates
    // ---------------------------
    {"G_ALLOC",    EPA_OP_G_ALLOC,              0,0, 0,{0}, NULL, "G_ALLOC (uses r0=type, r1=size; returns r0=idx, r1=gen)"},
    {"G_FREE",     EPA_OP_G_FREE,               0,0, 0,{0}, NULL, "G_FREE (uses r0=idx, r1=gen)"},
    {"G_XFER",     EPA_OP_G_XFER,               0,0, 0,{0}, NULL, "G_XFER (uses r0=idx, r1=gen, r2=new_owner)"},
	{ "G_XFERX",    EPA_OP_G_XFERX,              1,1, 1, { AK_U32 }, NULL, "G_XFERX <count:u32> (uses csc2=new_owner; consumes handles from stack: lo,hi; pushed in reverse)" },
    {"G_RESIZE",   EPA_OP_G_RESIZE,             0,0, 0,{0}, NULL, "G_RESIZE (uses r0=idx, r1=gen, r2=new_size)"},
    {"G_PTR",      EPA_OP_G_PTR,                0,0, 0,{0}, NULL, "G_PTR (uses r0=idx, r1=gen; returns r0=ptr_lo, r1=ptr_hi)"},
    {"G_META",     EPA_OP_G_META,               0,0, 0,{0}, NULL, "G_META (uses r0=idx, r1=gen; returns r0=owner,r1=type,r2=size,r3=cap)"},
    {"G_TAG",      EPA_OP_G_TAG,                0,0, 0,{0}, NULL, "G_TAG (uses r0=idx, r1=gen; returns r0=tag)"},
	{"GR_MOV4",     EPA_OP_GR_MOV4,               1,1, 1,{AK_U8}, NULL, "GR_MOV4 <rx:u8> (uses r0=idx, r1=gen; returns rx=value)"},
    {"DYN_ALLOC",  EPA_OP_DYN_ALLOC,             1,1, 1,{AK_U32}, NULL, "DYN_ALLOC <pool_id:u32>"},
    {"DYN_FREE",   EPA_OP_DYN_FREE,              1,1, 1,{AK_U32}, NULL, "DYN_FREE <pool_id:u32>"},
    {"DYN_LOAD",   EPA_OP_DYN_LOAD,              1,1, 1,{AK_U32}, NULL, "DYN_LOAD <pool_id:u32>"},
    {"DYN_STORE",  EPA_OP_DYN_STORE,             1,1, 1,{AK_U32}, NULL, "DYN_STORE <pool_id:u32>"},
    {"DYN_SWAP",      EPA_OP_DYN_SWAP,              1,1, 1,{AK_U32}, NULL, "DYN_SWAP <pool_id:u32>"},
    {"DYN_ITER_HEAD", EPA_OP_DYN_ITER_HEAD,         1,1, 1,{AK_U32}, NULL, "DYN_ITER_HEAD <pool_id:u32>"},
    {"DYN_ITER_NEXT", EPA_OP_DYN_ITER_NEXT,         1,1, 1,{AK_U32}, NULL, "DYN_ITER_NEXT <pool_id:u32>"},

    // Debug / interrupts
    {"BREAK",      EPA_OP_BREAK,                 1,1, 1,{AK_U32}, NULL, "BREAK <code:u32>"},
    {"TRAP",       EPA_OP_TRAP,                  1,1, 1,{AK_U32}, NULL, "TRAP <code:u32>"},
    {"EXCEPT",     EPA_OP_EXCEPT,                1,1, 1,{AK_U32}, NULL, "EXCEPT <code:u32>"},
	{"SIGNAL",     EPA_OP_SIGNAL,                0,0, 0,{0}, NULL, "SIGNAL"},
	{"FAR_SIGNAL", EPA_OP_FAR_SIGNAL,            0,0, 0,{0}, NULL, "FAR_SIGNAL"},
	{"HOST_SIGNAL",EPA_OP_HOST_SIGNAL,           0,0, 0,{0}, NULL, "HOST_SIGNAL"},
	{"REQUEST_THREADS",EPA_OP_REQUEST_THREADS,   0,0, 0,{0}, NULL, "REQUEST_THREADS   ; kernel-only, r0=desired_total_threads"},

	{"LOAD_CONST", EPA_OP_LOAD_CONST, 1,1, 1,{AK_U32}, NULL, "LOAD_CONST <id:u32>"},

	{"L_ALLOC", EPA_OP_L_ALLOC, 0,0, 0,{0}, NULL, "L_ALLOC   ; r0=size_bytes -> r0=off,r1=size,r2=ok"},
	{"L_RESET", EPA_OP_L_RESET, 0,0, 0,{0}, NULL, "L_RESET   ; reset local byte arena head"},
	{"L_SCOPE_ENTER", EPA_OP_L_SCOPE_ENTER, 0,0, 0,{0}, NULL, "L_SCOPE_ENTER   ; push local byte arena mark"},
	{"L_SCOPE_LEAVE", EPA_OP_L_SCOPE_LEAVE, 0,0, 0,{0}, NULL, "L_SCOPE_LEAVE   ; restore local byte arena mark"},
	{"L_SCOPE_ALLOC", EPA_OP_L_SCOPE_ALLOC, 0,0, 0,{0}, NULL, "L_SCOPE_ALLOC   ; r0=size_bytes -> r0=off,r1=size,r2=ok"},

	{"FMT", EPA_OP_FMT, 1,1, 1,{AK_U8}, NULL, "FMT <argc:u8>"},
	{"LOG", EPA_OP_LOG, 0,0, 0,{0}, NULL, "LOG   ; log string (r0,r1,r2)"},

	{"SM_PUT", EPA_OP_SM_PUT, 0, 0, 0, {0}, NULL, "SM_PUT: write u32 from r0 to signal mailbox at r3; r3 += 4"},

	{"AT", EPA_OP_AT, 0, 0, 0, {0}, NULL, "AT: (uses: r0=EPA_FUNCTION, r1=GHS_HANDLE_LO, r2=GHS_HANDLE_HI, r3=THREAD_COUNT)"},

  };

  for (size_t i = 0; i < sizeof(kDesc)/sizeof(kDesc[0]); i++) {
    if (ieq(kDesc[i].mnemonic, mn)) return &kDesc[i];
  }
  return NULL;
}

static int emit_line(AsmCtx *ctx, Out *o, int line_no, char *line, char err[EPA_MAX_ERR]) {
  trim_comment(line);
  char *s = ltrim(line);
  rtrim(s);
  if (*s == 0) return 1;

  // Tokenize (space separated)
  char *tok[16] = {0};
  int nt = 0;
  char *p = s;
  while (*p && nt < 16) {
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) break;
    tok[nt++] = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = 0;
  }
  if (nt == 0) return 1;

  // label:
  {
    size_t t0len = strlen(tok[0]);
    if (t0len > 1 && tok[0][t0len - 1] == ':') {
      tok[0][t0len - 1] = 0;
      if (!ctx_add_label(ctx, tok[0], o->len, line_no, err)) return 0;
      if (nt == 1) return 1;
      for (int i = 0; i < nt - 1; i++) tok[i] = tok[i + 1];
      nt--;
    }
  }

  const AsmInsnDesc *d = find_desc(tok[0]);
  if (!d) {
    snprintf(err, EPA_MAX_ERR, "line %d: unknown instruction '%s'", line_no, tok[0]);
    return 0;
  }

  if (d->helper) return d->helper(ctx, o, line_no, nt, tok, d, err);
  return emit_generic(ctx, o, line_no, nt, tok, d, err);
}

/* Two-pass assembler core: f must be seekable (rewindable between passes). */
static uint8_t *compile_from_fp(FILE *f, size_t *out_len, char err[EPA_MAX_ERR])
{
  SStrVec ssv = {0};
  char line[1024];
  int line_no = 0;

  // -------------------------
  // PASS 1: collect .SSTR
  // -------------------------
  while (fgets(line, (int)sizeof(line), f)) {
    line_no++;
    strip_comment(line);
    uint32_t id = 0; uint8_t *bytes = NULL; uint16_t slen = 0;
    int r = parse_sstr_directive_line(line, &id, &bytes, &slen, line_no, err);
    if (r < 0) { sstrvec_free(&ssv); return NULL; }
    if (r == 1) {
      if (!sstrvec_add(&ssv, id, bytes, slen, line_no, err)) {
        sstrvec_free(&ssv); return NULL;
      }
    }
  }
  fseek(f, 0, SEEK_SET);

  // -------------------------
  // PASS 2: emit blob
  // -------------------------
  Out o; memset(&o, 0, sizeof(o));
  AsmCtx ctx; memset(&ctx, 0, sizeof(ctx));

  if (ssv.n > 0) {
    Out str_payload; memset(&str_payload, 0, sizeof(str_payload));
    if (ssv.n > 65535u) { snprintf(err, EPA_MAX_ERR, "too many .SSTR/.CONST_STR entries"); goto fail; }
    if (!out_u16_le(&str_payload, (uint16_t)ssv.n)) { snprintf(err, EPA_MAX_ERR, "OOM"); goto fail; }
    for (size_t i = 0; i < ssv.n; i++) {
      if (!out_u32_le(&str_payload, ssv.v[i].id)) { snprintf(err, EPA_MAX_ERR, "OOM"); goto fail; }
      if (!out_u16_le(&str_payload, ssv.v[i].len)) { snprintf(err, EPA_MAX_ERR, "OOM"); goto fail; }
      if (!out_reserve(&str_payload, ssv.v[i].len)) { snprintf(err, EPA_MAX_ERR, "OOM"); goto fail; }
      memcpy(str_payload.buf + str_payload.len, ssv.v[i].bytes, ssv.v[i].len);
      str_payload.len += ssv.v[i].len;
    }
    if (!out_u16_le(&o, EPA_OP_DATA_BLOCK)) { snprintf(err, EPA_MAX_ERR, "OOM"); goto fail; }
    if (!out_u16_le(&o, 1)) { snprintf(err, EPA_MAX_ERR, "OOM"); goto fail; }
    if (!out_u16_le(&o, DB_STR)) { snprintf(err, EPA_MAX_ERR, "OOM"); goto fail; }
    if (!out_u16_le(&o, 0)) { snprintf(err, EPA_MAX_ERR, "OOM"); goto fail; }
    if (!out_u32_le(&o, (uint32_t)str_payload.len)) { snprintf(err, EPA_MAX_ERR, "OOM"); goto fail; }
    if (!out_reserve(&o, str_payload.len)) { snprintf(err, EPA_MAX_ERR, "OOM"); goto fail; }
    memcpy(o.buf + o.len, str_payload.buf, str_payload.len);
    o.len += str_payload.len;
    while (o.len & 3u) { if (!out_u8(&o, 0)) { snprintf(err, EPA_MAX_ERR, "OOM"); goto fail; } }
    free(str_payload.buf);
  }

  line_no = 0;
  while (fgets(line, (int)sizeof(line), f)) {
    line_no++;
    strip_comment(line);
    { char tmp[1024]; strncpy(tmp, line, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = 0;
      char *s = ltrim(tmp); rtrim(s);
      if (is_sstr_directive(s) || is_const_directive(s)) continue; }
    if (!emit_line(&ctx, &o, line_no, line, err)) goto fail;
  }

  for (size_t i = 0; i < ctx.nfix; i++) {
    Fixup *F = &ctx.fixups[i];
    size_t label_pc = 0;
    if (!ctx_find_label(&ctx, F->name, &label_pc)) {
      snprintf(err, EPA_MAX_ERR, "line %d: unknown label '%s'", F->line_no, F->name);
      goto fail;
    }
    int64_t rel64 = (int64_t)label_pc - (int64_t)F->next_pc;
    if (rel64 < (int64_t)INT32_MIN || rel64 > (int64_t)INT32_MAX) {
      snprintf(err, EPA_MAX_ERR, "line %d: jump to '%s' out of rel32 range", F->line_no, F->name);
      goto fail;
    }
    int32_t r32 = (int32_t)rel64;
    size_t off = F->imm_off;
    o.buf[off+0] = (uint8_t)(r32 & 0xFF);
    o.buf[off+1] = (uint8_t)((r32 >>  8) & 0xFF);
    o.buf[off+2] = (uint8_t)((r32 >> 16) & 0xFF);
    o.buf[off+3] = (uint8_t)((r32 >> 24) & 0xFF);
  }

  ctx_free(&ctx);
  sstrvec_free(&ssv);
  if (out_len) *out_len = o.len;
  return o.buf;

fail:
  free(o.buf);
  ctx_free(&ctx);
  sstrvec_free(&ssv);
  return NULL;
}

uint8_t *epa_asm_compile_src(const char *src, size_t *out_len, char err[EPA_MAX_ERR]) {
  if (err) err[0] = 0;
  if (out_len) *out_len = 0;
  if (!src) { if (err) snprintf(err, EPA_MAX_ERR, "compile_src: null src"); return NULL; }
  size_t slen = strlen(src);
  char *buf = (char *)malloc(slen + 1u);
  if (!buf) { if (err) snprintf(err, EPA_MAX_ERR, "compile_src: OOM"); return NULL; }
  memcpy(buf, src, slen + 1u);
  FILE *f = fmemopen(buf, slen + 1u, "r");
  if (!f) { free(buf); if (err) snprintf(err, EPA_MAX_ERR, "compile_src: fmemopen failed"); return NULL; }
  uint8_t *result = compile_from_fp(f, out_len, err);
  fclose(f);
  free(buf);
  return result;
}

uint8_t *epa_asm_compile_file(const char *path, size_t *out_len, char err[EPA_MAX_ERR]) {
  if (err) err[0] = 0;
  if (out_len) *out_len = 0;

  FILE *f = fopen(path, "rb");
  if (!f) { snprintf(err, EPA_MAX_ERR, "cannot open %s", path); return NULL; }
  uint8_t *result = compile_from_fp(f, out_len, err);
  fclose(f);
  return result;
}
