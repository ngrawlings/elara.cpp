#include "EpaDbgShim.h"

#include <string.h>
#include <libelaraparallelassembly/epa_kernel.h>
#include <libelaraparallelassembly/vm/epa_worker_state.h>
#include <libelaraparallelassembly/memory/epa_stack.h>
#include <libelaraparallelassembly/memory/epa_ring_buffer.h>
#include <libelaraparallelassembly/memory/epa_ghs.h>
#include <libelaraparallelassembly/epa_program_desc.h>

int epa_dbg_capture_kernel(EpaKernel *kernel, EpaDbgKernelSnapshot *out) {
    uint32_t wid;
    if (!kernel || !out) return 0;
    memset(out, 0, sizeof(*out));
    out->prog_loaded        = (uint32_t)kernel->prog_loaded;
    out->rr_cursor          = kernel->impl.rr_cursor;
    out->current_wid        = kernel->impl.cur_wid;
    out->interrupt_requested = (uint32_t)kernel->impl.interrupt_requested;
    for (wid = 0; wid < EPA_MAX_WORKERS; wid++) {
        if (kernel->impl.workers[wid].inited) out->worker_count++;
    }
    if (kernel->impl.ghs) {
        out->ghs_live_count = epa_ghs_live_count(kernel->impl.ghs);
        out->ghs_capacity = epa_ghs_capacity(kernel->impl.ghs);
    }
    return 1;
}

static uint32_t epa_dbg_count_ghs_owned_by(EpaKernel *kernel, uint32_t owner) {
    uint32_t i;
    uint32_t count = 0;
    epa_ghs_t *ghs;
    if (!kernel || !kernel->impl.ghs) return 0;
    ghs = kernel->impl.ghs;
    for (i = 0; i < ghs->max_entries; i++) {
        epa_ghs_entry_t *entry = &ghs->entries[i];
        if ((entry->flags & EPA_GHS_F_IN_USE) && entry->owner == owner) count++;
    }
    return count;
}

size_t epa_dbg_capture_workers(EpaKernel *kernel, EpaDbgWorkerSnapshot *out, size_t max_workers) {
    size_t count = 0;
    uint32_t wid;
    if (!kernel || !out || !max_workers) return 0;
    memset(out, 0, sizeof(*out) * max_workers);
    for (wid = 0; wid < EPA_MAX_WORKERS && count < max_workers; wid++) {
        EpaWorkerState *w = &kernel->impl.workers[wid];
        EpaDbgWorkerSnapshot *dst;
        size_t i;
        if (!w->inited) continue;
        dst = &out[count++];
        memset(dst, 0, sizeof(*dst));
        dst->wid              = wid;
        dst->active           = 1u;
        dst->inited           = (uint32_t)w->inited;
        dst->halted           = (uint32_t)w->halted;
        dst->blocked          = (uint32_t)w->blocked;
        dst->faulted          = (uint32_t)w->faulted;
        dst->waiting_for_data = (uint32_t)w->waiting_for_data;
        dst->at_running       = (uint32_t)w->at_running;
        dst->has_current_ghs  = (uint32_t)w->has_current_ghs;
        memcpy(dst->csc, w->vm.csc, sizeof(dst->csc));
        dst->stack_depth         = (uint32_t)w->vm.stack.sp;
        dst->stack_preview_count = (uint32_t)(w->vm.stack.sp < EPA_DBG_STACK_PREVIEW ? w->vm.stack.sp : EPA_DBG_STACK_PREVIEW);
        for (i = 0; i < dst->stack_preview_count; i++) {
            size_t src = w->vm.stack.sp - dst->stack_preview_count + i;
            dst->stack_preview[i] = w->vm.stack.words ? w->vm.stack.words[src] : 0u;
        }
        dst->inq_count  = kernel->ingress.inq[wid].count;
        dst->outq_count = epa_ring_count(&w->outq);
        dst->owned_ghs_count = epa_dbg_count_ghs_owned_by(kernel, wid);
        memcpy(dst->locals, w->vm.locals, sizeof(dst->locals));
        dst->lbytes_top   = w->vm.lbytes_top;
        dst->lbytes_cap   = w->vm.lbytes_cap;
        dst->lscope_depth = w->vm.lscope_depth;
        dst->current_ghs  = (uint64_t)w->current_ghs;
        dst->eip.block_type = w->vm.eip.block_type;
        dst->eip.block_id   = w->vm.eip.block_id;
        dst->eip.rel_pc     = w->vm.eip.rel_pc;
        memcpy(dst->fault_message, w->fault_message, sizeof(dst->fault_message));
    }
    return count;
}

int epa_dbg_any_worker_at(EpaKernel *kernel, uint8_t block_type, uint32_t block_id, uint32_t rel_pc, uint32_t *out_wid) {
    uint32_t wid;
    if (!kernel) return 0;
    for (wid = 0; wid < EPA_MAX_WORKERS; wid++) {
        EpaWorkerState *w = &kernel->impl.workers[wid];
        if (!w->inited || w->halted || w->faulted) continue;
        if (w->vm.eip.block_type == block_type && w->vm.eip.block_id == block_id && w->vm.eip.rel_pc == rel_pc) {
            if (out_wid) *out_wid = wid;
            return 1;
        }
    }
    return 0;
}

