#include "epa_program_loader.h"
#include "opcodes/epa_opcode_values.h"
#include "opcodes/opcode_def.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Skip ENTRY section (supports nested ENTRY_START/ENTRY_END)
static int skip_entry(
    const uint8_t *blob, size_t blob_len,
    size_t pc_at_entry_start,
    size_t *out_pc_after_entry_end,
    char err[EPA_MAX_ERR]
) {
  size_t pc = pc_at_entry_start;
  int depth = 0;

  while (pc + 2 <= blob_len) {
    uint16_t op = EPA_READ_U16_LE(blob, pc);
    const EpaOpcodeDef *def = epa_find_opcode(op);
    if (!def) {
      snprintf(err, EPA_MAX_ERR, "program_loader: unknown opcode 0x%04x at pc=%zu", op, pc);
      return 0;
    }

    size_t need = 2u + (size_t)def->param_len;
    if (pc + need > blob_len) {
      snprintf(err, EPA_MAX_ERR, "program_loader: truncated %s at pc=%zu", def->name, pc);
      return 0;
    }

    if (op == EPA_OP_ENTRY_START) depth++;
    else if (op == EPA_OP_ENTRY_END) {
      depth--;
      if (depth == 0) {
        *out_pc_after_entry_end = pc + need;
        return 1;
      }
      if (depth < 0) {
        snprintf(err, EPA_MAX_ERR, "program_loader: ENTRY_END without ENTRY_START at pc=%zu", pc);
        return 0;
      }
    }

    pc += need;
  }

  snprintf(err, EPA_MAX_ERR, "program_loader: unterminated ENTRY (missing ENTRY_END)");
  return 0;
}

// Skip FUNC section (supports nested FUNC_START/FUNC_END; functions are not expected
// to be nested, but the depth logic makes malformed blobs easier to diagnose.)
static int skip_func(
    const uint8_t *blob, size_t blob_len,
    size_t pc_at_func_start,
    size_t *out_pc_after_func_end,
    char err[EPA_MAX_ERR]
) {
  size_t pc = pc_at_func_start;
  int depth = 0;

  while (pc + 2 <= blob_len) {
    uint16_t op = EPA_READ_U16_LE(blob, pc);
    const EpaOpcodeDef *def = epa_find_opcode(op);
    if (!def) {
      snprintf(err, EPA_MAX_ERR, "program_loader: unknown opcode 0x%04x at pc=%zu", op, pc);
      return 0;
    }

    size_t need = 2u + (size_t)def->param_len;
    if (pc + need > blob_len) {
      snprintf(err, EPA_MAX_ERR, "program_loader: truncated %s at pc=%zu", def->name, pc);
      return 0;
    }

    if (op == EPA_OP_FUNC_START) depth++;
    else if (op == EPA_OP_FUNC_END) {
      depth--;
      if (depth == 0) {
        *out_pc_after_func_end = pc + need;
        return 1;
      }
      if (depth < 0) {
        snprintf(err, EPA_MAX_ERR, "program_loader: FUNC_END without FUNC_START at pc=%zu", pc);
        return 0;
      }
    }

    pc += need;
  }

  snprintf(err, EPA_MAX_ERR, "program_loader: unterminated FUNC (missing FUNC_END)");
  return 0;
}

