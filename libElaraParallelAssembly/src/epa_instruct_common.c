#include "epa_instruct_common.h"
#include "epa_program_desc.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "log.h"

#include "epa_opcode_helpers.h"

#include "memory/epa_ghs.h"
#include "epa_instruct_helpers.h"

#define EPA_CALL_MAGIC0 0xC011CA11u
#define EPA_CALL_MAGIC1 0xF00DF00Du

#include "epa_kernel.h"

int epa_kernel_deliver_ghs_handles_framed(EpaKernel *k,
                                          uint32_t dst_wid,
                                          const uint64_t *ghs_handles,
                                          uint32_t ghs_handle_count,
                                          uint64_t source_kernel_uid,
                                          uint32_t source_worker_id,
                                          char err[EPA_MAX_ERR]);

int epa_flow_push_ret_frame(EpaStack *st, const EpaEip *ret, uint16_t argc, char err[EPA_MAX_ERR]) {
  if (!epa_stack_push(st, EPA_CALL_MAGIC0) ||
      !epa_stack_push(st, (uint32_t)ret->block_type) ||
      !epa_stack_push(st, ret->block_id) ||
      !epa_stack_push(st, ret->rel_pc) ||
      !epa_stack_push(st, (uint32_t)argc) ||
      !epa_stack_push(st, EPA_CALL_MAGIC1)) {
    if (err) snprintf(err, EPA_MAX_ERR, "stack overflow pushing call frame");
    return 0;
  }
  return 1;
}

