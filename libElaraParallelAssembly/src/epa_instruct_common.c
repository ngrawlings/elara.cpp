#include "epa_instruct_common.h"
#include "epa_program_desc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "log.h"

#include "epa_opcode_helpers.h"

#include "memory/epa_ghs.h"
#include "epa_instruct_helpers.h"
#include "atomic_tasks/epa_at_router.h"

#define EPA_CALL_MAGIC0 0xC011CA11u
#define EPA_CALL_MAGIC1 0xF00DF00Du

#include "epa_kernel.h"

int epa_kernel_deliver_ghs_handles(EpaKernel *k,
                                   uint32_t dst_wid,
                                   const uint64_t *ghs_handles,
                                   uint32_t ghs_handle_count,
                                   char err[EPA_MAX_ERR]);

static uint32_t pack_eip(const EpaEip *e) {
  return ((uint32_t)e->block_type & 0xFFu) | (((uint32_t)e->block_id & 0xFFFFu) << 8);
}
static void unpack_eip(uint32_t packed, EpaEip *e) {
  e->block_type = (uint8_t)(packed & 0xFFu);
  e->block_id   = (uint16_t)((packed >> 8) & 0xFFFFu);
}

int epa_flow_push_ret_frame(EpaStack *st, const EpaEip *ret, uint16_t argc, char err[EPA_MAX_ERR]) {
  if (!epa_stack_push(st, EPA_CALL_MAGIC0) ||
      !epa_stack_push(st, pack_eip(ret)) ||
      !epa_stack_push(st, ret->rel_pc) ||
      !epa_stack_push(st, (uint32_t)argc) ||
      !epa_stack_push(st, EPA_CALL_MAGIC1)) {
    if (err) snprintf(err, EPA_MAX_ERR, "stack overflow pushing call frame");
    return 0;
  }
  return 1;
}

int epa_flow_pop_ret_frame(EpaStack *st, EpaEip *out_ret, uint16_t *out_argc, char err[EPA_MAX_ERR]) {
  uint32_t m1, argc_w, pc_w, packed, m0;
  if (!epa_stack_pop(st, &m1) ||
      !epa_stack_pop(st, &argc_w) ||
      !epa_stack_pop(st, &pc_w) ||
      !epa_stack_pop(st, &packed) ||
      !epa_stack_pop(st, &m0)) {
    if (err) snprintf(err, EPA_MAX_ERR, "stack underflow popping call frame");
    return 0;
  }
  if (m0 != EPA_CALL_MAGIC0 || m1 != EPA_CALL_MAGIC1) {
    if (err) snprintf(err, EPA_MAX_ERR, "call frame magic mismatch (corrupt stack)");
    return 0;
  }
  unpack_eip(packed, out_ret);
  out_ret->rel_pc = pc_w;
  if (out_argc) *out_argc = (uint16_t)(argc_w & 0xFFFFu);
  return 1;
}

static int resolve_here(
    const EpaProgramDesc *p, const EpaEip *e,
    const uint8_t **out_code, size_t *out_len
) {
  return epa_prog_resolve(p, e->block_type, e->block_id, out_code, out_len);
}

static int read_rel32(const uint8_t *code, size_t pc, int32_t *out) {
  uint32_t u = (uint32_t)code[pc+2] |
               ((uint32_t)code[pc+3] << 8) |
               ((uint32_t)code[pc+4] << 16) |
               ((uint32_t)code[pc+5] << 24);
  *out = (int32_t)u;
  return 1;
}

static const EpaConst* prog_find_const(const EpaProgramDesc *p, uint32_t id) {
  // v1: linear scan; later: binary search if sorted
  for (size_t i = 0; i < p->nconsts; i++) {
    if (p->consts[i].id == id) return &p->consts[i];
  }
  return NULL;
}

static int epa_resolve_string(
    const EpaProgramDesc *prog,
    const EpaWorkerState *w,
    uint32_t kind,
    uint32_t off,
    uint32_t len,
    const uint8_t **out_ptr,
    uint32_t *out_len
) {
  if (len == 0) {
    *out_ptr = (const uint8_t*)"";
    *out_len = 0;
    return 1;
  }

  if (kind == EPA_CONST_STR) {
    if (!prog->image_base) return 0;
    if ((size_t)off > prog->image_size) return 0;
    if ((size_t)off + (size_t)len > prog->image_size) return 0;
    *out_ptr = prog->image_base + off;
    *out_len = len;
    return 1;
  }

  if (kind == EPA_CONST_TMP_STR) {
    if (!w->vm.lbytes || off + len > w->vm.lbytes_top) return 0;
    *out_ptr = w->vm.lbytes + off;
    *out_len = len;
    return 1;
  }

  return 0;
}

