#include "epa_flow_glue.h"
#include <stdio.h>

int epa_resolve_eip(
    const EpaProgramDesc *prog,
    const EpaEip *eip,
    const uint8_t **out_code, size_t *out_len,
    uint32_t *out_abs_base,
    char err[EPA_MAX_ERR]
) {
  if (err) err[0] = 0;
  if (!prog || !eip || !out_code || !out_len) {
    snprintf(err, EPA_MAX_ERR, "resolve_eip: NULL arg");
    return 0;
  }

  if (eip->block_type == EPA_BLOCK_ENTRY) {
    if (eip->block_id >= 256 || !prog->entry_present[eip->block_id]) {
      snprintf(err, EPA_MAX_ERR, "resolve_eip: missing entry %u", (unsigned)eip->block_id);
      return 0;
    }
    *out_code = prog->entries[eip->block_id].code;
    *out_len  = prog->entries[eip->block_id].code_len;
    if (out_abs_base) *out_abs_base = prog->entries[eip->block_id].abs_base;
    return 1;
  }

  if (eip->block_type == EPA_BLOCK_FUNC) {
    if (eip->block_id >= prog->nfuncs) {
      snprintf(err, EPA_MAX_ERR, "resolve_eip: missing func %u", (unsigned)eip->block_id);
      return 0;
    }
    *out_code = prog->funcs[eip->block_id].code.code;
    *out_len  = prog->funcs[eip->block_id].code.code_len;
    if (out_abs_base) *out_abs_base = prog->funcs[eip->block_id].code.abs_base;
    return 1;
  }

  if (eip->block_type == EPA_BLOCK_AT_ENTRY) {
    if (eip->block_id >= prog->nat_entries) {
      snprintf(err, EPA_MAX_ERR, "resolve_eip: missing at_entry %u", (unsigned)eip->block_id);
      return 0;
    }
    *out_code = prog->at_entries[eip->block_id].code.code;
    *out_len  = prog->at_entries[eip->block_id].code.code_len;
    if (out_abs_base) *out_abs_base = prog->at_entries[eip->block_id].code.abs_base;
    return 1;
  }

  snprintf(err, EPA_MAX_ERR, "resolve_eip: invalid block_type %u", (unsigned)eip->block_type);
  return 0;
}