int epa_flow_pop_ret_frame(EpaStack *st, EpaEip *out_ret, uint16_t *out_argc, char err[EPA_MAX_ERR]) {
  uint32_t m1, argc_w, pc_w, block_id_w, block_type_w, m0;
  if (!epa_stack_pop(st, &m1) ||
      !epa_stack_pop(st, &argc_w) ||
      !epa_stack_pop(st, &pc_w) ||
      !epa_stack_pop(st, &block_id_w) ||
      !epa_stack_pop(st, &block_type_w) ||
      !epa_stack_pop(st, &m0)) {
    if (err) snprintf(err, EPA_MAX_ERR, "stack underflow popping call frame");
    return 0;
  }
  if (m0 != EPA_CALL_MAGIC0 || m1 != EPA_CALL_MAGIC1) {
    if (err) snprintf(err, EPA_MAX_ERR, "call frame magic mismatch (corrupt stack)");
    return 0;
  }
  out_ret->block_type = (uint8_t)(block_type_w & 0xFFu);
  out_ret->block_id   = block_id_w;
  out_ret->rel_pc     = pc_w;
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

static float epa_f32_from_bits(uint32_t bits) {
  float value = 0.0f;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

static uint32_t epa_f32_to_bits(float value) {
  uint32_t bits = 0;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static EpaDynamicPool *worker_dynamic_pool_by_id(EpaWorkerState *w, uint32_t pool_id) {
  if (!w || !w->dynamic_pools) return NULL;
  if (pool_id >= w->dynamic_pool_count) return NULL;
  return &w->dynamic_pools[pool_id];
}

static int worker_release_current_ghs_if_owned(EpaKernel *k, EpaWorkerState *w, char err[EPA_MAX_ERR]) {
  epa_ghs_meta_t meta;
  epa_ghs_err_t ge;
  if (err) err[0] = 0;
  if (!k || !k->impl.ghs || !w || !w->has_current_ghs) return 1;

  ge = epa_ghs_get_meta(k->impl.ghs, w->current_ghs, &meta);
  if (ge == EPA_GHS_OK && meta.owner == (uint32_t)w->id) {
    ge = epa_ghs_free(k->impl.ghs, w->current_ghs);
    if (ge != EPA_GHS_OK) {
      snprintf(err, EPA_MAX_ERR, "WAIT_FOR_DATA auto-free failed err=%d", ge);
      return 0;
    }
  }

  w->has_current_ghs = 0;
  w->current_ghs = 0;
  return 1;
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
  if (pc + EPA_OPCODE_BYTES > code_len) {
    snprintf(err, EPA_MAX_ERR, "EIP out of range pc=%zu len=%zu", pc, code_len);
    return EPA_FLOW_ERR;
  }

  uint8_t op = code[pc];
  const EpaOpcodeDef *def = epa_find_opcode(op);
  if (!def) {
    snprintf(err, EPA_MAX_ERR, "unknown opcode 0x%02x at pc=%zu", op, pc);
    return EPA_FLOW_ERR;
  }
  size_t need = EPA_OPCODE_BYTES + (size_t)def->param_len;
  if (pc + need > code_len) {
    snprintf(err, EPA_MAX_ERR, "truncated %s at pc=%zu", def->name, pc);
    return EPA_FLOW_ERR;
  }

  TRACE("[GL-NF] slot=%u %s[%u] pc=%zu op=0x%02x %s\n",
        (unsigned)w->id,
        (eip->block_type == EPA_BLOCK_ENTRY) ? "entry" :
        (eip->block_type == EPA_BLOCK_AT_ENTRY) ? "at_entry" : "func",
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
      if (ctx->hooks.on_wait_on_sync && !ctx->hooks.on_wait_on_sync(ctx->hooks_user, err)) return EPA_FLOW_ERR;
      if (w && w->blocked) {
        eip->rel_pc = (uint32_t)pc;
      } else {
        eip->rel_pc = (uint32_t)(pc + need);
      }
      return EPA_FLOW_YIELDED;

    case EPA_OP_ENTRY_EXEC: {
      uint8_t wid = code[pc + EPA_OPCODE_BYTES];
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_entry_exec && !ctx->hooks.on_entry_exec(ctx->hooks_user, wid, err)) return EPA_FLOW_ERR;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_ENTRY_HALT: {
      uint8_t wid = code[pc + EPA_OPCODE_BYTES];
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_entry_halt && !ctx->hooks.on_entry_halt(ctx->hooks_user, wid, err)) return EPA_FLOW_ERR;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_ENTRY_RETIRE: {
      uint8_t wid = code[pc + EPA_OPCODE_BYTES];
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_entry_retire && !ctx->hooks.on_entry_retire(ctx->hooks_user, wid, err)) return EPA_FLOW_ERR;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_KERNEL_RETIRE: {
      uint64_t kernel_uid = ((uint64_t)(uint32_t)w->vm.csc[1] << 32) | (uint64_t)(uint32_t)w->vm.csc[0];
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_kernel_retire && !ctx->hooks.on_kernel_retire(ctx->hooks_user, kernel_uid, err)) return EPA_FLOW_ERR;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_ENTRY_PRIVILEGE: {
      uint8_t wid = code[pc + EPA_OPCODE_BYTES];
      uint32_t privilege = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES + 1);
      if (!w || w->id != 0u) {
        snprintf(err, EPA_MAX_ERR, "ENTRY_PRIVILEGE only valid from kernel entry wid=0");
        return EPA_FLOW_ERR;
      }
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_entry_privilege && !ctx->hooks.on_entry_privilege(ctx->hooks_user, wid, privilege, err)) return EPA_FLOW_ERR;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_PRIVILEGE_LOCK: {
      if (!w || w->id != 0u) {
        snprintf(err, EPA_MAX_ERR, "PRIVILEGE_LOCK only valid from kernel entry wid=0");
        return EPA_FLOW_ERR;
      }
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_privilege_lock && !ctx->hooks.on_privilege_lock(ctx->hooks_user, err)) return EPA_FLOW_ERR;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_ACL: {
      uint8_t acl_kind = code[pc + EPA_OPCODE_BYTES];
      uint8_t local_wid = code[pc + EPA_OPCODE_BYTES + 1u];
      uint64_t target_uid = ((uint64_t)(uint32_t)w->vm.csc[1] << 32) | (uint64_t)(uint32_t)w->vm.csc[0];
      uint64_t remote_uid = ((uint64_t)(uint32_t)w->vm.csc[3] << 32) | (uint64_t)(uint32_t)w->vm.csc[2];
      eip->rel_pc = (uint32_t)(pc + need);
      switch (acl_kind) {
        case 1u:
          if (ctx->hooks.on_acl_grant && !ctx->hooks.on_acl_grant(ctx->hooks_user, (uint8_t)w->id, target_uid, remote_uid, local_wid, err)) return EPA_FLOW_ERR;
          break;
        case 2u:
          if (ctx->hooks.on_acl_revoke && !ctx->hooks.on_acl_revoke(ctx->hooks_user, (uint8_t)w->id, target_uid, remote_uid, local_wid, err)) return EPA_FLOW_ERR;
          break;
        case 3u:
          if (ctx->hooks.on_acl_revoke_all && !ctx->hooks.on_acl_revoke_all(ctx->hooks_user, (uint8_t)w->id, target_uid, remote_uid, err)) return EPA_FLOW_ERR;
          break;
        default:
          snprintf(err, EPA_MAX_ERR, "ACL unknown kind %u", (unsigned)acl_kind);
          return EPA_FLOW_ERR;
      }
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_PID: {
      uint8_t pid_kind = code[pc + EPA_OPCODE_BYTES];
      eip->rel_pc = (uint32_t)(pc + need);
      switch (pid_kind) {
        case 1u: {
          uint32_t pid = 0u;
          if (ctx->hooks.on_pid_self && !ctx->hooks.on_pid_self(ctx->hooks_user, (uint8_t)w->id, &pid, err)) return EPA_FLOW_ERR;
          w->vm.csc[0] = (int32_t)pid;
          w->vm.csc[1] = 0;
          w->vm.csc[2] = 0;
          w->vm.csc[3] = 1;
          break;
        }
        case 2u: {
          uint32_t pid = (uint32_t)w->vm.csc[0];
          if (ctx->hooks.on_pid_retire && !ctx->hooks.on_pid_retire(ctx->hooks_user, (uint8_t)w->id, pid, err)) return EPA_FLOW_ERR;
          break;
        }
        default:
          snprintf(err, EPA_MAX_ERR, "PID unknown kind %u", (unsigned)pid_kind);
          return EPA_FLOW_ERR;
      }
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_VM_STATE: {
      uint8_t selector = code[pc + EPA_OPCODE_BYTES];
      uint64_t u64 = 0u;
      uint32_t u32 = 0u;
      int ok = 1;
      switch (selector) {
        case EPA_VM_STATE_CURRENT_KERNEL_UID:
          u64 = w->current_kernel_uid;
          w->vm.csc[0] = (uint32_t)(u64 & 0xFFFFFFFFu);
          w->vm.csc[1] = (uint32_t)(u64 >> 32);
          break;
        case EPA_VM_STATE_CURRENT_WORKER_ID:
          u32 = w->current_worker_id;
          w->vm.csc[0] = u32;
          w->vm.csc[1] = 0u;
          break;
        case EPA_VM_STATE_SOURCE_KERNEL_UID:
          u64 = w->ingress_source_kernel_uid;
          w->vm.csc[0] = (uint32_t)(u64 & 0xFFFFFFFFu);
          w->vm.csc[1] = (uint32_t)(u64 >> 32);
          break;
        case EPA_VM_STATE_SOURCE_WORKER_ID:
          u32 = w->ingress_source_worker_id;
          w->vm.csc[0] = u32;
          w->vm.csc[1] = 0u;
          break;
        default:
          w->vm.csc[0] = 0u;
          w->vm.csc[1] = 0u;
          ok = 0;
          break;
      }
      w->vm.csc[2] = selector;
      w->vm.csc[3] = ok ? 1u : 0u;
      eip->rel_pc = (uint32_t)(pc + need);
      return EPA_FLOW_YIELDED;
    }

    // Interrupt / debug
    case EPA_OP_BREAK: {
      uint32_t code_u = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
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

    case EPA_OP_FAR_SIGNAL: {
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_far_signal) {
        if (!ctx->hooks.on_far_signal(ctx->hooks_user, (uint8_t)w->id, err)) {
          return EPA_FLOW_ERR;
        }
      }
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_HOST_SIGNAL: {
      eip->rel_pc = (uint32_t)(pc + need);
      if (ctx->hooks.on_host_signal) {
        if (!ctx->hooks.on_host_signal(ctx->hooks_user, (uint8_t)w->id, err)) {
          return EPA_FLOW_ERR;
        }
      }
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_REQUEST: {
      uint8_t request_kind = code[pc + EPA_OPCODE_BYTES];
      uint32_t descriptor_word_count;
      uint32_t *descriptor_words;
      uint32_t request_id = 0u;
      int submit_rc;
      size_t descriptor_start;

      if (request_kind == 1u) {
        eip->rel_pc = (uint32_t)(pc + need);
        if (ctx->hooks.on_request_threads) {
          if (!ctx->hooks.on_request_threads(ctx->hooks_user, (uint8_t)w->id, (uint32_t)w->vm.csc[0], err)) {
            return EPA_FLOW_ERR;
          }
        }
        return EPA_FLOW_YIELDED;
      }

      if (request_kind != 2u) {
        snprintf(err, EPA_MAX_ERR, "REQUEST unknown kind %u", (unsigned)request_kind);
        return EPA_FLOW_ERR;
      }

      if (!ctx->hooks.on_request_at) {
        snprintf(err, EPA_MAX_ERR, "REQUEST kind=2 AT hook not installed");
        return EPA_FLOW_ERR;
      }
      if (!st->words || st->sp < 1u) {
        snprintf(err, EPA_MAX_ERR, "REQUEST kind=2: stack underflow reading descriptor size");
        return EPA_FLOW_ERR;
      }

      descriptor_word_count = st->words[st->sp - 1u];
      if (descriptor_word_count < 6u) {
        snprintf(err, EPA_MAX_ERR, "REQUEST kind=2: descriptor too small (%u words)", (unsigned)descriptor_word_count);
        return EPA_FLOW_ERR;
      }
      if (descriptor_word_count > 1024u) {
        snprintf(err, EPA_MAX_ERR, "REQUEST kind=2: descriptor too large (%u words)", (unsigned)descriptor_word_count);
        return EPA_FLOW_ERR;
      }
      if (st->sp < (size_t)descriptor_word_count + 1u) {
        snprintf(err, EPA_MAX_ERR, "REQUEST kind=2: stack underflow need=%u have=%zu",
                 (unsigned)(descriptor_word_count + 1u), st->sp);
        return EPA_FLOW_ERR;
      }

      descriptor_start = st->sp - 1u - (size_t)descriptor_word_count;
      descriptor_words = (uint32_t*)malloc((size_t)descriptor_word_count * sizeof(uint32_t));
      if (!descriptor_words) {
        snprintf(err, EPA_MAX_ERR, "REQUEST kind=2: OOM copying descriptor");
        return EPA_FLOW_ERR;
      }
      memcpy(descriptor_words, st->words + descriptor_start, (size_t)descriptor_word_count * sizeof(uint32_t));

      submit_rc = ctx->hooks.on_request_at(ctx->hooks_user, (uint8_t)w->id, descriptor_words, descriptor_word_count, &request_id, err);
      if (submit_rc == 2) {
        if (err) err[0] = 0;
        free(descriptor_words);
        return EPA_FLOW_YIELDED;
      }
      if (!submit_rc) {
        free(descriptor_words);
        return EPA_FLOW_ERR;
      }

      free(descriptor_words);
      st->sp = descriptor_start;
      w->vm.csc[0] = (int32_t)request_id;
      w->vm.csc[1] = 1;
      eip->rel_pc = (uint32_t)(pc + need);
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_TRAP: {
      uint32_t code_u = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
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
      uint32_t code_u = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
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

      if (epa_ring_count(&w->inq) > 0u) {
        if (!epa_worker_round_enter(w, err)) {
          return EPA_FLOW_ERR;
        }
        w->waiting_for_data = 0;
        w->blocked = 0;
        eip->rel_pc = (uint32_t)(pc + need);
        return EPA_FLOW_YIELDED;
      }

      if (!worker_release_current_ghs_if_owned(k, w, err)) {
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
      if (pc + EPA_OPCODE_BYTES + 1 > (int)code_len) {
        snprintf(err, EPA_MAX_ERR, "DATA_READY truncated");
        return EPA_FLOW_ERR;
      }
      uint8_t wid = code[pc + EPA_OPCODE_BYTES];

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
      uint8_t dst = code[pc + EPA_OPCODE_BYTES];
      uint8_t src = code[pc + EPA_OPCODE_BYTES + 1];
      w->vm.csc[dst] = w->vm.csc[src];
      eip->rel_pc = pc + need;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_SET_R: {
        uint8_t reg = code[pc + EPA_OPCODE_BYTES];
        int32_t val;
        memcpy(&val, code + pc + EPA_OPCODE_BYTES + 1, sizeof(int32_t));

        if (reg >= EPA_VM_REGS_MAX) {
            snprintf(err, EPA_MAX_ERR, "SET_R invalid register");
            return EPA_FLOW_ERR;
        }

        w->vm.csc[reg] = val;

        eip->rel_pc = pc + need;
        return EPA_FLOW_YIELDED;
    }

    case EPA_OP_INC: {
        uint8_t reg = code[pc + EPA_OPCODE_BYTES];

        if (reg >= EPA_VM_REGS_MAX) {
            snprintf(err, EPA_MAX_ERR, "INC invalid register");
            return EPA_FLOW_ERR;
        }

        w->vm.csc[reg] += 1;

        eip->rel_pc = pc + need;
        return EPA_FLOW_YIELDED;
    }

    case EPA_OP_DEC: {
        uint8_t reg = code[pc + EPA_OPCODE_BYTES];

        if (reg >= EPA_VM_REGS_MAX) {
            snprintf(err, EPA_MAX_ERR, "DEC invalid register");
            return EPA_FLOW_ERR;
        }

        w->vm.csc[reg] -= 1;

        eip->rel_pc = pc + need;
        return EPA_FLOW_YIELDED;
    }

    case EPA_OP_RLB_MOV1: {
      uint8_t reg    = code[pc + EPA_OPCODE_BYTES];
      uint8_t lb_reg = code[pc + EPA_OPCODE_BYTES + 1];

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
      uint8_t reg    = code[pc + EPA_OPCODE_BYTES];
      uint8_t lb_reg = code[pc + EPA_OPCODE_BYTES + 1];

      uint32_t off = w->vm.csc[lb_reg];
      if (off >= w->vm.lbytes_top) {
        snprintf(err, EPA_MAX_ERR, "LBR_MOV1: off out of range (%u)", off);
        return EPA_FLOW_ERR;
      }

      w->vm.csc[reg] = (uint32_t)w->vm.lbytes[off];
      eip->rel_pc = pc + need;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_RLB_MOV4: {
      uint8_t reg    = code[pc + EPA_OPCODE_BYTES];
      uint8_t lb_reg = code[pc + EPA_OPCODE_BYTES + 1];
      uint32_t off   = (uint32_t)w->vm.csc[lb_reg];
      uint32_t val   = (uint32_t)w->vm.csc[reg];
      if (!w->vm.lbytes || off + 3u >= w->vm.lbytes_top) {
        snprintf(err, EPA_MAX_ERR, "RLB_MOV4: off out of range (%u)", (unsigned)off);
        return EPA_FLOW_ERR;
      }
      w->vm.lbytes[off + 0] = (uint8_t)(val       & 0xFFu);
      w->vm.lbytes[off + 1] = (uint8_t)((val >> 8) & 0xFFu);
      w->vm.lbytes[off + 2] = (uint8_t)((val >> 16) & 0xFFu);
      w->vm.lbytes[off + 3] = (uint8_t)((val >> 24) & 0xFFu);
      eip->rel_pc = pc + need;
      return EPA_FLOW_YIELDED;
    }

    case EPA_OP_LBR_MOV4: {
      uint8_t reg    = code[pc + EPA_OPCODE_BYTES];
      uint8_t lb_reg = code[pc + EPA_OPCODE_BYTES + 1];
      uint32_t off   = (uint32_t)w->vm.csc[lb_reg];
      if (!w->vm.lbytes || off + 3u >= w->vm.lbytes_top) {
        snprintf(err, EPA_MAX_ERR, "LBR_MOV4: off out of range (%u)", (unsigned)off);
        return EPA_FLOW_ERR;
      }
      w->vm.csc[reg] = EPA_READ_U32_LE(w->vm.lbytes, off);
      eip->rel_pc = pc + need;
      return EPA_FLOW_YIELDED;
    }

// ---- Common VM ops (backend-independent) ----
case EPA_OP_PUSH_I32: {
  int32_t v = (int32_t)EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  if (!epa_stack_push(st, (uint32_t)v)) { snprintf(err, EPA_MAX_ERR, "PUSH_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_PUSH_R: {
  uint8_t ridx = code[pc + EPA_OPCODE_BYTES];
  if (ridx >= EPA_VM_REGS_MAX) { snprintf(err, EPA_MAX_ERR, "PUSH_R bad reg %u", (unsigned)ridx); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, (uint32_t)w->vm.csc[ridx])) { snprintf(err, EPA_MAX_ERR, "PUSH_R: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_POP_R: {
  uint8_t ridx = code[pc + EPA_OPCODE_BYTES];
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
  if (!epa_stack_push(st, (uint32_t)((a < b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "LT_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_EQ_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "EQ_I32: stack underflow"); return EPA_FLOW_ERR; }
  int32_t a = (int32_t)ua, b = (int32_t)ub;
  if (!epa_stack_push(st, (uint32_t)((a == b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "EQ_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_NE_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "NE_I32: stack underflow"); return EPA_FLOW_ERR; }
  int32_t a = (int32_t)ua, b = (int32_t)ub;
  if (!epa_stack_push(st, (uint32_t)((a != b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "NE_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_LE_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "LE_I32: stack underflow"); return EPA_FLOW_ERR; }
  int32_t a = (int32_t)ua, b = (int32_t)ub;
  if (!epa_stack_push(st, (uint32_t)((a <= b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "LE_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_GT_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "GT_I32: stack underflow"); return EPA_FLOW_ERR; }
  int32_t a = (int32_t)ua, b = (int32_t)ub;
  if (!epa_stack_push(st, (uint32_t)((a > b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "GT_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_GE_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "GE_I32: stack underflow"); return EPA_FLOW_ERR; }
  int32_t a = (int32_t)ua, b = (int32_t)ub;
  if (!epa_stack_push(st, (uint32_t)((a >= b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "GE_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_DIV_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "DIV_I32: stack underflow"); return EPA_FLOW_ERR; }
  int32_t a = (int32_t)ua, b = (int32_t)ub;
  int32_t result = (b == 0) ? 0 : (a / b);
  if (!epa_stack_push(st, (uint32_t)result)) { snprintf(err, EPA_MAX_ERR, "DIV_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_AND_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "AND_I32: stack underflow"); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, ua & ub)) { snprintf(err, EPA_MAX_ERR, "AND_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_OR_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "OR_I32: stack underflow"); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, ua | ub)) { snprintf(err, EPA_MAX_ERR, "OR_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_XOR_I32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "XOR_I32: stack underflow"); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, ua ^ ub)) { snprintf(err, EPA_MAX_ERR, "XOR_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_NOT_I32: {
  uint32_t ua=0;
  if (!epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "NOT_I32: stack underflow"); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, ~ua)) { snprintf(err, EPA_MAX_ERR, "NOT_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_ROL_BYTE: {
  uint32_t ucount=0, uvalue=0;
  if (!epa_stack_pop(st, &ucount) || !epa_stack_pop(st, &uvalue)) { snprintf(err, EPA_MAX_ERR, "ROL_BYTE: stack underflow"); return EPA_FLOW_ERR; }
  uint32_t value = uvalue & 0xFFu;
  uint32_t count = ucount & 7u;
  uint32_t result = count ? (((value << count) | (value >> (8u - count))) & 0xFFu) : value;
  if (!epa_stack_push(st, result)) { snprintf(err, EPA_MAX_ERR, "ROL_BYTE: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_ROR_BYTE: {
  uint32_t ucount=0, uvalue=0;
  if (!epa_stack_pop(st, &ucount) || !epa_stack_pop(st, &uvalue)) { snprintf(err, EPA_MAX_ERR, "ROR_BYTE: stack underflow"); return EPA_FLOW_ERR; }
  uint32_t value = uvalue & 0xFFu;
  uint32_t count = ucount & 7u;
  uint32_t result = count ? (((value >> count) | (value << (8u - count))) & 0xFFu) : value;
  if (!epa_stack_push(st, result)) { snprintf(err, EPA_MAX_ERR, "ROR_BYTE: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_PUSH_F32: {
  uint32_t bits = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  if (!epa_stack_push(st, bits)) { snprintf(err, EPA_MAX_ERR, "PUSH_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_ADD_F32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "ADD_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua), b = epa_f32_from_bits(ub);
  if (!epa_stack_push(st, epa_f32_to_bits(a + b))) { snprintf(err, EPA_MAX_ERR, "ADD_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_SUB_F32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "SUB_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua), b = epa_f32_from_bits(ub);
  if (!epa_stack_push(st, epa_f32_to_bits(a - b))) { snprintf(err, EPA_MAX_ERR, "SUB_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_MUL_F32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "MUL_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua), b = epa_f32_from_bits(ub);
  if (!epa_stack_push(st, epa_f32_to_bits(a * b))) { snprintf(err, EPA_MAX_ERR, "MUL_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_DIV_F32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "DIV_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua), b = epa_f32_from_bits(ub);
  if (!epa_stack_push(st, epa_f32_to_bits(a / b))) { snprintf(err, EPA_MAX_ERR, "DIV_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_NEG_F32: {
  uint32_t ua=0;
  if (!epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "NEG_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua);
  if (!epa_stack_push(st, epa_f32_to_bits(-a))) { snprintf(err, EPA_MAX_ERR, "NEG_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_SQRT_F32: {
  uint32_t ua=0;
  if (!epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "SQRT_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua);
  if (!epa_stack_push(st, epa_f32_to_bits(sqrtf(a)))) { snprintf(err, EPA_MAX_ERR, "SQRT_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_LT_F32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "LT_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua), b = epa_f32_from_bits(ub);
  if (!epa_stack_push(st, (uint32_t)((a < b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "LT_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_LE_F32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "LE_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua), b = epa_f32_from_bits(ub);
  if (!epa_stack_push(st, (uint32_t)((a <= b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "LE_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_GT_F32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "GT_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua), b = epa_f32_from_bits(ub);
  if (!epa_stack_push(st, (uint32_t)((a > b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "GT_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_GE_F32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "GE_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua), b = epa_f32_from_bits(ub);
  if (!epa_stack_push(st, (uint32_t)((a >= b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "GE_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_EQ_F32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "EQ_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua), b = epa_f32_from_bits(ub);
  if (!epa_stack_push(st, (uint32_t)((a == b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "EQ_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_NE_F32: {
  uint32_t ub=0, ua=0;
  if (!epa_stack_pop(st, &ub) || !epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "NE_F32: stack underflow"); return EPA_FLOW_ERR; }
  float a = epa_f32_from_bits(ua), b = epa_f32_from_bits(ub);
  if (!epa_stack_push(st, (uint32_t)((a != b) ? 1u : 0u))) { snprintf(err, EPA_MAX_ERR, "NE_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_I32_TO_F32: {
  uint32_t ua=0;
  if (!epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "I32_TO_F32: stack underflow"); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, epa_f32_to_bits((float)(int32_t)ua))) { snprintf(err, EPA_MAX_ERR, "I32_TO_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_F32_TO_I32: {
  uint32_t ua=0;
  if (!epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "F32_TO_I32: stack underflow"); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, (uint32_t)(int32_t)epa_f32_from_bits(ua))) { snprintf(err, EPA_MAX_ERR, "F32_TO_I32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_U32_TO_F32: {
  uint32_t ua=0;
  if (!epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "U32_TO_F32: stack underflow"); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, epa_f32_to_bits((float)ua))) { snprintf(err, EPA_MAX_ERR, "U32_TO_F32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_F32_TO_U32: {
  uint32_t ua=0;
  if (!epa_stack_pop(st, &ua)) { snprintf(err, EPA_MAX_ERR, "F32_TO_U32: stack underflow"); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, (uint32_t)epa_f32_from_bits(ua))) { snprintf(err, EPA_MAX_ERR, "F32_TO_U32: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_STORE_L: {
  uint8_t idx = code[pc + EPA_OPCODE_BYTES];
  if (idx >= EPA_VM_LOCALS_MAX) { snprintf(err, EPA_MAX_ERR, "STORE_L idx out of range: %u", (unsigned)idx); return EPA_FLOW_ERR; }
  uint32_t u = 0;
  if (!epa_stack_pop(st, &u)) { snprintf(err, EPA_MAX_ERR, "STORE_L: stack underflow"); return EPA_FLOW_ERR; }
  w->vm.locals[idx] = (int32_t)u;
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_LOAD_L: {
  uint8_t idx = code[pc + EPA_OPCODE_BYTES];
  if (idx >= EPA_VM_LOCALS_MAX) { snprintf(err, EPA_MAX_ERR, "LOAD_L idx out of range: %u", (unsigned)idx); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, (uint32_t)w->vm.locals[idx])) { snprintf(err, EPA_MAX_ERR, "LOAD_L: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_STORE_LW: {
  uint32_t idx = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  if (idx >= EPA_VM_LOCALS_MAX) { snprintf(err, EPA_MAX_ERR, "STORE_LW idx out of range: %u", (unsigned)idx); return EPA_FLOW_ERR; }
  uint32_t u = 0;
  if (!epa_stack_pop(st, &u)) { snprintf(err, EPA_MAX_ERR, "STORE_LW: stack underflow"); return EPA_FLOW_ERR; }
  w->vm.locals[idx] = (int32_t)u;
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_LOAD_LW: {
  uint32_t idx = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  if (idx >= EPA_VM_LOCALS_MAX) { snprintf(err, EPA_MAX_ERR, "LOAD_LW idx out of range: %u", (unsigned)idx); return EPA_FLOW_ERR; }
  if (!epa_stack_push(st, (uint32_t)w->vm.locals[idx])) { snprintf(err, EPA_MAX_ERR, "LOAD_LW: stack overflow"); return EPA_FLOW_ERR; }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

// ---- Bulk transfers (ring <-> locals) ----
case EPA_OP_KERNEL_TRX_IN_L: {
  if (w->id != 0) { snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_IN_L only valid in kernel"); return EPA_FLOW_ERR; }
  if (!ctx->hooks.get_worker) { snprintf(err, EPA_MAX_ERR, "KERNEL_TRX_IN_L: get_worker hook not installed"); return EPA_FLOW_ERR; }
  uint8_t wid8 = code[pc + EPA_OPCODE_BYTES];
  uint32_t laddr = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES + 1);
  uint16_t len = (uint16_t)EPA_READ_U16_LE(code, pc + EPA_OPCODE_BYTES + 5);
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
  uint8_t wid8 = code[pc + EPA_OPCODE_BYTES];
  uint32_t laddr = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES + 1);
  uint16_t len = (uint16_t)EPA_READ_U16_LE(code, pc + EPA_OPCODE_BYTES + 5);
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
  uint32_t laddr = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  uint16_t len = (uint16_t)EPA_READ_U16_LE(code, pc + EPA_OPCODE_BYTES + 4);
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
  uint32_t laddr = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  uint16_t len = (uint16_t)EPA_READ_U16_LE(code, pc + EPA_OPCODE_BYTES + 4);
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
  uint8_t r = code[pc + EPA_OPCODE_BYTES];
  uint32_t v;
  if (!epa_ring_pop(&w->inq, &v)) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX_IN_L inq pop failed"); return EPA_FLOW_ERR; }
  w->vm.csc[r] = (int32_t)v;
  if (r == 1u) {
    w->current_ghs = epa_h_from_regs((uint32_t)w->vm.csc[0], (uint32_t)w->vm.csc[1]);
    w->has_current_ghs = 1;
  }

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
  uint8_t r = code[pc + EPA_OPCODE_BYTES];
  if (!epa_ring_push(&w->outq, r, 0, err)) return EPA_FLOW_ERR;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_WORKER_TRX: {
  // kernel-only: transfer len words from src worker OUTQ to dst worker INQ.
  // src_laddr/dst_laddr refer to kernel locals slots that contain src_wid/dst_wid.
  if (w->id != 0) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX only valid in kernel"); return EPA_FLOW_ERR; }
  if (!ctx->hooks.get_worker) { snprintf(err, EPA_MAX_ERR, "WORKER_TRX: get_worker hook not installed"); return EPA_FLOW_ERR; }

  uint32_t src_laddr = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  uint32_t dst_laddr = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES + 4);
  uint16_t len = (uint16_t)EPA_READ_U16_LE(code, pc + EPA_OPCODE_BYTES + 8);

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

case EPA_OP_KERNEL_GHS_IN_R: {
  if (w->id != 0) { snprintf(err, EPA_MAX_ERR, "KERNEL_GHS_IN_R only valid in kernel"); return EPA_FLOW_ERR; }
  if (!ctx->hooks.get_worker) { snprintf(err, EPA_MAX_ERR, "KERNEL_GHS_IN_R: get_worker hook not installed"); return EPA_FLOW_ERR; }

  uint32_t wid_laddr = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  if (wid_laddr >= EPA_VM_LOCALS_MAX) {
    snprintf(err, EPA_MAX_ERR, "KERNEL_GHS_IN_R local addr out of range wid_local=%u", (unsigned)wid_laddr);
    return EPA_FLOW_ERR;
  }

  uint32_t wid = (uint32_t)w->vm.locals[wid_laddr];
  if (wid > 255u) {
    snprintf(err, EPA_MAX_ERR, "KERNEL_GHS_IN_R invalid wid=%u", (unsigned)wid);
    return EPA_FLOW_ERR;
  }

  EpaWorkerState *src = ctx->hooks.get_worker(ctx->hooks_user, (uint8_t)wid);
  if (!src || !src->has_current_ghs) {
    w->vm.csc[0] = 0;
    w->vm.csc[1] = 0;
    w->vm.csc[2] = 0;
    w->vm.csc[3] = 0;
    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
  }

  w->vm.csc[0] = (int32_t)epa_ghs_handle_index(src->current_ghs);
  w->vm.csc[1] = (int32_t)epa_ghs_handle_gen(src->current_ghs);
  w->vm.csc[2] = 1;
  w->vm.csc[3] = 0;
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_CALL: {
  // CALL <func_id:u32>
  // encoding: [u16 op][u32 func_id]
  uint32_t func_id = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);

  uint32_t func_index = 0;
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
  uint32_t ret_value = 0;
  if (!epa_stack_pop(st, &ret_value)) {
    snprintf(err, EPA_MAX_ERR, "RET: missing return value");
    return EPA_FLOW_ERR;
  }
  if (!epa_flow_pop_ret_frame(st, &ret, &frame_words, err)) {
    // err already set by helper
    return EPA_FLOW_ERR;
  }
  if (!epa_stack_push(st, ret_value)) {
    snprintf(err, EPA_MAX_ERR, "RET: stack overflow restoring return value");
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

case EPA_OP_G_ALLOC_L: {
  // in: r0=type, r1=size_bytes, r2=lbytes_off
  // out: r0=idx, r1=gen
  uint32_t caller = w->id;
  uint32_t type_u = (uint32_t)w->vm.csc[0];
  uint32_t size_b = (uint32_t)w->vm.csc[1];
  uint32_t off = (uint32_t)w->vm.csc[2];
  epa_ghs_handle_t h = 0;
  epa_ghs_err_t ge;

  if (!w->vm.lbytes || off > w->vm.lbytes_top || size_b > w->vm.lbytes_top - off) {
    snprintf(err, EPA_MAX_ERR, "G_ALLOC_L local range out of bounds off=%u size=%u top=%u",
             (unsigned)off, (unsigned)size_b, (unsigned)w->vm.lbytes_top);
    return EPA_FLOW_ERR;
  }

  ge = epa_ghs_alloc(k->impl.ghs, (epa_ghs_type_t)type_u, caller, size_b, &h);
  if (ge != EPA_GHS_OK) {
    snprintf(err, EPA_MAX_ERR, "G_ALLOC_L alloc failed (type=%u size=%u) err=%d", type_u, size_b, ge);
    return EPA_FLOW_ERR;
  }

  ge = epa_ghs_write_bytes(k->impl.ghs, h, 0u, w->vm.lbytes + off, size_b);
  if (ge != EPA_GHS_OK) {
    (void)epa_ghs_free(k->impl.ghs, h);
    snprintf(err, EPA_MAX_ERR, "G_ALLOC_L copy failed size=%u err=%d", size_b, ge);
    return EPA_FLOW_ERR;
  }

  w->vm.csc[0] = (int32_t)epa_ghs_handle_index(h);
  w->vm.csc[1] = (int32_t)epa_ghs_handle_gen(h);
  w->vm.csc[2] = 1;
  w->vm.csc[3] = 0;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_RGM_PUBLISH_L: {
  uint64_t name_uid = ((uint64_t)(uint32_t)w->vm.csc[1] << 32) | (uint64_t)(uint32_t)w->vm.csc[0];
  uint32_t off = (uint32_t)w->vm.csc[2];
  uint32_t size_b = (uint32_t)w->vm.csc[3];
  epa_rgm_handle_t h = 0;
  epa_rgm_err_t re;

  if (!k->impl.rgm) {
    snprintf(err, EPA_MAX_ERR, "RGM_PUBLISH_L: RGM not initialized");
    return EPA_FLOW_ERR;
  }
  if (!w->vm.lbytes || off > w->vm.lbytes_top || size_b > w->vm.lbytes_top - off) {
    snprintf(err, EPA_MAX_ERR, "RGM_PUBLISH_L local range out of bounds off=%u size=%u top=%u",
             (unsigned)off, (unsigned)size_b, (unsigned)w->vm.lbytes_top);
    return EPA_FLOW_ERR;
  }

  re = epa_rgm_publish_copy(k->impl.rgm, name_uid, w->vm.lbytes + off, size_b, &h);
  if (re != EPA_RGM_OK) {
    snprintf(err, EPA_MAX_ERR, "RGM_PUBLISH_L failed name=0x%016llx size=%u err=%d",
             (unsigned long long)name_uid, (unsigned)size_b, re);
    return EPA_FLOW_ERR;
  }

  w->vm.csc[0] = (int32_t)epa_rgm_handle_index(h);
  w->vm.csc[1] = (int32_t)epa_rgm_handle_gen(h);
  w->vm.csc[2] = (int32_t)size_b;
  w->vm.csc[3] = 1;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_RGM_GET: {
  uint64_t name_uid = ((uint64_t)(uint32_t)w->vm.csc[1] << 32) | (uint64_t)(uint32_t)w->vm.csc[0];
  epa_rgm_handle_t h = 0;
  epa_rgm_meta_t meta;
  epa_rgm_err_t re;

  if (!k->impl.rgm) {
    snprintf(err, EPA_MAX_ERR, "RGM_GET: RGM not initialized");
    return EPA_FLOW_ERR;
  }
  re = epa_rgm_get_by_name(k->impl.rgm, name_uid, &h);
  if (re == EPA_RGM_ERR_NOT_FOUND) {
    w->vm.csc[0] = 0;
    w->vm.csc[1] = 0;
    w->vm.csc[2] = 0;
    w->vm.csc[3] = 0;
    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
  }
  if (re != EPA_RGM_OK) {
    snprintf(err, EPA_MAX_ERR, "RGM_GET failed name=0x%016llx err=%d",
             (unsigned long long)name_uid, re);
    return EPA_FLOW_ERR;
  }
  re = epa_rgm_get_meta(k->impl.rgm, h, &meta);
  if (re != EPA_RGM_OK) {
    snprintf(err, EPA_MAX_ERR, "RGM_GET meta failed err=%d", re);
    return EPA_FLOW_ERR;
  }

  w->vm.csc[0] = (int32_t)epa_rgm_handle_index(h);
  w->vm.csc[1] = (int32_t)epa_rgm_handle_gen(h);
  w->vm.csc[2] = (int32_t)meta.size_bytes;
  w->vm.csc[3] = 1;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_RGM_META: {
  epa_rgm_handle_t h = epa_rgm_make_handle((uint32_t)w->vm.csc[0], (uint32_t)w->vm.csc[1]);
  epa_rgm_meta_t meta;
  epa_rgm_err_t re;
  if (!k->impl.rgm) {
    snprintf(err, EPA_MAX_ERR, "RGM_META: RGM not initialized");
    return EPA_FLOW_ERR;
  }
  re = epa_rgm_get_meta(k->impl.rgm, h, &meta);
  if (re != EPA_RGM_OK) {
    snprintf(err, EPA_MAX_ERR, "RGM_META failed err=%d", re);
    return EPA_FLOW_ERR;
  }
  w->vm.csc[0] = (int32_t)(meta.name_uid & 0xffffffffu);
  w->vm.csc[1] = (int32_t)((meta.name_uid >> 32) & 0xffffffffu);
  w->vm.csc[2] = (int32_t)meta.size_bytes;
  w->vm.csc[3] = (int32_t)meta.generation;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_RGM_READ4: {
  uint32_t rid = code[pc + EPA_OPCODE_BYTES];
  epa_rgm_handle_t h = epa_rgm_make_handle((uint32_t)w->vm.csc[0], (uint32_t)w->vm.csc[1]);
  epa_rgm_err_t re;
  if (rid >= 4u) {
    snprintf(err, EPA_MAX_ERR, "RGM_READ4: invalid register r%u", (unsigned)rid);
    return EPA_FLOW_ERR;
  }
  if (!k->impl.rgm) {
    snprintf(err, EPA_MAX_ERR, "RGM_READ4: RGM not initialized");
    return EPA_FLOW_ERR;
  }
  re = epa_rgm_read_bytes(k->impl.rgm, h, (uint32_t)w->vm.csc[2], &w->vm.csc[rid], 4u);
  if (re != EPA_RGM_OK) {
    snprintf(err, EPA_MAX_ERR, "RGM_READ4 failed off=%u err=%d", (unsigned)w->vm.csc[2], re);
    return EPA_FLOW_ERR;
  }

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
        if (w->has_current_ghs && w->current_ghs == h) {
            w->has_current_ghs = 0;
            w->current_ghs = 0;
        }

        msg[i] = h;
    }

    // Notify receiver (kernel-owned ingress queue -> worker wakes on it)
    if (!epa_kernel_deliver_ghs_handles_framed(k, new_owner, msg, n,
                                               k->kernel_uid, w->id, err)) {
        free(msg);
        w->faulted = 1;
        snprintf(w->fault_message, sizeof(w->fault_message),
                 "G_XFER FAULT: ingress queue full for wid=%u", new_owner);
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

    uint32_t count = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);

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
        if (w->has_current_ghs && w->current_ghs == h) {
            w->has_current_ghs = 0;
            w->current_ghs = 0;
        }

        msg[i] = h;
    }

    // Ring notify receiver
    if (!epa_kernel_deliver_ghs_handles_framed(k, new_owner, msg, count,
                                               k->kernel_uid, w->id, err)) {
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

case EPA_OP_G_TAG: {
  epa_ghs_handle_t h = epa_h_from_regs(w->vm.csc[0], w->vm.csc[1]);
  uint32_t tag = 0;
  epa_ghs_err_t ge = epa_ghs_get_tag(k->impl.ghs, h, &tag);
  if (ge != EPA_GHS_OK) {
    snprintf(err, EPA_MAX_ERR, "G_TAG failed err=%d", ge);
    return 0;
  }

  w->vm.csc[0] = (int32_t)tag;
  w->vm.csc[1] = 0;
  w->vm.csc[2] = 0;
  w->vm.csc[3] = 0;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_GR_MOV4: {
	uint32_t rid = code[pc + EPA_OPCODE_BYTES];
	epa_ghs_handle_t h = ((uint64_t)w->vm.csc[1] << 32) | (uint64_t)w->vm.csc[0];
	epa_ghs_err_t ge;
	if (rid >= 4u) {
	  snprintf(err, EPA_MAX_ERR, "GR_MOV4: invalid register r%u", (unsigned)rid);
	  return EPA_FLOW_ERR;
	}
	ge = epa_ghs_read_bytes(k->impl.ghs, h, (uint32_t)w->vm.csc[2], &w->vm.csc[rid], 4);
	if (ge != EPA_GHS_OK) {
	  snprintf(err, EPA_MAX_ERR, "GR_MOV4 failed off=%u err=%d", (unsigned)w->vm.csc[2], ge);
	  return EPA_FLOW_ERR;
	}

	eip->rel_pc = (uint32_t)(pc + need);
	return EPA_FLOW_YIELDED;
}

case EPA_OP_GW_MOV4: {
	uint32_t rid = code[pc + EPA_OPCODE_BYTES];
	epa_ghs_handle_t h = ((uint64_t)w->vm.csc[1] << 32) | (uint64_t)w->vm.csc[0];
	epa_ghs_err_t ge;
	if (rid >= 4u) {
	  snprintf(err, EPA_MAX_ERR, "GW_MOV4: invalid register r%u", (unsigned)rid);
	  return EPA_FLOW_ERR;
	}
	ge = epa_ghs_write_bytes(k->impl.ghs, h, (uint32_t)w->vm.csc[2], &w->vm.csc[rid], 4);
	if (ge != EPA_GHS_OK) {
	  snprintf(err, EPA_MAX_ERR, "GW_MOV4 failed off=%u err=%d", (unsigned)w->vm.csc[2], ge);
	  return EPA_FLOW_ERR;
	}

	eip->rel_pc = (uint32_t)(pc + need);
	return EPA_FLOW_YIELDED;
}

case EPA_OP_DYN_ALLOC: {
  uint32_t pool_id = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  EpaDynamicPool *pool = worker_dynamic_pool_by_id(w, pool_id);
  uint32_t ordinal = 0u;
  if (!pool) {
    snprintf(err, EPA_MAX_ERR, "DYN_ALLOC: invalid pool_id=%u", (unsigned)pool_id);
    return EPA_FLOW_ERR;
  }
  if (pool->count >= pool->cap) {
    uint32_t requested_cap = pool->cap ? pool->cap + pool->grow_by : pool->grow_by;
    int submit_rc;
    if (requested_cap <= pool->count) requested_cap = pool->count + 1u;
    if (!ctx->hooks.on_request_dynamic_pool_capacity) {
      snprintf(err, EPA_MAX_ERR, "DYN_ALLOC: dynamic capacity hook not installed");
      return EPA_FLOW_ERR;
    }
    submit_rc = ctx->hooks.on_request_dynamic_pool_capacity(ctx->hooks_user, (uint8_t)w->id, pool_id, requested_cap, 1, err);
    if (submit_rc == 2) {
      if (err) err[0] = 0;
      return EPA_FLOW_YIELDED;
    }
    if (!submit_rc) return EPA_FLOW_ERR;
  }
  if (!epa_dynamic_pool_alloc(pool, &ordinal, err)) return EPA_FLOW_ERR;
  w->vm.csc[0] = (int32_t)ordinal;
  w->vm.csc[1] = 1;
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_DYN_FREE: {
  uint32_t pool_id = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  EpaDynamicPool *pool = worker_dynamic_pool_by_id(w, pool_id);
  if (!pool) {
    snprintf(err, EPA_MAX_ERR, "DYN_FREE: invalid pool_id=%u", (unsigned)pool_id);
    return EPA_FLOW_ERR;
  }
  if (!epa_dynamic_pool_release(pool, (uint32_t)w->vm.csc[0], err)) return EPA_FLOW_ERR;
  if (ctx->hooks.on_request_dynamic_pool_capacity && pool->cap > pool->count + pool->max_free) {
    uint32_t suggested_cap = pool->count + pool->min_free;
    (void)ctx->hooks.on_request_dynamic_pool_capacity(ctx->hooks_user, (uint8_t)w->id, pool_id, suggested_cap, 0, err);
    if (err) err[0] = 0;
  }
  w->vm.csc[1] = 1;
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_DYN_LOAD: {
  uint32_t pool_id = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  EpaDynamicPool *pool = worker_dynamic_pool_by_id(w, pool_id);
  uint32_t size;
  uint32_t top;
  uint32_t off;
  if (!pool) {
    snprintf(err, EPA_MAX_ERR, "DYN_LOAD: invalid pool_id=%u", (unsigned)pool_id);
    return EPA_FLOW_ERR;
  }
  size = pool->element_size;
  top  = w->vm.lbytes_top;
  off  = (top + 3u) & ~3u;
  if (!w->vm.lbytes || size > w->vm.lbytes_cap || off > w->vm.lbytes_cap - size) {
    snprintf(err, EPA_MAX_ERR, "DYN_LOAD: local byte heap exhausted for size=%u", (unsigned)size);
    return EPA_FLOW_ERR;
  }
  if (!epa_dynamic_pool_read(pool, (uint32_t)w->vm.csc[0], w->vm.lbytes + off, size, err)) return EPA_FLOW_ERR;
  w->vm.lbytes_top = off + size;
  w->vm.csc[0] = (int32_t)off;
  w->vm.csc[1] = (int32_t)size;
  w->vm.csc[2] = 1;
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_DYN_STORE: {
  uint32_t pool_id = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  EpaDynamicPool *pool = worker_dynamic_pool_by_id(w, pool_id);
  uint32_t off;
  uint32_t size;
  if (!pool) {
    snprintf(err, EPA_MAX_ERR, "DYN_STORE: invalid pool_id=%u", (unsigned)pool_id);
    return EPA_FLOW_ERR;
  }
  off  = (uint32_t)w->vm.csc[1];
  size = (uint32_t)w->vm.csc[2];
  if (!w->vm.lbytes || off > w->vm.lbytes_top || size > w->vm.lbytes_top - off) {
    snprintf(err, EPA_MAX_ERR, "DYN_STORE: invalid source ref off=%u size=%u", (unsigned)off, (unsigned)size);
    return EPA_FLOW_ERR;
  }
  if (!epa_dynamic_pool_write(pool, (uint32_t)w->vm.csc[0], w->vm.lbytes + off, size, err)) return EPA_FLOW_ERR;
  w->vm.csc[3] = 1;
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_DYN_SWAP: {
  uint32_t pool_id = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  EpaDynamicPool *pool = worker_dynamic_pool_by_id(w, pool_id);
  if (!pool) {
    snprintf(err, EPA_MAX_ERR, "DYN_SWAP: invalid pool_id=%u", (unsigned)pool_id);
    return EPA_FLOW_ERR;
  }
  if (!epa_dynamic_pool_swap(pool, (uint32_t)w->vm.csc[0], (uint32_t)w->vm.csc[1], err)) return EPA_FLOW_ERR;
  w->vm.csc[2] = 1;
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_DYN_ITER_HEAD: {
  uint32_t pool_id = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  EpaDynamicPool *pool = worker_dynamic_pool_by_id(w, pool_id);
  if (!pool) {
    snprintf(err, EPA_MAX_ERR, "DYN_ITER_HEAD: invalid pool_id=%u", (unsigned)pool_id);
    return EPA_FLOW_ERR;
  }
  w->vm.csc[0] = 0;
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_DYN_ITER_NEXT: {
  uint32_t pool_id = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);
  EpaDynamicPool *pool = worker_dynamic_pool_by_id(w, pool_id);
  uint32_t ordinal = (uint32_t)w->vm.csc[0];
  if (!pool) {
    snprintf(err, EPA_MAX_ERR, "DYN_ITER_NEXT: invalid pool_id=%u", (unsigned)pool_id);
    return EPA_FLOW_ERR;
  }
  if (ordinal >= pool->count) {
    w->vm.csc[1] = 0;
    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
  }
  {
    uint32_t size = pool->element_size;
    uint32_t top  = w->vm.lbytes_top;
    uint32_t off  = (top + 3u) & ~3u;
    if (!w->vm.lbytes || size > w->vm.lbytes_cap || off > w->vm.lbytes_cap - size) {
      snprintf(err, EPA_MAX_ERR, "DYN_ITER_NEXT: local byte heap exhausted size=%u", (unsigned)size);
      return EPA_FLOW_ERR;
    }
    if (!epa_dynamic_pool_read(pool, ordinal, w->vm.lbytes + off, size, err)) return EPA_FLOW_ERR;
    w->vm.lbytes_top = off + size;
    w->vm.csc[0] = (int32_t)(ordinal + 1u);
    w->vm.csc[1] = 1;
    w->vm.csc[2] = (int32_t)off;
    w->vm.csc[3] = (int32_t)size;
  }
  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_LOAD_CONST: {
  uint32_t id = EPA_READ_U32_LE(code, pc + EPA_OPCODE_BYTES);

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
  w->vm.lscope_depth = 0;
  w->vm.csc[0] = 0;
  w->vm.csc[1] = 0;
  w->vm.csc[2] = 1;   // ok
  w->vm.csc[3] = 0;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_L_SCOPE_ENTER: {
  if (w->vm.lscope_depth >= EPA_VM_LSCOPE_MAX) {
    w->vm.csc[0] = 0;
    w->vm.csc[1] = 0;
    w->vm.csc[2] = 0;
    w->vm.csc[3] = 0;
    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
  }

  w->vm.lscope_marks[w->vm.lscope_depth++] = w->vm.lbytes_top;
  w->vm.csc[0] = w->vm.lbytes_top;
  w->vm.csc[1] = 0;
  w->vm.csc[2] = 1;
  w->vm.csc[3] = 0;

  eip->rel_pc = (uint32_t)(pc + need);
  return EPA_FLOW_YIELDED;
}

case EPA_OP_L_SCOPE_LEAVE: {
  if (w->vm.lscope_depth == 0) {
    w->vm.csc[0] = w->vm.lbytes_top;
    w->vm.csc[1] = 0;
    w->vm.csc[2] = 0;
    w->vm.csc[3] = 0;
    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
  }

  w->vm.lscope_depth--;
  w->vm.lbytes_top = w->vm.lscope_marks[w->vm.lscope_depth];
  w->vm.csc[0] = w->vm.lbytes_top;
  w->vm.csc[1] = 0;
  w->vm.csc[2] = 1;
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

case EPA_OP_L_SCOPE_ALLOC: {
  // Scoped allocation uses the same bump allocator semantics as L_ALLOC.
  // Lifetime is bounded by the nearest active L_SCOPE_ENTER/L_SCOPE_LEAVE pair.
  uint32_t sz = (uint32_t)w->vm.csc[0];

  if (!w->vm.lbytes || w->vm.lbytes_cap == 0) {
    w->vm.csc[0] = 0;
    w->vm.csc[1] = 0;
    w->vm.csc[2] = 0;
    w->vm.csc[3] = 0;
    eip->rel_pc = (uint32_t)(pc + need);
    return EPA_FLOW_YIELDED;
  }

  {
    uint32_t top = w->vm.lbytes_top;
    const uint32_t align = 4;
    uint32_t aligned_top = (top + (align - 1u)) & ~(align - 1u);
    if (sz > w->vm.lbytes_cap || aligned_top > w->vm.lbytes_cap - sz) {
      w->vm.csc[0] = 0;
      w->vm.csc[1] = 0;
      w->vm.csc[2] = 0;
      w->vm.csc[3] = 0;
      eip->rel_pc = (uint32_t)(pc + need);
      return EPA_FLOW_YIELDED;
    }

    w->vm.lbytes_top = aligned_top + sz;
    w->vm.csc[0] = aligned_top;
    w->vm.csc[1] = sz;
    w->vm.csc[2] = 1;
    w->vm.csc[3] = 0;
  }

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

    default:
      return EPA_FLOW_NOT_FLOW;
  }
}