int epa_dbg_patch_code(EpaKernel *kernel, uint8_t block_type, uint32_t block_id, uint32_t rel_pc,
                       const uint8_t *bytes, size_t len, uint8_t *original) {
    const uint8_t *code_const;
    uint8_t *code;
    size_t code_len;
    if (!kernel || !bytes || !len) return 0;
    if (!epa_prog_resolve(&kernel->prog, block_type, block_id, &code_const, &code_len)) return 0;
    if ((size_t)rel_pc > code_len || len > code_len - (size_t)rel_pc) return 0;
    code = (uint8_t *)code_const;
    if (original) memcpy(original, code + rel_pc, len);
    memcpy(code + rel_pc, bytes, len);
    return 1;
}

int epa_dbg_set_worker_eip(EpaKernel *kernel, uint32_t wid, uint8_t block_type, uint32_t block_id, uint32_t rel_pc) {
    EpaWorkerState *w;
    if (!kernel || wid >= EPA_MAX_WORKERS) return 0;
    w = &kernel->impl.workers[wid];
    if (!w->inited) return 0;
    w->vm.eip.block_type = block_type;
    w->vm.eip.block_id = block_id;
    w->vm.eip.rel_pc = rel_pc;
    return 1;
}

int epa_dbg_capture_worker_inspect(EpaKernel *kernel, uint32_t wid, EpaDbgWorkerInspect *out,
                                   uint32_t stack_words_limit, uint32_t arena_bytes_limit, uint32_t ghs_bytes_limit) {
    EpaWorkerState *w;
    uint32_t stack_take, arena_take, ghs_take;
    if (!kernel || !out || wid >= EPA_MAX_WORKERS) return 0;
    w = &kernel->impl.workers[wid];
    if (!w->inited) return 0;
    memset(out, 0, sizeof(*out));
    out->wid = wid;
    out->halted = (uint32_t)w->halted;
    out->blocked = (uint32_t)w->blocked;
    out->faulted = (uint32_t)w->faulted;
    out->waiting_for_data = (uint32_t)w->waiting_for_data;
    out->at_running = (uint32_t)w->at_running;
    out->inq_count = kernel->ingress.inq[wid].count;
    out->outq_count = epa_ring_count(&w->outq);
    memcpy(out->csc, w->vm.csc, sizeof(out->csc));
    out->eip.block_type = w->vm.eip.block_type;
    out->eip.block_id = w->vm.eip.block_id;
    out->eip.rel_pc = w->vm.eip.rel_pc;
    out->stack_depth = (uint32_t)w->vm.stack.sp;
    stack_take = stack_words_limit ? stack_words_limit : EPA_DBG_STACK_WORDS;
    if (stack_take > EPA_DBG_STACK_WORDS) stack_take = EPA_DBG_STACK_WORDS;
    if (stack_take > out->stack_depth) stack_take = out->stack_depth;
    out->stack_word_count = stack_take;
    out->stack_start = out->stack_depth > stack_take ? (out->stack_depth - stack_take) : 0u;
    if (w->vm.stack.words && stack_take) {
        memcpy(out->stack_words, &w->vm.stack.words[out->stack_start], stack_take * sizeof(uint32_t));
    }
    memcpy(out->locals, w->vm.locals, sizeof(out->locals));
    out->lbytes_top = w->vm.lbytes_top;
    out->lbytes_cap = w->vm.lbytes_cap;
    out->lscope_depth = w->vm.lscope_depth;
    arena_take = arena_bytes_limit ? arena_bytes_limit : EPA_DBG_ARENA_PREVIEW;
    if (arena_take > EPA_DBG_ARENA_PREVIEW) arena_take = EPA_DBG_ARENA_PREVIEW;
    if (arena_take > out->lbytes_top) arena_take = out->lbytes_top;
    out->arena_preview_len = arena_take;
    out->arena_preview_from = out->lbytes_top - arena_take;
    if (w->vm.lbytes && arena_take) {
        memcpy(out->arena_preview, &w->vm.lbytes[out->arena_preview_from], arena_take);
    }
    out->has_current_ghs = (uint32_t)w->has_current_ghs;
    out->current_ghs = (uint64_t)w->current_ghs;
    memcpy(out->fault_message, w->fault_message, sizeof(out->fault_message));
    if (kernel->impl.ghs) {
        out->ghs_live_count = epa_ghs_live_count(kernel->impl.ghs);
        out->ghs_capacity = epa_ghs_capacity(kernel->impl.ghs);
    }
    if (kernel->impl.ghs && w->has_current_ghs) {
        epa_ghs_meta_t meta;
        memset(&meta, 0, sizeof(meta));
        if (epa_ghs_get_meta(kernel->impl.ghs, w->current_ghs, &meta) == EPA_GHS_OK) {
            out->ghs.valid = 1u;
            out->ghs.type = (uint32_t)meta.type;
            out->ghs.owner = meta.owner;
            out->ghs.flags = meta.flags;
            out->ghs.size_bytes = meta.size_bytes;
            out->ghs.capacity = meta.capacity;
            out->ghs.generation = meta.generation;
            ghs_take = ghs_bytes_limit ? ghs_bytes_limit : EPA_DBG_GHS_PREVIEW;
            if (ghs_take > EPA_DBG_GHS_PREVIEW) ghs_take = EPA_DBG_GHS_PREVIEW;
            if (ghs_take > meta.size_bytes) ghs_take = meta.size_bytes;
            out->ghs.preview_len = ghs_take;
            if (ghs_take) {
                epa_ghs_read_bytes(kernel->impl.ghs, w->current_ghs, 0u, out->ghs.preview, ghs_take);
            }
        }
    }
    return 1;
}
