#pragma once
#include "epa_kernel.h"

// Internal kernel helpers needed by scheduler profiles.
// (These are not part of the public embedding API.)
int epa_kernel_drain_ingress(EpaKernel *k, char err[EPA_MAX_ERR]);
int epa_kernel_dispatch_at_requests_cpu(EpaKernel *k, char err[EPA_MAX_ERR]);
int epa_kernel_dispatch_memory_requests_cpu(EpaKernel *k, char err[EPA_MAX_ERR]);
