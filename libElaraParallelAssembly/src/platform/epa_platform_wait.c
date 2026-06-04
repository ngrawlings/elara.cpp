#include "platform/epa_platform_wait.h"

#include <errno.h>
#include <time.h>

int epa_platform_cond_wait(pthread_cond_t *cv, pthread_mutex_t *mu) {
  return pthread_cond_wait(cv, mu);
}

int epa_platform_cond_broadcast(pthread_cond_t *cv) {
  return pthread_cond_broadcast(cv);
}

void epa_platform_micro_sleep(uint32_t usec) {
  struct timespec ts;
  if (usec == 0u) return;
  ts.tv_sec = (time_t)(usec / 1000000u);
  ts.tv_nsec = (long)(usec % 1000000u) * 1000L;
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
}