static int parse_data_block(
    EpaProgramDesc *out,
    const uint8_t *blob, size_t blob_len,
    size_t pc_at_data_block,
    size_t *out_pc_after_data_block,
    char err[EPA_MAX_ERR]
) {
  // DATA_BLOCK v2 (chunked):
  //   u16 opcode = DATA_BLOCK
  //   u16 chunk_count
  //   chunk_count * {
  //     u16 kind
  //     u16 flags
  //     u32 payload_len
  //     u8  payload[payload_len]
  //   }
  //   (optional padding to 4-byte alignment between chunks / at end)

  if (pc_at_data_block + 2 + 2 > blob_len) {
    snprintf(err, EPA_MAX_ERR, "program_loader: truncated DATA_BLOCK header at pc=%zu", pc_at_data_block);
    return 0;
  }

  size_t pc = pc_at_data_block;

  // u16 opcode already known by caller; skip it
  pc += 2;

  uint16_t chunk_count = EPA_READ_U16_LE(blob, pc);
  pc += 2;

  // reset const table
  free(out->consts);
  out->consts = NULL;
  out->nconsts = 0;

  for (uint16_t ci = 0; ci < chunk_count; ci++) {
    if (pc + 2 + 2 + 4 > blob_len) {
      snprintf(err, EPA_MAX_ERR, "program_loader: truncated DATA_BLOCK chunk header at pc=%zu", pc);
      return 0;
    }

    uint16_t kind  = EPA_READ_U16_LE(blob, pc); pc += 2;
    uint16_t flags = EPA_READ_U16_LE(blob, pc); pc += 2;
    uint32_t plen  = EPA_READ_U32_LE(blob, pc); pc += 4;

    (void)flags;

    if (pc + (size_t)plen > blob_len) {
      snprintf(err, EPA_MAX_ERR, "program_loader: truncated DATA_BLOCK chunk payload at pc=%zu", pc_at_data_block);
      return 0;
    }

    const uint8_t *payload = blob + pc;

    // ---- chunk kinds ----
    // DB_STR payload:
    //   u16 count
    //   repeat count:
    //     u32 id
    //     u16 len
    //     u8  bytes[len]   (no NUL)
    if (kind == 1 /* DB_STR */) {
      if (plen < 2) {
        snprintf(err, EPA_MAX_ERR, "program_loader: DB_STR payload too small");
        return 0;
      }

      size_t ppc = 0;
      uint16_t count = EPA_READ_U16_LE(payload, ppc);
      ppc += 2;

      // allocate/append consts
      size_t base = out->nconsts;
      size_t new_n = base + (size_t)count;

      EpaConst *nc = (EpaConst*)realloc(out->consts, new_n * sizeof(EpaConst));
      if (!nc) {
        snprintf(err, EPA_MAX_ERR, "program_loader: OOM growing const table");
        return 0;
      }
      out->consts = nc;
      out->nconsts = new_n;

      for (uint16_t i = 0; i < count; i++) {
        if (ppc + 4 + 2 > (size_t)plen) {
          snprintf(err, EPA_MAX_ERR, "program_loader: truncated DB_STR entry");
          return 0;
        }

        uint32_t id  = EPA_READ_U32_LE(payload, ppc); ppc += 4;
        uint16_t len = EPA_READ_U16_LE(payload, ppc); ppc += 2;

        if (ppc + (size_t)len > (size_t)plen) {
          snprintf(err, EPA_MAX_ERR, "program_loader: truncated DB_STR bytes");
          return 0;
        }

        // Store absolute offsets into the blob (matching your old const model),
        // so VM can reference blob memory directly:
        //   c.a = absolute byte offset in blob
        //   c.b = length
        EpaConst c;
        memset(&c, 0, sizeof(c));
        c.id    = id;
        c.kind  = EPA_CONST_STR;
        c.flags = 0;
        c.aux   = 0;
        c.a     = (uint32_t)(pc + ppc); // absolute offset into blob
        c.b     = (uint32_t)len;

        out->consts[base + i] = c;

        ppc += (size_t)len;
      }

      // Note: any trailing bytes inside plen are ignored (but you shouldn't have any)
    } else {
      // Unknown chunk kinds can be skipped for forward-compat
      // (or make it fatal if you prefer strictness)
    }

    pc += (size_t)plen;

    // match compiler: align to 4 bytes after chunk payload
    while (pc & 3u) pc++;
  }

  *out_pc_after_data_block = pc;
  return 1;
}

void epa_program_free(EpaProgramDesc *p) {
  if (!p) return;
  free(p->consts);
  free(p->funcs);
  memset(p, 0, sizeof(*p));
}

