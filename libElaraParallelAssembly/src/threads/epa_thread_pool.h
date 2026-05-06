#pragma once
#include <stdint.h>
#include <pthread.h>
#include "../vm/epa_worker_state.h"
#include "../atomic_tasks/epa_atomic_tasks.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EpaThreadPool EpaThreadPool;


#ifndef EPA_TP_MAX_BATCHES
#define EPA_TP_MAX_BATCHES 256
#endif

#ifndef EPA_TP_THREAD_LBYTES
#define EPA_TP_THREAD_LBYTES (8u * 1024u)
#endif


// --------------------
// Simple ring queue of batches (protected by pool mutex)
// --------------------
typedef struct {
  EpaAtBatch *items[EPA_TP_MAX_BATCHES];
  uint32_t head, tail, count;
} BatchRing;

/*
  Thread pool is a generic container of OS threads.

  Threads wait until batches are submitted. A submitted batch defines:
  - how to claim unfinished subtasks (returns subtask index => VTID)
  - how to execute one subtask for a given VTID
  - how to mark completion

  This keeps threads generic and enables CUDA-like mapping:
    VTID == subtask index
*/

typedef struct {
  EpaExecType exec_type;
  EpaVM vm;

  // thread identity (scheduler slot)
  uint32_t id;  // 0..255 (entry slot)
  uint32_t vid;
} EpaThreadState;

// --------------------
// ThreadPool state
// --------------------
typedef struct {
  struct EpaThreadPool *p;
  uint32_t tid;

  // One VM per OS thread. Lives for thread lifetime.
  EpaVM vm;
  int   vm_inited;
} TpThreadCtx;

struct EpaThreadPool {
  pthread_t *threads;
  uint32_t   n_threads;

  // Stable per-thread VTID (subtask index). -1 when idle.
  int32_t   *vtid;

  pthread_mutex_t mu;
  pthread_cond_t  cv;
  int stop;

  BatchRing q;

  // how many threads are currently blocked in cond_wait
  _Atomic uint32_t waiting;

  // Per-thread contexts
  TpThreadCtx *tctx;
};

typedef struct EpaAtBatch EpaAtBatch;

/* Create/destroy */
int  epa_thread_pool_init(EpaThreadPool *p, uint32_t n_threads);
void epa_thread_pool_shutdown(EpaThreadPool *p);

/* Submit work */
int  epa_thread_pool_submit_batch(EpaThreadPool *p, EpaAtBatch *batch);

/* Wake sleeping threads (optional; submit already wakes) */
void epa_thread_pool_wake(EpaThreadPool *p);

/* Telemetry */
uint32_t epa_thread_pool_thread_count(EpaThreadPool *p);

/* Number of OS threads currently waiting (blocked on condvar). */
uint32_t epa_thread_pool_waiting_count(EpaThreadPool *p);

/* Current VTID for a physical thread (tid). -1 means idle/unassigned. */
int32_t  epa_thread_pool_get_vtid(EpaThreadPool *p, uint32_t tid);

// Returns the current OS thread's VM when called from inside a threadpool worker.
// Returns NULL if called from a non-pool thread.
EpaVM *epa_thread_pool_tls_vm(void);

#ifdef __cplusplus
}
#endif
