#include "platform/epa_platform_wait.h"

#include <cuda_runtime.h>

extern "C" __global__ void epa_cuda_smoke_kernel(unsigned int *out) {
  epa_platform_device_pause(1u);
  if (threadIdx.x == 0 && blockIdx.x == 0 && out) {
    out[0] = 0xE9A00001u;
  }
}

extern "C" int epa_cuda_smoke_host_probe(void) {
  int count = 0;
  cudaError_t rc = cudaGetDeviceCount(&count);
  return (rc == cudaSuccess && count > 0) ? count : 0;
}