static uint32_t epa_u32_to_dec(char *dst, uint32_t cap, uint32_t v) {
  char tmp[16];
  uint32_t n = 0;
  do {
    tmp[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  } while (v && n < sizeof(tmp));
  uint32_t out = 0;
  while (n && out < cap) dst[out++] = tmp[--n];
  return out;
}

static uint32_t epa_i32_to_dec(char *dst, uint32_t cap, int32_t v) {
  if (cap == 0) return 0;
  uint32_t out = 0;
  uint32_t uv;
  if (v < 0) {
    dst[out++] = '-';
    uv = (uint32_t)(-(int64_t)v);
  } else {
    uv = (uint32_t)v;
  }
  if (out >= cap) return out;
  out += epa_u32_to_dec(dst + out, cap - out, uv);
  return out;
}

static uint32_t epa_u32_to_hex(char *dst, uint32_t cap, uint32_t v) {
  static const char *hex = "0123456789abcdef";
  char tmp[8];
  for (int i = 0; i < 8; i++) {
    tmp[7 - i] = hex[(v >> (i * 4)) & 0xFu];
  }
  // trim leading zeros but keep at least 1 digit
  int start = 0;
  while (start < 7 && tmp[start] == '0') start++;
  uint32_t out = 0;
  for (int i = start; i < 8 && out < cap; i++) dst[out++] = tmp[i];
  return out;
}

EpaFlowRc epa_flow_step(
	void *_k,
    const EpaFlowCtx *ctx,
	EpaWorkerState *w,
    EpaStack *st,
    char err[EPA_MAX_ERR]
) {
  if (err) err[0] = 0;
  if (!ctx || !ctx->prog || !w || !st) {
    snprintf(err, EPA_MAX_ERR, "epa_flow_step: NULL arg");
    return EPA_FLOW_ERR;
  }

  EpaKernel *k = ((EpaKernel*)_k);
  EpaEip *eip = &w->vm.eip;

  const uint8_t *code = NULL;
  size_t code_len = 0;
  if (!resolve_here(ctx->prog, eip, &code, &code_len)) {
    snprintf(err, EPA_MAX_ERR, "EIP resolve failed type=%u id=%u", (unsigned)eip->block_type, (unsigned)eip->block_id);
    return EPA_FLOW_ERR;
  }

  size_t pc = (size_t)eip->rel_pc;
  if (pc + 2 > code_len) {
    snprintf(err, EPA_MAX_ERR, "EIP out of range pc=%zu len=%zu", pc, code_len);
    return EPA_FLOW_ERR;
  }

  uint16_t op = EPA_READ_U16_LE(code, pc);
  const EpaOpcodeDef *def = epa_find_opcode(op);
  if (!def) {
    snprintf(err, EPA_MAX_ERR, "unknown opcode 0x%04x at pc=%zu", op, pc);
    return EPA_FLOW_ERR;
  }
  size_t need = 2u + (size_t)def->param_len;
  if (pc + need > code_len) {
    snprintf(err, EPA_MAX_ERR, "truncated %s at pc=%zu", def->name, pc);
    return EPA_FLOW_ERR;
  }

  TRACE("[GL-NF] slot=%u %s[%u] pc=%zu op=0x%04x %s\n",
        (unsigned)w->id,
        (eip->block_type==EPA_BLOCK_ENTRY)?"entry":"func",
        (unsigned)eip->block_id,
        pc, op, def->name);

  // Identify flow opcodes here. Everything else => backend.
  switch (op) {
    case EPA_OP_END:
      return EPA_FLOW_OK;

    case EPA_OP_YIELD:
      eip->rel_pc = (uint32_t)(pc + need);
      return EPA_FLOW_YIELDED;

    case EPA_OP_JMP_REL32: {
      int32_t rel; read_rel32(code, pc, &rel);
      int64_t next = (int64_t)(pc + need);
      int64_t tgt  = next + (int64_t)rel;
      if (tgt < 0 || tgt > (int64_t)code_len) {
        snprintf(err, EPA_MAX_ERR, "JMP out of range tgt=%lld len=%zu", (long long)tgt, code_len);
        return EPA_FLOW_ERR;
      }
      eip->rel_pc = (uint32_t)tgt;
      return EPA_FLOW_YIELDED;
    }

    // Scheduler hooks (flow-owned, backend-notified)
    case EPA_OP_SYNC:
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_sync && !ctx->hooks.on_sync(ctx->hooks_user, err)) return EPA_FLOW_ERR;
      return EPA_FLOW_YIELDED;

    case EPA_OP_WAIT_ON_SYNC:
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_wait_on_sync && !ctx->hooks.on_wait_on_sync(ctx->hooks_user, err)) return EPA_FLOW_ERR;
      return EPA_FLOW_YIELDED;

    case EPA_OP_ENTRY_EXEC: {
      uint8_t wid = code[pc + 2];
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_entry_exec && !ctx->hooks.on_entry_exec(ctx->hooks_user, wid, err)) return EPA_FLOW_ERR;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_ENTRY_HALT: {
      uint8_t wid = code[pc + 2];
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_entry_halt && !ctx->hooks.on_entry_halt(ctx->hooks_user, wid, err)) return EPA_FLOW_ERR;
      return EPA_FLOW_YIELDED;
    }

    // Interrupt / debug
    case EPA_OP_BREAK: {
      uint32_t code_u = EPA_READ_U32_LE(code, pc + 2);
      EpaEip at = *eip;
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_break && !ctx->hooks.on_break(ctx->hooks_user, (uint8_t)w->id, code_u, &at, err)) {
        return EPA_FLOW_ERR;
      }
      eip->rel_pc = (uint32_t)(pc + need);
      return EPA_FLOW_YIELDED;
    }

    // Interrupt / debug
    case EPA_OP_SIGNAL: {
      // Advance PC first (so "at" points at SIGNAL, and execution continues after it)
      eip->rel_pc = (uint32_t)(pc + need);

      // If no hook installed, SIGNAL is a noop+yield (per your design)
      if (ctx->hooks.on_signal) {
        if (!ctx->hooks.on_signal(ctx->hooks_user, (uint8_t)w->id, err)) {
          return EPA_FLOW_ERR;
        }
      }

      // Always yield so host/ElaraScript has a natural boundary to react
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_TRAP: {
      uint32_t code_u = EPA_READ_U32_LE(code, pc + 2);
      EpaEip at = *eip;
      // TRAP fires only when r0 == 0 (assert-false)
      if (w->vm.csc[0] == 0u) {
        if (ctx->hooks.on_trap) {
          (void)ctx->hooks.on_trap(ctx->hooks_user, (uint8_t)w->id, code_u, &at, err);
        }
        snprintf(err, EPA_MAX_ERR, "TRAP code=%u at type=%u id=%u pc=%u", (unsigned)code_u,
                 (unsigned)at.block_type, (unsigned)at.block_id, (unsigned)at.rel_pc);
        return EPA_FLOW_ERR;
      }
      eip->rel_pc = (uint32_t)(pc + need);
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_EXCEPT: {
      uint32_t code_u = EPA_READ_U32_LE(code, pc + 2);
      EpaEip at = *eip;
      int ret = 0;
      if (ctx->hooks.on_except) {
        ret = ctx->hooks.on_except(ctx->hooks_user, (uint8_t)w->id, code_u, &at, err);
      }
      snprintf(err, EPA_MAX_ERR, "EXCEPT code=%u at type=%u id=%u pc=%u", (unsigned)code_u,
               (unsigned)at.block_type, (unsigned)at.block_id, (unsigned)at.rel_pc);

      if (!ret)
    	  return EPA_FLOW_ERR;
      eip->rel_pc = (uint32_t)(pc + need);
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_WAIT_FOR_DATA: {
      // worker-only
      if (w->id == 0) {
        snprintf(err, EPA_MAX_ERR, "WAIT_FOR_DATA only valid in workers");
        return EPA_FLOW_ERR;
      }

      w->waiting_for_data = 1;
      w->blocked = 1;

      eip->rel_pc = (uint32_t)(pc + need);
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_DATA_READY: {
      // kernel-only
      if (w->id != 0) {
        snprintf(err, EPA_MAX_ERR, "DATA_READY only valid in kernel");
        return EPA_FLOW_ERR;
      }
      if (!ctx->hooks.on_data_ready) {
        snprintf(err, EPA_MAX_ERR, "DATA_READY hook not installed");
        return EPA_FLOW_ERR;
      }

      // param is right after opcode (opcode=2 bytes, then u8 wid)
      if (pc + 2 + 1 > (int)code_len) {
        snprintf(err, EPA_MAX_ERR, "DATA_READY truncated");
        return EPA_FLOW_ERR;
      }
      uint8_t wid = code[pc + 2];

      int ok = ctx->hooks.on_data_ready(ctx->hooks_user, wid, err);
      if (!ok) return EPA_FLOW_ERR;

      // on_data_ready should set an internal flag for success; simplest: return success in ok via hook
      // But we also want 0/1 on stack: easiest convention -> hook writes err only, and we push 1 always if no err.
      // Better: have hook return 0/1 success and never set err unless fatal.
      if (!epa_stack_push(st, (uint32_t)ok)) {
        snprintf(err, EPA_MAX_ERR, "DATA_READY: stack overflow");
        return EPA_FLOW_ERR;
      }

      eip->rel_pc = (uint32_t)(pc + need);
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_WAIT_FOR_AT: {
          // worker-only
          if (w->id == 0) {
            snprintf(err, EPA_MAX_ERR, "WAIT_FOR_AT only valid in workers");
            return EPA_FLOW_ERR;
          }

          if (!epa_at_router_update_worker(w, err)) {
            return EPA_FLOW_ERR;
          }

          if (w->at_running) {
        	  w->blocked = 1;
              return EPA_FLOW_YIELDED;
          }

          eip->rel_pc = (uint32_t)(pc + need);
          return EPA_FLOW_YIELDED;
    }

    case EPA_OP_CMP: {
            int32_t a = w->vm.csc[0];
            int32_t b = w->vm.csc[1];

            if (a < b)
                w->vm.csc[0] = -1;
            else if (a > b)
                w->vm.csc[0] = 1;
            else
                w->vm.csc[0] = 0;

            eip->rel_pc = pc + need;
            return EPA_FLOW_YIELDED;
        }

    case EPA_OP_CMPZ: {
        int32_t a = w->vm.csc[2];
        int32_t b = w->vm.csc[3];

        if (a < b)
            w->vm.csc[0] = -1;
        else if (a > b)
            w->vm.csc[0] = 1;
        else
            w->vm.csc[0] = 0;

        eip->rel_pc = pc + need;
        return EPA_FLOW_YIELDED;
    }

    // Conditions jumps
    case EPA_OP_JZ_REL32:
    case EPA_OP_JNZ_REL32: {
          // Condition convention: read CSC r0 (w->vm.csc[0]).
          // 0=false, nonzero=true. Calls do NOT preserve r0.
          int cond = (w->vm.csc[0] != 0u);
          int take = (op == EPA_OP_JZ_REL32) ? (!cond) : (cond);
          if (!take) {
            eip->rel_pc = (uint32_t)(pc + need);
            return EPA_FLOW_YIELDED;
          }
          int32_t rel; read_rel32(code, pc, &rel);
          int64_t next = (int64_t)(pc + need);
          int64_t tgt  = next + (int64_t)rel;
          if (tgt < 0 || tgt > (int64_t)code_len) {
            snprintf(err, EPA_MAX_ERR, "%s out of range tgt=%lld len=%zu", def->name, (long long)tgt, code_len);
            return EPA_FLOW_ERR;
          }
          eip->rel_pc = (uint32_t)tgt;
          return EPA_FLOW_YIELDED;
        }

    case EPA_OP_JLZ_REL32: {
        int32_t v = (int32_t)w->vm.csc[0];
        if (v < 0) {
            int32_t rel; read_rel32(code, pc, &rel);
            eip->rel_pc = (uint32_t)((int64_t)(pc + need) + rel);
        } else {
            eip->rel_pc = pc + need;
        }
        return EPA_FLOW_YIELDED;
    }

    case EPA_OP_JGZ_REL32: {
        int32_t v = (int32_t)w->vm.csc[0];
        if (v > 0) {
            int32_t rel; read_rel32(code, pc, &rel);
            eip->rel_pc = (uint32_t)((int64_t)(pc + need) + rel);
        } else {
            eip->rel_pc = pc + need;
        }
        return EPA_FLOW_YIELDED;
    }

    case EPA_OP_MV: {
      uint8_t dst = code[pc + 2];
      uint8_t src = code[pc + 3];
      w->vm.csc[dst] = w->vm.csc[src];
      eip->rel_pc = pc + need;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_SET_R: {
        uint8_t reg = code[pc + 2];
        int32_t val;
        memcpy(&val, code + pc + 3, sizeof(int32_t));

        if (reg >= EPA_VM_REGS_MAX) {
            snprintf(err, EPA_MAX_ERR, "SET_R invalid register");
            return EPA_FLOW_ERR;
        }

        w->vm.csc[reg] = val;

        eip->rel_pc = pc + need;
        return EPA_FLOW_YIELDED;
    }

    case EPA_OP_INC: {
        uint8_t reg = code[pc + 2];

        if (reg >= EPA_VM_REGS_MAX) {
            snprintf(err, EPA_MAX_ERR, "INC invalid register");
            return EPA_FLOW_ERR;
        }

        w->vm.csc[reg] += 1;

        eip->rel_pc = pc + need;
        return EPA_FLOW_YIELDED;
    }

    case EPA_OP_DEC: {
        uint8_t reg = code[pc + 2];

        if (reg >= EPA_VM_REGS_MAX) {
            snprintf(err, EPA_MAX_ERR, "DEC invalid register");
            return EPA_FLOW_ERR;
        }

        w->vm.csc[reg] -= 1;

        eip->rel_pc = pc + need;
        return EPA_FLOW_YIELDED;
    }

    case EPA_OP_RLB_MOV1: {
      uint8_t reg    = code[pc + 2];
      uint8_t lb_reg = code[pc + 3];

      uint32_t off = w->vm.csc[lb_reg];
      if (off >= w->vm.lbytes_top) {
        snprintf(err, EPA_MAX_ERR, "RLB_MOV1: off out of range (%u)", off);
        return EPA_FLOW_ERR;
      }

      w->vm.lbytes[off] = (uint8_t)(w->vm.csc[reg] & 0xFF);
      eip->rel_pc = pc + need;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_LBR_MOV1: {
      uint8_t reg    = code[pc + 2];
      uint8_t lb_reg = code[pc + 3];

      uint32_t off = w->vm.csc[lb_reg];
      if (off >= w->vm.lbytes_top) {
        snprintf(err, EPA_MAX_ERR, "LBR_MOV1: off out of range (%u)", off);
        return EPA_FLOW_ERR;
      }

      w->vm.csc[reg] = (uint32_t)w->vm.lbytes[off];
      eip->rel_pc = pc + need;
      return EPA_FLOW_YIELDED;
    }


// ---- Common VM ops (backend-independent) ----
case EPA_OP_PUSH_I32: {
  int32_t v = (int32_t)EPA_READ_U32_LE(code, pc + 2);
  if (!epa_stack_push(st, (uint32_t)v)) { snprintf(err, EPA_MAX_ERR, "PUSH_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_PUSH_R: {
  uint8_t ridx = code[pc + 2];
  if (ridx >= EPA_VM_REGS_MAX) { snprintf(err, EPA_MAX_ERR, "PUSH_R bad reg %u", (unsigned)ridx); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, (uint32_t)w->vm.csc[ridx])) { snprintf(err, EPA_MAX_ERR, "PUSH_R: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_POP_R: {
  uint8_t ridx = code[pc + 2];
  if (ridx >= EPA_VM_REGS_MAX) { snprintf(err, EPA_MAX_ERR, "POP_R bad reg %u", (unsigned)ridx); return EPA_FLOW_ERR; }
  uint32_t u = 0;
  if (!epa_stack_pop(st, &u)) { snprintf(err, EPA_MAX_ERR, "POP_R: stack underflow"); return EPA_FLOW_ERR; }
  w->vm.csc[ridx] = u;
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_ADD_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "ADD_I32: stack underflow"); return EPA_FLOW_ERR; }
  int32_t a = (int32_t)ua, b = (int32_t)ub;
  if (!epa_stack_push(st, (uint32_t)(a + b))) { snprintf(err, EPA_MAX_ERR, "ADD_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_SUB_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "SUB_I32: stack underflow"); return EPA_FLOW_ERR; }
  int32_t a = (int32_t)ua, b = (int32_t)ub;
  if (!epa_stack_push(st, (uint32_t)(a - b))) { snprintf(err, EPA_MAX_ERR, "SUB_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_MUL_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "MUL_I32: stack underflow"); return EPA_FLOW_ERR; }
  int32_t a = (int32_t)ua, b = (int32_t)ub;
  if (!epa_stack_push(st, (uint32_t)(a * b))) { snprintf(err, EPA_MAX_ERR, "MUL_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_LT_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "LT_I32: stack underflow"); return EPA_FLOW_ERR; }
  int32_t a = (int32_t)ua, b = (int32_t)ub;
  w->vm.csc[0] = (uint32_t)((a < b) ? 1u : 0u);
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_STORE_L: {
  uint8_t idx = code[pc + 2];
  if (idx >= EPA_VM_LOCALS_MAX) { snprintf(err, EPA_MAX_ERR, "STORE_L idx out of range: %u", (unsigned)idx); return EPA_FLOW_ERR; }
  uint32_t u = 0;
  if (!epa_stack_pop(st, &u)) { snprintf(err, EPA_MAX_ERR, "STORE_L: stack underflow"); return EPA_FLOW_ERR; }
  w->vm.locals[idx] = (int32_t)u;
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_LOAD_L: {
  uint8_t idx = code[pc + 2];
  if (idx >= EPA_VM_LOCALS_MAX) { snprintf(err, EPA_MAX_ERR, "LOAD_L idx out of range: %u", (unsigned)idx); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, (uint32_t)w->vm.locals[idx])) { snprintf(err, EPA_MAX_ERR, "LOAD_L: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

// ---- Bulk transfers (ring <-> locals) ----
case EPA_OP_KERNEL_TRX_IN_L: {
  if (w->id != 0) { snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_IN_L only valid in kernel"); return EPA_FLOW_ERR; }
  if (!ctx->hooks.get_worker) { snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_IN_L: get_worker hook not installed"); return EPA_FLOW_ERR; }
  uint8_t wid8 = code[pc + 2];
  uint32_t laddr = EPA_READ_U32_LE(code, pc + 3);
  uint16_t len = (uint16_t)EPA_READ_U16_LE(code, pc + 7);
  if (laddr > EPA_VM_LOCALS_MAX || (uint64_t)laddr + (uint64_t)len > (uint64_t)EPA_VM_LOCALS_MAX) {
    snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_IN_L locals range out of bounds laddr=%u len=%u", (unsigned)laddr, (unsigned)len);
    return EPA_FLOW_ERR;
  }
  EpaWorkerState *src = ctx->hooks.get_worker(ctx->hooks_user, wid8);
  if (!src) { snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_IN_L invalid worker %u", (unsigned)wid8); return EPA_FLOW_ERR; }
  if (epa_ring_count(&src->outq) < (uint32_t)len) {
    snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_IN_L not enough outq data worker=%u need=%u have=%u",
             (unsigned)wid8, (unsigned)len, (unsigned)epa_ring_count(&src->outq));
    return EPA_FLOW_ERR;
  }
  for (uint16_t i = 0; i < len; i++) {
    uint32_t v = 0;
    if (!epa_ring_pop(&src->outq, &v)) { snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_IN_L outq pop failed"); return EPA_FLOW_ERR; }
    w->vm.locals[laddr + i] = (int32_t)v;
  }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_KERNEL_TRX_OUT_L: {
  if (w->id != 0) { snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_OUT_L only valid in kernel"); return EPA_FLOW_ERR; }
  if (!ctx->hooks.get_worker) { snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_OUT_L: get_worker hook not installed"); return EPA_FLOW_ERR; }
  uint8_t wid8 = code[pc + 2];
  uint32_t laddr = EPA_READ_U32_LE(code, pc + 3);
  uint16_t len = (uint16_t)EPA_READ_U16_LE(code, pc + 7);
  if (laddr > EPA_VM_LOCALS_MAX || (uint64_t)laddr + (uint64_t)len > (uint64_t)EPA_VM_LOCALS_MAX) {
    snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_OUT_L locals range out of bounds laddr=%u len=%u", (unsigned)laddr, (unsigned)len);
    return EPA_FLOW_ERR;
  }
  EpaWorkerState *dst = ctx->hooks.get_worker(ctx->hooks_user, wid8);
  if (!dst) { snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_OUT_L invalid worker %u", (unsigned)wid8); return EPA_FLOW_ERR; }
  if (epa_ring_space(&dst->inq) < (uint32_t)len) {
    snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_OUT_L not enough inq space worker=%u need=%u have=%u",
             (unsigned)wid8, (unsigned)len, (unsigned)epa_ring_space(&dst->inq));
    return EPA_FLOW_ERR;
  }
  for (uint16_t i = 0; i < len; i++) {
    uint32_t v = (uint32_t)w->vm.locals[laddr + i];
    if (!epa_ring_push(&dst->inq, v, 0, err)) return EPA_FLOW_ERR;
  }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_WORKER_TRX_IN_L: {
  if (w->id == 0) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX_IN_L only valid in workers"); return EPA_FLOW_ERR; }
  uint32_t laddr = EPA_READ_U32_LE(code, pc + 2);
  uint16_t len = (uint16_t)EPA_READ_U16_LE(code, pc + 6);
  if (laddr > EPA_VM_LOCALS_MAX || (uint64_t)laddr + (uint64_t)len > (uint64_t)EPA_VM_LOCALS_MAX) {
    snprintf(err, EPA_MAX_ERR, "WORKER_TRX_IN_L locals range out of bounds laddr=%u len=%u", (unsigned)laddr, (unsigned)len);
    return EPA_FLOW_ERR;
  }
  if (epa_ring_count(&w->inq) < (uint32_t)len) {
    snprintf(err, EPA_MAX_ERR, "WORKER_TRX_IN_L not enough inq data need=%u have=%u",
             (unsigned)len, (unsigned)epa_ring_count(&w->inq));
    return EPA_FLOW_ERR;
  }
  for (uint16_t i = 0; i < len; i++) {
    uint32_t v = 0;
    if (!epa_ring_pop(&w->inq, &v)) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX_IN_L inq pop failed"); return EPA_FLOW_ERR; }
    w->vm.locals[laddr + i] = (int32_t)v;
  }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_WORKER_TRX_OUT_L: {
  if (w->id == 0) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX_OUT_L only valid in workers"); return EPA_FLOW_ERR; }
  uint32_t laddr = EPA_READ_U32_LE(code, pc + 2);
  uint16_t len = (uint16_t)EPA_READ_U16_LE(code, pc + 6);
  if (laddr > EPA_VM_LOCALS_MAX || (uint64_t)laddr + (uint64_t)len > (uint64_t)EPA_VM_LOCALS_MAX) {
    snprintf(err, EPA_MAX_ERR, "WORKER_TRX_OUT_L locals range out of bounds laddr=%u len=%u", (unsigned)laddr, (unsigned)len);
    return EPA_FLOW_ERR;
  }
  if (epa_ring_space(&w->outq) < (uint32_t)len) {
    snprintf(err, EPA_MAX_ERR, "WORKER_TRX_OUT_L not enough outq space need=%u have=%u",
             (unsigned)len, (unsigned)epa_ring_space(&w->outq));
    return EPA_FLOW_ERR;
  }
  for (uint16_t i = 0; i < len; i++) {
    uint32_t v = (uint32_t)w->vm.locals[laddr + i];
    if (!epa_ring_push(&w->outq, v, 0, err)) return EPA_FLOW_ERR;
  }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_WORKER_TRX_IN_R: {
  if (w->id == 0) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX_IN_L only valid in workers"); return EPA_FLOW_ERR; }
  if (epa_ring_count(&w->inq) < 1) {
    snprintf(err, EPA_MAX_ERR, "WORKER_TRX_IN_L not enough inq data need=%u have=%u",
             4, (unsigned)epa_ring_count(&w->inq));
    return EPA_FLOW_ERR;
  }
  uint8_t r = code[pc + 2];
  uint32_t v;
  if (!epa_ring_pop(&w->inq, &v)) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX_IN_L inq pop failed"); return EPA_FLOW_ERR; }
  w->vm.csc[r] = (int32_t)v;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_WORKER_TRX_OUT_R: {
  if (w->id == 0) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX_OUT_L only valid in workers"); return EPA_FLOW_ERR; }
  if (epa_ring_space(&w->outq) < 1) {
    snprintf(err, EPA_MAX_ERR, "WORKER_TRX_OUT_L not enough outq space need=%u have=%u",
             4, (unsigned)epa_ring_space(&w->outq));
    return EPA_FLOW_ERR;
  }
  uint8_t r = code[pc + 2];
  if (!epa_ring_push(&w->outq, r, 0, err)) return EPA_FLOW_ERR;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_WORKER_TRX: {
  // kernel-only: transfer len words from src worker OUTQ to dst worker INQ.
  // src_laddr/dst_laddr refer to kernel locals slots that contain src_wid/dst_wid.
  if (w->id != 0) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX only valid in kernel"); return EPA_FLOW_ERR; }
  if (!ctx->hooks.get_worker) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX: get_worker hook not installed"); return EPA_FLOW_ERR; }

  uint32_t src_laddr = EPA_READ_U32_LE(code, pc + 2);
  uint32_t dst_laddr = EPA_READ_U32_LE(code, pc + 6);
  uint16_t len = (uint16_t)EPA_READ_U16_LE(code, pc + 10);

  if (src_laddr >= EPA_VM_LOCALS_MAX || dst_laddr >= EPA_VM_LOCALS_MAX) {
    snprintf(err, EPA_MAX_ERR, "WORKER_TRX locals addr out of range src=%u dst=%u", (unsigned)src_laddr, (unsigned)dst_laddr);
    return EPA_FLOW_ERR;
  }

  uint32_t src_wid = (uint32_t)w->vm.locals[src_laddr];
  uint32_t dst_wid = (uint32_t)w->vm.locals[dst_laddr];

  if (src_wid > 255u || dst_wid > 255u) {
    snprintf(err, EPA_MAX_ERR, "WORKER_TRX invalid wid src=%u dst=%u", (unsigned)src_wid, (unsigned)dst_wid);
    return EPA_FLOW_ERR;
  }

  EpaWorkerState *src = ctx->hooks.get_worker(ctx->hooks_user, (uint8_t)src_wid);
  EpaWorkerState *dst = ctx->hooks.get_worker(ctx->hooks_user, (uint8_t)dst_wid);
  if (!src || !dst) {
    snprintf(err, EPA_MAX_ERR, "WORKER_TRX missing worker src=%u dst=%u", (unsigned)src_wid, (unsigned)dst_wid);
    return EPA_FLOW_ERR;
  }

  if (epa_ring_count(&src->outq) < (uint32_t)len) {
    snprintf(err, EPA_MAX_ERR, "WORKER_TRX not enough src outq data need=%u have=%u",
             (unsigned)len, (unsigned)epa_ring_count(&src->outq));
    return EPA_FLOW_ERR;
  }
  if (epa_ring_space(&dst->inq) < (uint32_t)len) {
    snprintf(err, EPA_MAX_ERR, "WORKER_TRX not enough dst inq space need=%u have=%u",
             (unsigned)len, (unsigned)epa_ring_space(&dst->inq));
    return EPA_FLOW_ERR;
  }

  for (uint16_t i = 0; i < len; i++) {
    uint32_t v = 0;
    if (!epa_ring_pop(&src->outq, &v)) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX src pop failed"); return EPA_FLOW_ERR; }
    if (!epa_ring_push(&dst->inq, v, 0, err)) return EPA_FLOW_ERR;
  }

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_CALL: {
  // CALL <func_id:u32>
  // encoding: [u16 op][u32 func_id]
  uint32_t func_id = EPA_READ_U32_LE(code, pc + 2);

  uint16_t func_index = 0;
  uint16_t frame_words = 0;
  if (!epa_prog_find_func(ctx->prog, func_id, &func_index, &frame_words)) {
    snprintf(err, EPA_MAX_ERR, "CALL: unknown func_id=%u", (unsigned)func_id);
    return EPA_FLOW_ERR;
  }

  // Save return point (next instruction in current block)
  EpaEip ret = *eip;
  ret.rel_pc = (uint32_t)(pc + need);

  // Push return frame onto VM stack (your current design already supports this)
  // We store frame_words in the "argc" slot for now (same u16 carrier).
  if (!epa_flow_push_ret_frame(st, &ret, frame_words, err)) {
    return EPA_FLOW_ERR;
  }

  // Jump into function (rel_pc resets to 0)
  eip->block_type = EPA_BLOCK_FUNC;
  eip->block_id   = func_index;
  eip->rel_pc     = 0;

  return EPA_FLOW_YIELDED;
}

case EPA_OP_RET: {
  // RET (no params)
  EpaEip ret;
  uint16_t frame_words = 0;
  if (!epa_flow_pop_ret_frame(st, &ret, &frame_words, err)) {
    // err already set by helper
    return EPA_FLOW_ERR;
  }

  *eip = ret;
  return EPA_FLOW_YIELDED;
}

case EPA_OP_G_ALLOC: {
  // in: r0=type, r1=size_bytes
  // out: r0=idx, r1=gen
  uint32_t caller = w->id;
  uint32_t type_u = w->vm.csc[0];
  uint32_t size_b = w->vm.csc[1];

  epa_ghs_handle_t h = 0;
  epa_ghs_err_t ge = epa_ghs_alloc(k->impl.ghs, (epa_ghs_type_t)type_u, caller, size_b, &h);
  if (ge != EPA_GHS_OK) {
    snprintf(err, EPA_MAX_ERR, "G_ALLOC failed (type=%u size=%u) err=%d", type_u, size_b, ge);
    return 0; // or your fault path
  }

  w->vm.csc[0] = epa_ghs_handle_index(h);
  w->vm.csc[1] = epa_ghs_handle_gen(h);

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_G_FREE: {
  uint32_t caller = w->id;
  epa_ghs_handle_t h = epa_h_from_regs(w->vm.csc[0], w->vm.csc[1]);

  if (!epa_ghs_require_owner(k->impl.ghs, h, caller)) return 0;

  epa_ghs_err_t ge = epa_ghs_free(k->impl.ghs, h);
  if (ge != EPA_GHS_OK) {
    snprintf(err, EPA_MAX_ERR, "G_FREE failed err=%d", ge);
    return 0;
  }
  w->vm.csc[0] = 0;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_G_XFER: {
    uint32_t caller    = w->id;
    uint32_t new_owner = w->vm.csc[2];
    uint32_t n         = 1;

    if (n == 0) {
        // nothing to do; still yield to preserve scheduling semantics if you want
        eip->rel_pc = (uint32_t)(pc + need);
        return EPA_FLOW_YIELDED;
    }

    // Need 2 u32 per handle on stack
    if (w->vm.stack.sp < (size_t)(2u * n)) {
        snprintf(err, EPA_MAX_ERR, "G_XFER failed: stack underflow (need %u u32, have %zu)",
                 2u * n, w->vm.stack.sp);
        return 0;
    }

    // Build notification payload as packed 64-bit GHS handles.
    size_t bytes = (size_t)n * sizeof(uint64_t);
    uint64_t *msg = (uint64_t*)malloc(bytes);
    if (!msg) {
        snprintf(err, EPA_MAX_ERR, "G_XFER failed: OOM building notify payload");
        return 0;
    }

    // Pop handles from stack, transfer each one, and write into payload.
    // IMPORTANT: stack LIFO reverses order; if you care about preserving order,
    // you can fill msg backwards or use a temp array.
    for (uint32_t i = 0; i < n; i++) {
        uint32_t hi = 0, lo = 0;

        // If you pushed [lo][hi], you must pop hi then lo:
        if (!epa_stack_pop(&w->vm.stack, &hi) || !epa_stack_pop(&w->vm.stack, &lo)) {
            free(msg);
            snprintf(err, EPA_MAX_ERR, "G_XFER failed: stack pop failed");
            return 0;
        }

        epa_ghs_handle_t h = epa_h_from_regs(lo, hi);

        if (!epa_ghs_require_owner(k->impl.ghs, h, caller)) {
            free(msg);
            snprintf(err, EPA_MAX_ERR, "G_XFER failed: not owner of handle");
            return 0;
        }

        epa_ghs_err_t ge = epa_ghs_transfer(k->impl.ghs, h, new_owner);
        if (ge != EPA_GHS_OK) {
            free(msg);
            snprintf(err, EPA_MAX_ERR, "G_XFER failed err=%d", ge);
            return 0;
        }

        msg[i] = h;
    }

    // Notify receiver (kernel-owned ingress queue -> worker wakes on it)
    if (!epa_kernel_deliver_ghs_handles(k, new_owner, msg, n, err)) {
        free(msg);
        snprintf(err, EPA_MAX_ERR, "G_XFER failed: ingress queue full for wid=%u", new_owner);
        return 0;
    }

    free(msg);

    // Clear args for safety
    w->vm.csc[0] = 0;
    w->vm.csc[1] = 0;
    w->vm.csc[3] = 0;

    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
}

case EPA_OP_G_XFERX: {
    uint32_t caller    = w->id;
    uint32_t new_owner = w->vm.csc[2];

    uint32_t count = EPA_READ_U32_LE(code, pc + 2);

    if (count == 0) {
        eip->rel_pc = (uint32_t)(pc + need);
        return EPA_FLOW_YIELDED;
    }

    // Need 2 u32 per handle on stack.
    if (w->vm.stack.sp < (size_t)(2u * count)) {
        snprintf(err, EPA_MAX_ERR,
                 "G_XFERX failed: stack underflow need=%u have=%zu",
                 2u * count, w->vm.stack.sp);
        return 0;
    }

    // Build notify payload as packed 64-bit GHS handles.
    size_t bytes = (size_t)count * sizeof(uint64_t);
    uint64_t *msg  = (uint64_t*)malloc(bytes);
    if (!msg) {
        snprintf(err, EPA_MAX_ERR, "G_XFERX failed: OOM");
        return 0;
    }

    // Because handles were pushed in reverse, popping yields forward order.
    for (uint32_t i = 0; i < count; i++) {
        uint32_t hi, lo;

        // If you push [lo][hi], you pop hi then lo:
        if (!epa_stack_pop(&w->vm.stack, &hi) ||
            !epa_stack_pop(&w->vm.stack, &lo)) {
            free(msg);
            snprintf(err, EPA_MAX_ERR, "G_XFERX failed: stack pop");
            return 0;
        }

        epa_ghs_handle_t h = epa_h_from_regs(lo, hi);

        if (!epa_ghs_require_owner(k->impl.ghs, h, caller)) {
            free(msg);
            snprintf(err, EPA_MAX_ERR, "G_XFERX failed: not owner");
            return 0;
        }

        epa_ghs_err_t ge = epa_ghs_transfer(k->impl.ghs, h, new_owner);
        if (ge != EPA_GHS_OK) {
            free(msg);
            snprintf(err, EPA_MAX_ERR, "G_XFERX failed err=%d", ge);
            return 0;
        }

        msg[i] = h;
    }

    // Ring notify receiver
    if (!epa_kernel_deliver_ghs_handles(k, new_owner, msg, count, err)) {
        free(msg);
        snprintf(err, EPA_MAX_ERR, "G_XFERX failed: ingress full wid=%u", new_owner);
        return 0;
    }

    free(msg);

    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
}


case EPA_OP_G_RESIZE: {
  uint32_t caller = w->id;
  uint32_t new_size = w->vm.csc[2];
  epa_ghs_handle_t h = epa_h_from_regs(w->vm.csc[0], w->vm.csc[1]);

  if (!epa_ghs_require_owner(k->impl.ghs, h, caller)) return 0;

  epa_ghs_err_t ge = epa_ghs_resize(k->impl.ghs, h, new_size);
  if (ge != EPA_GHS_OK) {
    snprintf(err, EPA_MAX_ERR, "G_RESIZE failed new_size=%u err=%d", new_size, ge);
    return 0;
  }
  w->vm.csc[0] = 0;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_G_PTR: {
  uint32_t caller = w->id;
  epa_ghs_handle_t h = epa_h_from_regs(w->vm.csc[0], w->vm.csc[1]);

  if (!epa_ghs_require_owner(k->impl.ghs, h, caller)) return 0;

  void* p = NULL;
  epa_ghs_err_t ge = epa_ghs_get_ptr(k->impl.ghs, h, &p);
  if (ge != EPA_GHS_OK || !p) {
    snprintf(err, EPA_MAX_ERR, "G_PTR failed err=%d", ge);
    return 0;
  }

  uintptr_t up = (uintptr_t)p;
  w->vm.csc[0] = (uint32_t)(up & 0xFFFFFFFFu);
  w->vm.csc[1] = (uint32_t)((up >> 32) & 0xFFFFFFFFu);

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_G_META: {
  epa_ghs_handle_t h = epa_h_from_regs(w->vm.csc[0], w->vm.csc[1]);

  epa_ghs_meta_t m;
  epa_ghs_err_t ge = epa_ghs_get_meta(k->impl.ghs, h, &m);
  if (ge != EPA_GHS_OK) {
    snprintf(err, EPA_MAX_ERR, "G_META failed err=%d", ge);
    return 0;
  }

  w->vm.csc[0] = m.owner;
  w->vm.csc[1] = (uint32_t)m.type;
  w->vm.csc[2] = m.size_bytes;
  w->vm.csc[3] = m.capacity;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_GR_MOV4: {
	uint32_t rid = code[pc + 2];
	epa_ghs_handle_t h = ((uint64_t)w->vm.csc[1] << 32) | (uint64_t)w->vm.csc[0];
	epa_ghs_read_bytes(k->impl.ghs, h, w->vm.csc[2], &w->vm.csc[rid], 4);

	eip->rel_pc = (uint32_t)(pc + need);
	return EPA_FLOW_YIELDED;
}

case EPA_OP_LOAD_CONST: {
  uint32_t id = EPA_READ_U32_LE(code, pc + 2);

  uint32_t a = 0, b = 0;
  uint32_t kind = EPA_CONST_NONE;
  uint32_t flags = 0;

  const EpaConst *c = prog_find_const(ctx->prog, id);
  if (c) {
    a = c->a;
    b = c->b;
    kind = c->kind;
    flags = c->flags;
  }

  w->vm.csc[0] = a;      // r0
  w->vm.csc[1] = b;      // r1
  w->vm.csc[2] = kind;   // r2
  w->vm.csc[3] = flags;  // r3 (optional use later)

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_L_RESET: {
  // Reset byte arena head (keep capacity).
  w->vm.lbytes_top = 0;
  w->vm.csc[0] = 0;
  w->vm.csc[1] = 0;
  w->vm.csc[2] = 1;   // ok
  w->vm.csc[3] = 0;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_L_ALLOC: {
  // in:  r0 = size_bytes
  // out: r0 = off, r1 = size, r2 = ok(1/0), r3 = 0

  uint32_t sz = (uint32_t)w->vm.csc[0];

  // Define behavior for sz==0: succeed with off=current top and len=0
  if (!w->vm.lbytes || w->vm.lbytes_cap == 0) {
    w->vm.csc[0] = 0;
    w->vm.csc[1] = 0;
    w->vm.csc[2] = 0;
    w->vm.csc[3] = 0;
    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
  }

  uint32_t top = w->vm.lbytes_top;

  // Align to 4 for sanity (optional but recommended)
  const uint32_t align = 4;
  uint32_t aligned_top = (top + (align - 1u)) & ~(align - 1u);

  // Overflow-safe bounds check
  if (sz > w->vm.lbytes_cap || aligned_top > w->vm.lbytes_cap - sz) {
    w->vm.csc[0] = 0;
    w->vm.csc[1] = 0;
    w->vm.csc[2] = 0;   // fail
    w->vm.csc[3] = 0;
    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
  }

  w->vm.lbytes_top = aligned_top + sz;

  w->vm.csc[0] = aligned_top; // off
  w->vm.csc[1] = sz;          // size
  w->vm.csc[2] = 1;           // ok
  w->vm.csc[3] = 0;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_FMT: {
  uint16_t argc = code[pc + 2];

  uint32_t tmpl_off  = (uint32_t)w->vm.csc[0];
  uint32_t tmpl_len  = (uint32_t)w->vm.csc[1];
  uint32_t tmpl_kind = (uint32_t)w->vm.csc[2];

  // Pop args (reverse because stack)
  uint32_t args[256];
  if (argc > 255) argc = 255;

  for (uint32_t i = 0; i < argc; i++) {
    uint32_t v = 0;
    if (!epa_stack_pop(st, &v)) {
      // stack underflow -> treat missing as 0
      v = 0;
    }
    args[argc - 1 - i] = v;
  }

  const uint8_t *tp = NULL;
  uint32_t tlen = 0;
  if (!epa_resolve_string(ctx->prog, w, tmpl_kind, tmpl_off, tmpl_len, &tp, &tlen)) {
    // Not a valid string template -> return empty tmp string
    w->vm.csc[0] = 0; w->vm.csc[1] = 0; w->vm.csc[2] = EPA_CONST_TMP_STR; w->vm.csc[3] = 0;
    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
  }

  // Scratch buffer (keeps v1 simple). If you want unbounded later,
  // we can do a 2-pass length calc then L_ALLOC exact.
  char scratch[4096];
  uint32_t si = 0;
  uint32_t ai = 0;

  for (uint32_t i = 0; i < tlen && si < (uint32_t)sizeof(scratch) - 1u; ) {
    uint8_t c = tp[i++];

    if (c == '{' && i < tlen) {
      uint8_t n = tp[i];

      if (n == '{') { // "{{" -> "{"
        scratch[si++] = '{';
        i++;
        continue;
      }

      // placeholder must be "{x}" style
      if ((n == 'd' || n == 'u' || n == 'x' || n == 'c') && (i + 1 < tlen) && tp[i + 1] == '}') {
        uint32_t v = (ai < argc) ? args[ai++] : 0;
        if (n == 'd') {
          si += epa_i32_to_dec(scratch + si, (uint32_t)sizeof(scratch) - 1u - si, (int32_t)v);
        } else if (n == 'u') {
          si += epa_u32_to_dec(scratch + si, (uint32_t)sizeof(scratch) - 1u - si, v);
        } else if (n == 'x') {
          si += epa_u32_to_hex(scratch + si, (uint32_t)sizeof(scratch) - 1u - si, v);
        } else { // 'c'
          scratch[si++] = (char)(v & 0xFFu);
        }
        i += 2; // skip type + '}'
        continue;
      }

      // malformed "{...": treat '{' literally
      scratch[si++] = '{';
      continue;
    }

    if (c == '}' && i < tlen && tp[i] == '}') { // "}}" -> "}"
      scratch[si++] = '}';
      i++;
      continue;
    }

    scratch[si++] = (char)c;
  }

  uint32_t out_len = si;

  // Allocate from worker local byte heap (same behavior as L_ALLOC)
  if (!w->vm.lbytes || w->vm.lbytes_cap == 0) {
    w->vm.csc[0] = 0; w->vm.csc[1] = 0; w->vm.csc[2] = EPA_CONST_TMP_STR; w->vm.csc[3] = 0;
    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
  }

  const uint32_t align = 4;
  uint32_t top = w->vm.lbytes_top;
  uint32_t off = (top + (align - 1u)) & ~(align - 1u);

  if (out_len > w->vm.lbytes_cap || off > w->vm.lbytes_cap - out_len) {
    // out of local heap space -> return empty
    w->vm.csc[0] = 0; w->vm.csc[1] = 0; w->vm.csc[2] = EPA_CONST_TMP_STR; w->vm.csc[3] = 0;
    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
  }

  memcpy(w->vm.lbytes + off, scratch, out_len);
  w->vm.lbytes_top = off + out_len;

  // Return tmp string ref
  w->vm.csc[0] = off;
  w->vm.csc[1] = out_len;
  w->vm.csc[2] = EPA_CONST_TMP_STR;
  w->vm.csc[3] = 0;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_LOG: {
  uint32_t off  = (uint32_t)w->vm.csc[0];
  uint32_t len  = (uint32_t)w->vm.csc[1];
  uint32_t kind = (uint32_t)w->vm.csc[2];

  const uint8_t *ptr = NULL;
  uint32_t plen = 0;

  if (epa_resolve_string(ctx->prog, w, kind, off, len, &ptr, &plen)) {
    if (plen > 0 && ptr) {
      fwrite(ptr, 1, plen, stdout);
      fflush(stdout);
    }
  }
  // LOG must never fail, trap, or modify state

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_SM_PUT: {
  // SM_PUT:
  //   write u32 (little-endian) from r0 into signal mailbox at cursor r3,
  //   then r3 += 4
  //
  // Contract:
  //   r3 = byte offset cursor
  //   r0 = u32 value to write
  //
  // Notes:
  // - Bounds against mailbox capacity.
  // - Does NOT block; SIGNAL is the boundary that may block.
  EPA_REQUIRE_WORKER_ONLY(w, err, "EPA_OP_SM_PUT", EPA_NF_EXEC_ERR);

  uint32_t cur = (uint32_t)w->vm.csc[3];
  if (!w->signal_mailbox || ctx->prog->signal_mailbox_size[w->id] == 0) {
    snprintf(err, EPA_MAX_ERR, "SM_PUT: signal mailbox not configured");
    return EPA_FLOW_ERR;
  }
  if (cur > ctx->prog->signal_mailbox_size[w->id]|| (ctx->prog->signal_mailbox_size[w->id] - cur) < 4) {
    snprintf(err, EPA_MAX_ERR, "SM_PUT: out of bounds (cur=%u cap=%u)", (unsigned)cur, (unsigned)ctx->prog->signal_mailbox_size[w->id]);
    return EPA_FLOW_ERR;
  }

  uint32_t v = (uint32_t)w->vm.csc[0];
  uint8_t *dst = w->signal_mailbox + cur;

  // little-endian store
  dst[0] = (uint8_t)(v & 0xFFu);
  dst[1] = (uint8_t)((v >> 8) & 0xFFu);
  dst[2] = (uint8_t)((v >> 16) & 0xFFu);
  dst[3] = (uint8_t)((v >> 24) & 0xFFu);

  w->vm.csc[3] = (int32_t)(cur + 4);

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_AT: {
  // worker-only (kernel entry 0 must not launch AT)
  if (w->id == 0) {
    snprintf(err, EPA_MAX_ERR, "AT_PARALLEL only valid in workers");
    return EPA_FLOW_ERR;
  }

  // ABI: r0=sub_id, r1=ghs_handle_lo, r2=ghs_handle_hi, r3=thread_count
  uint32_t sub_id = w->vm.csc[0];
  epa_ghs_handle_t h = epa_h_from_regs(w->vm.csc[1], w->vm.csc[2]);
  uint32_t thread_count = w->vm.csc[3];

  if (!k || !k->impl.ghs) {
    snprintf(err, EPA_MAX_ERR, "AT_PARALLEL: kernel has no GHS");
    return EPA_FLOW_ERR;
  }

  if (w->at_running) {
    snprintf(err, EPA_MAX_ERR, "AT_PARALLEL: AT already running");
    return EPA_FLOW_ERR;
  }

  // Launch AT batch
  if (!epa_at_router_launch_parallel(k, w, sub_id, k->impl.ghs, h, thread_count, err)) {
    return EPA_FLOW_ERR;
  }

  // Advance PC and yield (AT runs async; WAIT_FOR_AT will join)
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}


    default:
      return EPA_FLOW_NOT_FLOW;
  }
}
