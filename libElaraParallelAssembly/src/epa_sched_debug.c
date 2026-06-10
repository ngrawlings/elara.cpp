#include "epa_kernel.h"
#include "epa_kernel_internal.h"
#include "epa_kernel_hooks.h"

#include <stdio.h>
#include <string.h>

/* Debug scheduler: fully synchronous, no autonomous threads, no timeouts.
 * Execution is driven entirely by the host (Python side) via explicit RPC
 * calls.  run() executes exactly max_ticks worker ticks and returns.
 * The host is responsible for all scheduling decisions.
 */

static void debug_request_interrupt(EpaKernel *k, EpaSchedState *s) {
    (void)s;
    k->impl.interrupt_requested = 1;
}

static void debug_wake(EpaKernel *k, EpaSchedState *s) {
    (void)k; (void)s;
}

static int worker_runnable(const EpaWorkerState *w) {
    return w && w->inited && !w->retired && !w->halted && !w->faulted && !w->blocked && !w->waiting_for_data;
}

static int any_ignore_max_ticks_runnable(const EpaKernel *k) {
    if (!k) return 0;
    for (uint32_t wid = 0; wid < EPA_MAX_WORKERS; wid++) {
        const EpaWorkerState *w = &k->impl.workers[wid];
        if (worker_runnable(w) && w->ignore_max_ticks) return 1;
    }
    return 0;
}

static int debug_run(EpaKernel *k,
                     EpaSchedState *s,
                     uint32_t max_ticks,
                     int debug,
                     char err[EPA_MAX_ERR]) {
    (void)s;
    (void)debug; /* always in debug mode */

    if (err) err[0] = 0;
    if (!k || !k->prog_loaded) {
        if (err) snprintf(err, EPA_MAX_ERR, "debug_sched: program not loaded");
        return 0;
    }

    if (!epa_kernel_drain_ingress(k, err)) return 0;
    uint64_t ingress_seen = k->impl.ingress_deliveries;

    k->impl.interrupt_requested = 0;

    uint32_t ticks = 0;

    /* Walk the active-worker list, round-robin from rr_cursor. */
    uint32_t start = k->impl.rr_cursor;
    if (start >= EPA_MAX_WORKERS) start = k->impl.worker_head;

    /* Two-pass round-robin: workers >= start, then workers < start. */
    uint32_t order[EPA_MAX_WORKERS];
    uint32_t norder = 0;
    uint32_t wid;

    /* First pass: rr_cursor onward */
    for (wid = start; wid < EPA_MAX_WORKERS; wid = k->impl.worker_next[wid]) {
        if (wid >= EPA_MAX_WORKERS) break;
        order[norder++] = wid;
    }
    /* Second pass: head up to (but not including) start */
    for (wid = k->impl.worker_head; wid < start; wid = k->impl.worker_next[wid]) {
        if (wid >= EPA_MAX_WORKERS) break;
        order[norder++] = wid;
    }

    int any_ran = 0;
    for (uint32_t oi = 0; oi < norder; oi++) {
        if (max_ticks && ticks >= max_ticks && !any_ignore_max_ticks_runnable(k)) break;
        if (k->impl.interrupt_requested) break;

        wid = order[oi];
        EpaWorkerState *w = &k->impl.workers[wid];

        if (!worker_runnable(w)) continue;

        any_ran = 1;
        k->impl.cur_wid = wid;

        uint32_t next_cursor = k->impl.worker_next[wid];
        if (next_cursor >= EPA_MAX_WORKERS) next_cursor = k->impl.worker_head;

        EpaFlowRc frc = epa_flow_step(k, &k->flow, w, (EpaStack*)&w->vm.stack, err);
        if (!w->ignore_max_ticks) ticks++;

        if (frc == EPA_FLOW_ERR) {
            w->faulted = 1;
            epa_print_fault_location(k, wid, &w->vm.eip, err);
            kdbg_emit(k, EPA_KDBG_EXCEPT, (uint8_t)wid, 0xFFFF0002u, &w->vm.eip, err);
            return 0;
        }

        if (frc == EPA_FLOW_OK) {
            w->halted = 1;
            k->impl.rr_cursor = next_cursor;
            if (wid == 0) return 1; /* kernel entry halted — done */
        } else if (frc == EPA_FLOW_NOT_FLOW) {
            EpaNonFlowRc nrc = k->nf.vt->exec_one(k->nf.impl, &k->prog, w, &w->vm.eip, err);
            if (nrc == EPA_NF_EXEC_ERR) {
                w->faulted = 1;
                epa_print_fault_location(k, wid, &w->vm.eip, err);
                kdbg_emit(k, EPA_KDBG_EXCEPT, (uint8_t)wid, 0xFFFF0003u, &w->vm.eip, err);
                return 0;
            }
            k->impl.rr_cursor = next_cursor;
        } else {
            /* EPA_FLOW_YIELDED — normal step */
            k->impl.rr_cursor = next_cursor;
        }
    }

    if (!any_ran) return 1; /* all workers idle/halted — not an error */

    if (!epa_kernel_drain_ingress(k, err)) return 0;
    if (ingress_seen != k->impl.ingress_deliveries) {
        ingress_seen = k->impl.ingress_deliveries;
        ticks = 0;
    }

    if (max_ticks && ticks >= max_ticks && !any_ignore_max_ticks_runnable(k)) {
        if (err)
            snprintf(err, EPA_MAX_ERR,
                     "run: step complete returning to host after %u ticks", ticks);
        return 0;
    }

    return 1;
}

const EpaSchedulerVt EPA_SCHED_DEBUG_VT = {
    "debug",
    NULL,   /* init */
    NULL,   /* destroy */
    debug_request_interrupt,
    debug_wake,
    debug_run,
};
