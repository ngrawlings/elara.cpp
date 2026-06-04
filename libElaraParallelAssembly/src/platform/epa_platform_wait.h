#pragma once

#include <stdint.h>

#if defined(__CUDACC__) && defined(__CUDA_ARCH__)
__device__ static inline void epa_platform_device_pause(uint32_t cycles) {
#if __CUDA_ARCH__ >= 700
  __nanosleep(cycles ? cycles : 1u);
#else
  volatile uint32_t i;
  for (i = 0; i < (cycles ? cycles : 1u); i++) {
  }
#endif
}
#endif

#if !defined(__CUDA_ARCH__)
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

int epa_platform_cond_wait(pthread_cond_t *cv, pthread_mutex_t *mu);
int epa_platform_cond_broadcast(pthread_cond_t *cv);
void epa_platform_micro_sleep(uint32_t usec);

#ifdef __cplusplus
}
#endif
#endif
