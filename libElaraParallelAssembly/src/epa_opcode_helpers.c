#include "epa_opcode_helpers.h"
#include "log.h"

epa_ghs_handle_t epa_h_from_regs(uint32_t idx, uint32_t gen) {
  return epa_ghs_make_handle(idx, gen);
}

// returns 1 if ok, else writes err and returns 0
int epa_ghs_require_owner(epa_ghs_t* ghs, epa_ghs_handle_t h, uint32_t caller) {
  epa_ghs_meta_t m;
  epa_ghs_err_t ge = epa_ghs_get_meta(ghs, h, &m);
  if (ge != EPA_GHS_OK) {
    TRACE("GHS bad handle (idx=%u gen=%u)", epa_ghs_handle_index(h), epa_ghs_handle_gen(h));
    return 0;
  }
  if (m.owner != caller) {
    TRACE("GHS owner violation (owner=%u caller=%u)", m.owner, caller);
    return 0;
  }
  return 1;
}
