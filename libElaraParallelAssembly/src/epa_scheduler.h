#pragma once
#include <stdint.h>

#define EPA_MAX_ERR 256

struct EpaKernel;
struct EpaSchedState;

/* Scheduler profiles */
typedef enum {
  EPA_SCHED_WAVE = 1,
  EPA_SCHED_CPU_THREAD,
  EPA_SCHED_CUDA_HOTIDLE,
} EpaSchedProfile;

/* Scheduler virtual table */
typedef struct {
  const char *name;

  /* Optional lifecycle */
  int  (*init)(struct EpaKernel *k, struct EpaSchedState *s, char err[EPA_MAX_ERR]);
  void (*destroy)(struct EpaKernel *k, struct EpaSchedState *s);

  /* Control */
  void (*request_interrupt)(struct EpaKernel *k, struct EpaSchedState *s);
  void (*wake)(struct EpaKernel *k, struct EpaSchedState *s);

  /* Run loop */
  int (*run)(struct EpaKernel *k,
             struct EpaSchedState *s,
             uint32_t max_ticks,
             int debug,
             char err[EPA_MAX_ERR]);
} EpaSchedulerVt;

/* Opaque per-scheduler state */
typedef struct EpaSchedState {
  int interrupt_requested;
  void *opaque;
} EpaSchedState;

int epa_sched_cpu_thread_add_threads(struct EpaKernel *k,
                                     struct EpaSchedState *s,
                                     uint32_t add_count,
                                     char err[EPA_MAX_ERR]);
uint32_t epa_sched_cpu_thread_thread_count(struct EpaSchedState *s);
