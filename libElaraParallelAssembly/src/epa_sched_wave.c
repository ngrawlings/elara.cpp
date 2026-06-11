#include "epa_kernel.h"
#include "epa_kernel_internal.h"
#include "epa_kernel_hooks.h"

#include <stdio.h>

static void wave_request_interrupt(EpaKernel *k, EpaSchedState *s) {
  (void)s;
  // Keep interrupt flag in kernel impl so all schedulers behave the same.
  k->impl.interrupt_requested = 1;
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

static int wave_run(EpaKernel *k,
                    EpaSchedState *s,
                    uint32_t max_ticks,
                    int debug,
                    char err[EPA_MAX_ERR]) {
  (void)s;

  if (err) err[0] = 0;
  if (!k || !k->prog_loaded) {
    snprintf(err, EPA_MAX_ERR, "run: program not loaded");
    return 0;
  }

  uint32_t ticks = 0;
  k->impl.interrupt_requested = 0;

  if (!epa_kernel_drain_ingress(k, err)) return 0;
  uint64_t ingress_seen = k->impl.ingress_deliveries;

  for (;;) {
    if (max_ticks && ticks >= max_ticks && !any_ignore_max_ticks_runnable(k)) {
      if (!debug) {
        kdbg_emit(k, EPA_KDBG_EXCEPT, 0, 0xFFFF0001u, &k->impl.workers[0].vm.eip, "timeout");
        snprintf(err, EPA_MAX_ERR, "run: timeout after %u ticks", ticks);
      } else {
        snprintf(err, EPA_MAX_ERR, "run: step complete returning to host after %u ticks", ticks);
      }
      return 0;
    }

    int any_ran = 0;

    if (k->impl.n_workers > 0) {
      // Build round-robin order starting from rr_cursor.
      // Two list walks: first wids >= rr_cursor, then wids < rr_cursor (wrap).
      // EPA_MAX_WORKERS is the nil sentinel in worker_next.
      uint32_t order[EPA_MAX_WORKERS];
      uint32_t norder = 0;
      uint32_t rc = k->impl.rr_cursor;

      for (uint32_t w = k->impl.worker_head; w < EPA_MAX_WORKERS; w = k->impl.worker_next[w])
        if (w >= rc) order[norder++] = w;
      for (uint32_t w = k->impl.worker_head; w < EPA_MAX_WORKERS && w < rc; w = k->impl.worker_next[w])
        order[norder++] = w;

      for (uint32_t step = 0; step < norder; step++) {
        uint32_t i = order[step];
        EpaWorkerState *w = &k->impl.workers[i];

        if (!worker_runnable(w)) continue;

        any_ran = 1;
        k->impl.cur_wid = i;
        if (!w->ignore_max_ticks) ticks++;

        // Advance cursor to the next wid in list (wrapping to head).
        uint32_t next_cursor = k->impl.worker_next[i];
        if (next_cursor >= EPA_MAX_WORKERS) next_cursor = k->impl.worker_head;

        // --- execute exactly one "tick" of this worker ---
        EpaFlowRc frc = epa_flow_step(
            k,
            &k->flow,
            w,
            (EpaStack*)&w->vm.stack,
            err
        );

        if (k->boot_reset_pending) {
          if (!epa_kernel_commit_pending_boot_reset(k, err)) {
            return 0;
          }
          return 2;
        }

        if (frc == EPA_FLOW_ERR) {
          w->faulted = 1;
          epa_print_fault_location(k, i, &w->vm.eip, err);
          kdbg_emit(k, EPA_KDBG_EXCEPT, (uint8_t)i, 0xFFFF0002u, &w->vm.eip, err);
          return 0;
        }

        if (frc == EPA_FLOW_OK) {
          w->halted = 1;
          k->impl.rr_cursor = next_cursor;
          if (i == 0) return 1; // kernel ended
          // kernel continues; go to next worker
        } else if (frc == EPA_FLOW_NOT_FLOW) {
          EpaNonFlowRc nrc = k->nf.vt->exec_one(k->nf.impl, &k->prog, w, &w->vm.eip, err);

          if (nrc == EPA_NF_EXEC_ERR) {
            w->faulted = 1;
            epa_print_fault_location(k, i, &w->vm.eip, err);
            kdbg_emit(k, EPA_KDBG_EXCEPT, (uint8_t)i, 0xFFFF0003u, &w->vm.eip, err);
            return 0;
          }

          if (nrc == EPA_NF_EXEC_HALT) {
            w->halted = 1;
            k->impl.rr_cursor = next_cursor;
            if (i == 0) return 1;
          }
        } else {
          // EPA_FLOW_YIELDED or other yield-like outcome:
          // advance cursor so resume behaves as if no pause happened
          k->impl.rr_cursor = next_cursor;
        }

        // --- SAFE INTERRUPT BOUNDARY: AFTER completing this worker tick ---
        if (k->impl.interrupt_requested) {
          // IMPORTANT: resume should start at next worker, not current
          k->impl.rr_cursor = next_cursor;
          k->impl.interrupt_requested = 0;
          return 2;
        }
      }
    }

    if (!epa_kernel_drain_ingress(k, err)) return 0;
    if (ingress_seen != k->impl.ingress_deliveries) {
      ingress_seen = k->impl.ingress_deliveries;
      ticks = 0;
    }

    if (!any_ran) return 1;
  }
}

const EpaSchedulerVt EPA_SCHED_WAVE_VT = {
  .name = "wave",
  .init = NULL,
  .destroy = NULL,
  .request_interrupt = wave_request_interrupt,
  .wake = NULL,
  .run = wave_run
};
