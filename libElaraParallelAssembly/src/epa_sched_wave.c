#include "epa_kernel.h"
#include "epa_kernel_internal.h"
#include "epa_kernel_hooks.h"

#include <stdio.h>

static void wave_request_interrupt(EpaKernel *k, EpaSchedState *s) {
  (void)s;
  // Keep interrupt flag in kernel impl so all schedulers behave the same.
  k->impl.interrupt_requested = 1;
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

  for (;;) {
    if (max_ticks && ticks++ >= max_ticks) {
      if (!debug) {
        kdbg_emit(k, EPA_KDBG_EXCEPT, 0, 0xFFFF0001u, &k->impl.workers[0].vm.eip, "timeout");
        snprintf(err, EPA_MAX_ERR, "run: timeout after %u ticks", ticks);
      } else {
        snprintf(err, EPA_MAX_ERR, "run: step complete returning to host after %u ticks", ticks);
      }
      return 0;
    }

    int any_ran = 0;

    // Round-robin: attempt up to EPA_MAX_WORKERS steps each outer tick
    for (uint32_t step = 0; step < EPA_MAX_WORKERS; step++) {
      uint32_t i = (k->impl.rr_cursor + step) % EPA_MAX_WORKERS;
      EpaWorkerState *w = &k->impl.workers[i];

      if (!w->inited || w->halted || w->faulted || w->blocked) continue;

      any_ran = 1;
      k->impl.cur_wid = i;

      // --- execute exactly one "tick" of this worker ---
      EpaFlowRc frc = epa_flow_step(
          k,
          &k->flow,
          w,
          (EpaStack*)&w->vm.stack,
          err
      );

      if (frc == EPA_FLOW_ERR) {
        w->faulted = 1;
        kdbg_emit(k, EPA_KDBG_EXCEPT, (uint8_t)i, 0xFFFF0002u, &w->vm.eip, err);
        return 0;
      }

      if (frc == EPA_FLOW_OK) {
        w->halted = 1;
        // advance cursor to next worker for fairness
        k->impl.rr_cursor = (i + 1) % EPA_MAX_WORKERS;
        if (i == 0) return 1; // kernel ended
        // kernel continues; go to next worker
      } else if (frc == EPA_FLOW_NOT_FLOW) {
        EpaNonFlowRc nrc = k->nf.vt->exec_one(k->nf.impl, k->vp, &k->prog, w, &w->vm.eip, err);

        if (nrc == EPA_NF_EXEC_ERR) {
          w->faulted = 1;
          kdbg_emit(k, EPA_KDBG_EXCEPT, (uint8_t)i, 0xFFFF0003u, &w->vm.eip, err);
          return 0;
        }

        if (nrc == EPA_NF_EXEC_HALT) {
          w->halted = 1;
          k->impl.rr_cursor = (i + 1) % EPA_MAX_WORKERS;
          if (i == 0) return 1;
        }
      } else {
        // EPA_FLOW_YIELDED or other yield-like outcome:
        // advance cursor so resume behaves as if no pause happened
        k->impl.rr_cursor = (i + 1) % EPA_MAX_WORKERS;
      }

      // --- SAFE INTERRUPT BOUNDARY: AFTER completing this worker tick ---
      if (k->impl.interrupt_requested) {
        // IMPORTANT: resume should start at next worker, not current
        k->impl.rr_cursor = (i + 1) % EPA_MAX_WORKERS;

        // clear flag
        k->impl.interrupt_requested = 0;

        // Tell caller we yielded to host (CUDA-like boundary)
        return 2;
      }
    }

    if (!any_ran) {
      int any_at_running = 0;
      for (uint32_t wid = 0; wid < EPA_MAX_WORKERS; wid++) {
        if (k->impl.workers[wid].inited && k->impl.workers[wid].at_running) {
          any_at_running = 1;
          break;
        }
      }

      if (any_at_running) {
        continue;
      }

      return 1;
    }
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
