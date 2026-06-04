#include "epa_kernel.h"
#include "epa_kernel_internal.h"
#include "epa_kernel_hooks.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  EpaKernel *k;
  const EpaSystemAtRequestRecord *req;
  uint32_t first_tid;
  uint32_t stride;
  uint32_t virtual_threads;
  uint32_t ghs_lo;
  uint32_t ghs_hi;
  int ok;
  char err[EPA_MAX_ERR];
} CpuAtThreadArg;

static int pop_at_request(EpaKernel *k, EpaSystemAtRequestRecord *out) {
  EpaSystemAtRequestRecord *slot;
  if (!k || !out) return 0;
  memset(out, 0, sizeof(*out));

  pthread_mutex_lock(&k->impl.atq_mu);
  if (k->impl.atq.count == 0u) {
    pthread_mutex_unlock(&k->impl.atq_mu);
    return 0;
  }

  slot = &k->impl.atq.q[k->impl.atq.head];
  *out = *slot;
  memset(slot, 0, sizeof(*slot));
  k->impl.atq.head = (k->impl.atq.head + 1u) % EPA_SYSTEM_AT_QMAX;
  k->impl.atq.count--;
  pthread_mutex_unlock(&k->impl.atq_mu);
  return 1;
}

static int run_at_entry_thread(EpaKernel *k,
                               const EpaSystemAtRequestRecord *req,
                               uint32_t thread_index,
                               uint32_t thread_count,
                               uint32_t ghs_lo,
                               uint32_t ghs_hi,
                               char err[EPA_MAX_ERR]) {
  EpaWorkerState atw;
  uint32_t guard;

  if (!k || !req) {
    snprintf(err, EPA_MAX_ERR, "cpu_at_dispatch: null argument");
    return 0;
  }

  memset(&atw, 0, sizeof(atw));
  if (!epa_worker_init(&atw,
                       req->wid,
                       0u,
                       0u,
                       1u,
                       1u,
                       0u,
                       err)) {
    return 0;
  }

  atw.vm.eip.block_type = EPA_BLOCK_AT_ENTRY;
  atw.vm.eip.block_id = (uint16_t)req->at_entry_index;
  atw.vm.eip.rel_pc = 0u;
  atw.vm.csc[0] = (int32_t)thread_index;
  atw.vm.csc[1] = (int32_t)ghs_lo;
  atw.vm.csc[2] = (int32_t)ghs_hi;
  atw.vm.csc[3] = (int32_t)thread_count;

  for (guard = 0u; guard < 1000000u; guard++) {
    EpaFlowRc frc = epa_flow_step(k, &k->flow, &atw, (EpaStack*)&atw.vm.stack, err);

    if (frc == EPA_FLOW_ERR) {
      epa_print_fault_location(k, req->wid, &atw.vm.eip, err);
      kdbg_emit(k, EPA_KDBG_EXCEPT, (uint8_t)req->wid, 0xFFFF0101u, &atw.vm.eip, err);
      epa_worker_free(&atw);
      return 0;
    }

    if (frc == EPA_FLOW_OK) {
      epa_worker_free(&atw);
      return 1;
    }

    if (frc == EPA_FLOW_NOT_FLOW) {
      EpaNonFlowRc nrc;
      if (!k->nf.vt || !k->nf.vt->exec_one) {
        snprintf(err, EPA_MAX_ERR, "cpu_at_dispatch: non-flow backend missing");
        epa_worker_free(&atw);
        return 0;
      }
      nrc = k->nf.vt->exec_one(k->nf.impl, &k->prog, &atw, &atw.vm.eip, err);
      if (nrc == EPA_NF_EXEC_ERR) {
        epa_print_fault_location(k, req->wid, &atw.vm.eip, err);
        kdbg_emit(k, EPA_KDBG_EXCEPT, (uint8_t)req->wid, 0xFFFF0102u, &atw.vm.eip, err);
        epa_worker_free(&atw);
        return 0;
      }
      if (nrc == EPA_NF_EXEC_HALT) {
        epa_worker_free(&atw);
        return 1;
      }
      if (nrc == EPA_NF_EXEC_NOT_MINE) {
        snprintf(err, EPA_MAX_ERR, "cpu_at_dispatch: non-flow backend declined opcode");
        epa_worker_free(&atw);
        return 0;
      }
    }
  }

  snprintf(err, EPA_MAX_ERR, "cpu_at_dispatch: at_entry exceeded instruction guard");
  epa_worker_free(&atw);
  return 0;
}