// Parse blob -> fill entry code views.
// NOTE: This does NOT take ownership of the blob. Caller must keep blob alive.
int epa_program_parse(
    EpaProgramDesc *out,
    const uint8_t *blob, size_t blob_len,
    char err[EPA_MAX_ERR]
) {
  if (err) err[0] = 0;
  if (!out || !blob || blob_len < 2) {
    snprintf(err, EPA_MAX_ERR, "program_loader: invalid args");
    return 0;
  }

  memset(out, 0, sizeof(*out));

  out->funcs = NULL;
  out->nfuncs = 0;

  out->image_base = blob;
  out->image_size = blob_len;

  size_t pc = 0;
  while (pc + 2 <= blob_len) {
    uint16_t op = EPA_READ_U16_LE(blob, pc);

    // Handle variable-length opcodes FIRST (they cannot use def->param_len)
    if (op == EPA_OP_DATA_BLOCK) {
      size_t pc_after = 0;
      if (!parse_data_block(out, blob, blob_len, pc, &pc_after, err)) return 0;
      pc = pc_after;
      continue;
    }

    const EpaOpcodeDef *def = epa_find_opcode(op);
    if (!def) {
      snprintf(err, EPA_MAX_ERR, "program_loader: unknown opcode 0x%04x at pc=%zu", op, pc);
      return 0;
    }

    size_t need = 2u + (size_t)def->param_len;
    if (pc + need > blob_len) {
      snprintf(err, EPA_MAX_ERR, "program_loader: truncated %s at pc=%zu", def->name, pc);
      return 0;
    }

    if (op == EPA_OP_ENTRY_START) {
      // ENTRY_START: u32 id, u16 in_words, u16 out_words (param_len=8)
      uint32_t id = EPA_READ_U32_LE(blob, pc + 2);
      uint16_t in_words  = EPA_READ_U16_LE(blob, pc + 6);
      uint16_t out_words = EPA_READ_U16_LE(blob, pc + 8);
      uint32_t signal_mail_box_size = EPA_READ_U32_LE(blob, pc + 10);

      if (id >= 256) {
        snprintf(err, EPA_MAX_ERR, "program_loader: ENTRY_START id=%u out of range", (unsigned)id);
        return 0;
      }

      size_t pc_after = 0;
      if (!skip_entry(blob, blob_len, pc, &pc_after, err)) return 0;

      size_t body_start = pc + need;
      size_t body_end   = pc_after - 2; // start of ENTRY_END
      if (body_end < body_start) {
        snprintf(err, EPA_MAX_ERR, "program_loader: malformed ENTRY id=%u", (unsigned)id);
        return 0;
      }

      out->entries[id].code = blob + body_start;
      out->entries[id].code_len = (body_end - body_start);

      // If your EpaCodeView has abs_base (recommended debug metadata), keep this.
      // If it does NOT, delete the next line.
      out->entries[id].abs_base = (uint32_t)body_start;

      out->entry_present[id] = 1;
      out->entry_in_words[id]  = in_words;
      out->entry_out_words[id] = out_words;
      out->signal_mailbox_size[id] = signal_mail_box_size;

      pc = pc_after;
      continue;
    }

    if (op == EPA_OP_FUNC_START) {
      // FUNC_START: u32 func_id, u16 frame_words (param_len=6)
      uint32_t func_id = EPA_READ_U32_LE(blob, pc + 2);
      uint16_t frame_words = EPA_READ_U16_LE(blob, pc + 6);

      // disallow duplicate func_id
      for (size_t i = 0; i < out->nfuncs; i++) {
        if (out->funcs[i].func_id == func_id) {
          snprintf(err, EPA_MAX_ERR, "program_loader: duplicate FUNC_START func_id=%u", (unsigned)func_id);
          return 0;
        }
      }

      size_t pc_after = 0;
      if (!skip_func(blob, blob_len, pc, &pc_after, err)) return 0;

      size_t body_start = pc + need;
      size_t body_end   = pc_after - 2; // start of FUNC_END
      if (body_end < body_start) {
        snprintf(err, EPA_MAX_ERR, "program_loader: malformed FUNC func_id=%u", (unsigned)func_id);
        return 0;
      }

      EpaFuncDesc *nf = (EpaFuncDesc *)realloc(out->funcs, (out->nfuncs + 1) * sizeof(EpaFuncDesc));
      if (!nf) {
        snprintf(err, EPA_MAX_ERR, "program_loader: OOM adding FUNC func_id=%u", (unsigned)func_id);
        return 0;
      }
      out->funcs = nf;

      EpaFuncDesc *F = &out->funcs[out->nfuncs++];
      memset(F, 0, sizeof(*F));
      F->func_id = func_id;
      F->frame_words = frame_words;
      F->code.code = blob + body_start;
      F->code.code_len = (body_end - body_start);
      F->code.abs_base = (uint32_t)body_start;

      pc = pc_after;
      continue;
    }

    pc += need;
  }

  return 1;
}
