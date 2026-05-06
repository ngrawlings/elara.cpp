#pragma once

#include "memory/epa_ghs.h"

epa_ghs_handle_t epa_h_from_regs(uint32_t idx, uint32_t gen);
int epa_ghs_require_owner(epa_ghs_t* ghs, epa_ghs_handle_t h, uint32_t caller);