static void *cpu_at_thread_main(void *argp) {
  CpuAtThreadArg *a = (CpuAtThreadArg*)argp;
  a->ok = 1;
  for (uint32_t tid = a->first_tid; tid < a->virtual_threads; tid += a->stride) {
    if (!run_at_entry_thread(a->k, a->req, tid, a->virtual_threads, a->ghs_lo, a->ghs_hi, a->err)) {
      a->ok = 0;
      break;
    }
  }
  return NULL;
}

int epa_kernel_dispatch_at_requests_cpu(EpaKernel *k, char err[EPA_MAX_ERR]) {
  EpaSystemAtRequestRecord req;

  if (err) err[0] = 0;
  if (!k) {
    snprintf(err, EPA_MAX_ERR, "cpu_at_dispatch: kernel null");
    return 0;
  }

  while (pop_at_request(k, &req)) {
    uint32_t requested_threads;
    uint32_t real_threads;
    uint32_t param_words;
    uint32_t ghs_lo;
    uint32_t ghs_hi;

    if (!req.descriptor_words || req.descriptor_word_count < 8u) {
      free(req.descriptor_words);
      snprintf(err, EPA_MAX_ERR, "cpu_at_dispatch: malformed descriptor");
      return 0;
    }

    requested_threads = req.descriptor_words[2];
    real_threads = req.descriptor_words[3];
    param_words = req.descriptor_words[4];
    if (requested_threads == 0u || param_words < 2u || req.descriptor_word_count < 8u) {
      free(req.descriptor_words);
      snprintf(err, EPA_MAX_ERR, "cpu_at_dispatch: bad AT descriptor header");
      return 0;
    }
    if (real_threads == 0u) real_threads = 1u;
    if (real_threads > requested_threads) real_threads = requested_threads;
    if (real_threads > 1024u) real_threads = 1024u;

    ghs_lo = req.descriptor_words[6];
    ghs_hi = req.descriptor_words[7];

    kdbg_emit(k, EPA_KDBG_SIGNAL, (uint8_t)req.wid,
              (uint32_t)(req.request_id & 0xffffffffu),
              NULL, "cpu_at_dispatch_begin");

    if (real_threads == 1u) {
      for (uint32_t tid = 0u; tid < requested_threads; tid++) {
        if (!run_at_entry_thread(k, &req, tid, requested_threads, ghs_lo, ghs_hi, err)) {
          free(req.descriptor_words);
          return 0;
        }
      }
    } else {
      pthread_t *threads = (pthread_t*)calloc(real_threads, sizeof(pthread_t));
      CpuAtThreadArg *args = (CpuAtThreadArg*)calloc(real_threads, sizeof(CpuAtThreadArg));
      if (!threads || !args) {
        free(threads);
        free(args);
        free(req.descriptor_words);
        snprintf(err, EPA_MAX_ERR, "cpu_at_dispatch: OOM spawning %u real threads", (unsigned)real_threads);
        return 0;
      }
      for (uint32_t i = 0u; i < real_threads; i++) {
        args[i].k = k;
        args[i].req = &req;
        args[i].first_tid = i;
        args[i].stride = real_threads;
        args[i].virtual_threads = requested_threads;
        args[i].ghs_lo = ghs_lo;
        args[i].ghs_hi = ghs_hi;
        args[i].ok = 0;
        args[i].err[0] = 0;
        if (pthread_create(&threads[i], NULL, cpu_at_thread_main, &args[i]) != 0) {
          for (uint32_t j = 0u; j < i; j++) pthread_join(threads[j], NULL);
          free(threads);
          free(args);
          free(req.descriptor_words);
          snprintf(err, EPA_MAX_ERR, "cpu_at_dispatch: pthread_create failed");
          return 0;
        }
      }
      for (uint32_t i = 0u; i < real_threads; i++) {
        pthread_join(threads[i], NULL);
      }
      for (uint32_t i = 0u; i < real_threads; i++) {
        if (!args[i].ok) {
          snprintf(err, EPA_MAX_ERR, "%s", args[i].err[0] ? args[i].err : "cpu_at_dispatch: AT worker failed");
          free(threads);
          free(args);
          free(req.descriptor_words);
          return 0;
        }
      }
      free(threads);
      free(args);
    }

    kdbg_emit(k, EPA_KDBG_SIGNAL, (uint8_t)req.wid,
              (uint32_t)(req.request_id & 0xffffffffu),
              NULL, "cpu_at_dispatch_end");
    free(req.descriptor_words);
  }

  return 1;
}
