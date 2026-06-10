#include "epa_program_loader.h"
#include "opcodes/epa_opcode_values.h"
#include "opcodes/epa_opcode_parameter_values.h"
#include "opcodes/opcode_def.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int push_acl_entry(EpaProgramDesc *out, uint64_t remote_uid, uint32_t local_wid, char err[EPA_MAX_ERR]) {
  EpaProgramAclEntry *next = (EpaProgramAclEntry*)realloc(
    out->acl_entries, sizeof(EpaProgramAclEntry) * (out->acl_count + 1u));
  if (!next) {
    snprintf(err, EPA_MAX_ERR, "program_loader: OOM growing ACL");
    return 0;
  }
  out->acl_entries = next;
  out->acl_entries[out->acl_count].remote_kernel_uid = remote_uid;
  out->acl_entries[out->acl_count].local_wid = local_wid;
  out->acl_count++;
  return 1;
}

// Skip ENTRY section (supports nested ENTRY_START/ENTRY_END)
static int skip_entry(
    const uint8_t *blob, size_t blob_len,
    size_t pc_at_entry_start,
    size_t *out_pc_after_entry_end,
    char err[EPA_MAX_ERR]
) {
  size_t pc = pc_at_entry_start;
  int depth = 0;

  while (pc + EPA_OPCODE_BYTES <= blob_len) {
    uint8_t op = blob[pc];
    const EpaOpcodeDef *def = epa_find_opcode(op);
    if (!def) {
      snprintf(err, EPA_MAX_ERR, "program_loader: unknown opcode 0x%02x at pc=%zu", op, pc);
      return 0;
    }

    size_t need = EPA_OPCODE_BYTES + (size_t)def->param_len;
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

  while (pc + EPA_OPCODE_BYTES <= blob_len) {
    uint8_t op = blob[pc];
    const EpaOpcodeDef *def = epa_find_opcode(op);
    if (!def) {
      snprintf(err, EPA_MAX_ERR, "program_loader: unknown opcode 0x%02x at pc=%zu", op, pc);
      return 0;
    }

    size_t need = EPA_OPCODE_BYTES + (size_t)def->param_len;
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

static int skip_at_entry(
    const uint8_t *blob, size_t blob_len,
    size_t pc_at_at_entry_start,
    size_t *out_pc_after_at_entry_end,
    char err[EPA_MAX_ERR]
) {
  size_t pc = pc_at_at_entry_start;
  int depth = 0;

  while (pc + EPA_OPCODE_BYTES <= blob_len) {
    uint8_t op = blob[pc];
    const EpaOpcodeDef *def = epa_find_opcode(op);
    if (!def) {
      snprintf(err, EPA_MAX_ERR, "program_loader: unknown opcode 0x%02x at pc=%zu", op, pc);
      return 0;
    }

    size_t need = EPA_OPCODE_BYTES + (size_t)def->param_len;
    if (pc + need > blob_len) {
      snprintf(err, EPA_MAX_ERR, "program_loader: truncated %s at pc=%zu", def->name, pc);
      return 0;
    }

    if (op == EPA_OP_AT_ENTRY_START) depth++;
    else if (op == EPA_OP_AT_ENTRY_END) {
      depth--;
      if (depth == 0) {
        *out_pc_after_at_entry_end = pc + need;
        return 1;
      }
      if (depth < 0) {
        snprintf(err, EPA_MAX_ERR, "program_loader: AT_ENTRY_END without AT_ENTRY_START at pc=%zu", pc);
        return 0;
      }
    }

    pc += need;
  }

  snprintf(err, EPA_MAX_ERR, "program_loader: unterminated AT_ENTRY (missing AT_ENTRY_END)");
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
  //   u8 opcode = DATA_BLOCK
  //   u16 chunk_count
  //   chunk_count * {
  //     u16 kind
  //     u16 flags
  //     u32 payload_len
  //     u8  payload[payload_len]
  //   }
  //   (optional padding to 4-byte alignment between chunks / at end)

  if (pc_at_data_block + EPA_OPCODE_BYTES + 2 > blob_len) {
    snprintf(err, EPA_MAX_ERR, "program_loader: truncated DATA_BLOCK header at pc=%zu", pc_at_data_block);
    return 0;
  }

  size_t pc = pc_at_data_block;

  // u8 opcode already known by caller; skip it
  pc += EPA_OPCODE_BYTES;

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
  free(p->at_entries);
  free(p->dynamic_pools);
  free(p->acl_entries);
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
  out->at_entries = NULL;
  out->nat_entries = 0;

  out->image_base = blob;
  out->image_size = blob_len;

  size_t pc = 0;
  while (pc + EPA_OPCODE_BYTES <= blob_len) {
    uint8_t op = blob[pc];

    // Handle variable-length opcodes FIRST (they cannot use def->param_len)
    if (op == EPA_OP_DATA_BLOCK) {
      size_t pc_after = 0;
      if (!parse_data_block(out, blob, blob_len, pc, &pc_after, err)) return 0;
      pc = pc_after;
      continue;
    }

    const EpaOpcodeDef *def = epa_find_opcode(op);
    if (!def) {
      snprintf(err, EPA_MAX_ERR, "program_loader: unknown opcode 0x%02x at pc=%zu", op, pc);
      return 0;
    }

    size_t need = EPA_OPCODE_BYTES + (size_t)def->param_len;
    if (pc + need > blob_len) {
      snprintf(err, EPA_MAX_ERR, "program_loader: truncated %s at pc=%zu", def->name, pc);
      return 0;
    }

    if (op == EPA_OP_ENTRY_START) {
      // ENTRY_START: u32 id, u16 in_words, u16 out_words (param_len=8)
      uint32_t id = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES);
      uint16_t in_words  = EPA_READ_U16_LE(blob, pc + EPA_OPCODE_BYTES + 4);
      uint16_t out_words = EPA_READ_U16_LE(blob, pc + EPA_OPCODE_BYTES + 6);
      uint32_t signal_mail_box_size = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES + 8);

      if (id >= 256) {
        snprintf(err, EPA_MAX_ERR, "program_loader: ENTRY_START id=%u out of range", (unsigned)id);
        return 0;
      }

      size_t pc_after = 0;
      if (!skip_entry(blob, blob_len, pc, &pc_after, err)) return 0;

      size_t body_start = pc + need;
      size_t body_end   = pc_after - EPA_OPCODE_BYTES; // start of ENTRY_END
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

    if (op == EPA_OP_KERNEL_ID) {
      uint32_t lo = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES);
      uint32_t hi = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES + 4u);
      out->kernel_uid = ((uint64_t)hi << 32) | (uint64_t)lo;
      pc += need;
      continue;
    }

    if (op == EPA_OP_SET_MODE) {
      uint8_t kind = blob[pc + EPA_OPCODE_BYTES];
      uint8_t target = blob[pc + EPA_OPCODE_BYTES + 1u];
      uint32_t value = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES + 2u);
      if (kind == EPA_SET_MODE_BACKEND) {
        (void)target;
        /* Backend mode is retained as image metadata for future backends. */
      } else if (kind == EPA_SET_MODE_WORKER_PRIVILEGE) {
        out->worker_privilege[target] = value;
      } else if (kind == EPA_SET_MODE_WORKER_IGNORE_MAX_TICKS) {
        out->worker_ignore_max_ticks[target] = value ? 1u : 0u;
      } else {
        snprintf(err, EPA_MAX_ERR, "program_loader: unknown SET_MODE kind=%u at pc=%zu", (unsigned)kind, pc);
        return 0;
      }
      pc += need;
      continue;
    }

    if (op == EPA_OP_ACL_ALLOW) {
      uint32_t lo = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES);
      uint32_t hi = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES + 4u);
      uint32_t local_wid = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES + 8u);
      uint64_t remote_uid = ((uint64_t)hi << 32) | (uint64_t)lo;
      if (!push_acl_entry(out, remote_uid, local_wid, err)) return 0;
      pc += need;
      continue;
    }

    if (op == EPA_OP_DYNAMIC_POOL) {
      uint32_t pool_id = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES);
      uint32_t element_size = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES + 4);
      uint32_t min_free = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES + 8);
      uint32_t max_free = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES + 12);
      uint32_t grow_by = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES + 16);
      size_t i;
      EpaProgramDynamicPoolDesc *np;

      if (pool_id != out->dynamic_pool_count) {
        snprintf(err, EPA_MAX_ERR, "program_loader: DYNAMIC_POOL pool_id=%u must be dense and declared in order", (unsigned)pool_id);
        return 0;
      }
      if (element_size == 0u) {
        snprintf(err, EPA_MAX_ERR, "program_loader: DYNAMIC_POOL pool_id=%u has element_size=0", (unsigned)pool_id);
        return 0;
      }
      if (grow_by == 0u) {
        snprintf(err, EPA_MAX_ERR, "program_loader: DYNAMIC_POOL pool_id=%u has grow_by=0", (unsigned)pool_id);
        return 0;
      }
      if (min_free > max_free) {
        snprintf(err, EPA_MAX_ERR, "program_loader: DYNAMIC_POOL pool_id=%u has min_free > max_free", (unsigned)pool_id);
        return 0;
      }
      for (i = 0; i < out->dynamic_pool_count; i++) {
        if (out->dynamic_pools[i].pool_id == pool_id) {
          snprintf(err, EPA_MAX_ERR, "program_loader: duplicate DYNAMIC_POOL pool_id=%u", (unsigned)pool_id);
          return 0;
        }
      }

      np = (EpaProgramDynamicPoolDesc*)realloc(
          out->dynamic_pools,
          (out->dynamic_pool_count + 1u) * sizeof(EpaProgramDynamicPoolDesc));
      if (!np) {
        snprintf(err, EPA_MAX_ERR, "program_loader: OOM growing dynamic pool table");
        return 0;
      }
      out->dynamic_pools = np;
      out->dynamic_pools[out->dynamic_pool_count].pool_id = pool_id;
      out->dynamic_pools[out->dynamic_pool_count].element_size = element_size;
      out->dynamic_pools[out->dynamic_pool_count].min_free = min_free;
      out->dynamic_pools[out->dynamic_pool_count].max_free = max_free;
      out->dynamic_pools[out->dynamic_pool_count].grow_by = grow_by;
      out->dynamic_pool_count++;

      pc += need;
      continue;
    }

    if (op == EPA_OP_FUNC_START) {
      // FUNC_START: u32 func_id, u16 frame_words (param_len=6)
      uint32_t func_id = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES);
      uint16_t frame_words = EPA_READ_U16_LE(blob, pc + EPA_OPCODE_BYTES + 4);

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
      size_t body_end   = pc_after - EPA_OPCODE_BYTES; // start of FUNC_END
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

    if (op == EPA_OP_AT_ENTRY_START) {
      uint32_t at_id = EPA_READ_U32_LE(blob, pc + EPA_OPCODE_BYTES);
      uint16_t frame_words = EPA_READ_U16_LE(blob, pc + EPA_OPCODE_BYTES + 4);

      for (size_t i = 0; i < out->nat_entries; i++) {
        if (out->at_entries[i].at_id == at_id) {
          snprintf(err, EPA_MAX_ERR, "program_loader: duplicate AT_ENTRY_START at_id=%u", (unsigned)at_id);
          return 0;
        }
      }

      size_t pc_after = 0;
      if (!skip_at_entry(blob, blob_len, pc, &pc_after, err)) return 0;

      size_t body_start = pc + need;
      size_t body_end   = pc_after - EPA_OPCODE_BYTES; // start of AT_ENTRY_END
      if (body_end < body_start) {
        snprintf(err, EPA_MAX_ERR, "program_loader: malformed AT_ENTRY at_id=%u", (unsigned)at_id);
        return 0;
      }

      EpaAtEntryDesc *na = (EpaAtEntryDesc *)realloc(out->at_entries, (out->nat_entries + 1) * sizeof(EpaAtEntryDesc));
      if (!na) {
        snprintf(err, EPA_MAX_ERR, "program_loader: OOM adding AT_ENTRY at_id=%u", (unsigned)at_id);
        return 0;
      }
      out->at_entries = na;

      EpaAtEntryDesc *A = &out->at_entries[out->nat_entries++];
      memset(A, 0, sizeof(*A));
      A->at_id = at_id;
      A->frame_words = frame_words;
      A->code.code = blob + body_start;
      A->code.code_len = (body_end - body_start);
      A->code.abs_base = (uint32_t)body_start;

      pc = pc_after;
      continue;
    }

    pc += need;
  }

  return 1;
}
