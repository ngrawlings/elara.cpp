#pragma once
#include <stdint.h>

#include "epa_atomic_tasks.h"
#include "../memory/epa_ghs.h"

int at_parallel_exec(EpaAtBatch *b, uint32_t vtid, int32_t tid, epa_ghs_t *ghs, epa_ghs_handle_t h);
