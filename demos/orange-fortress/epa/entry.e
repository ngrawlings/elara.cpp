#include "engine_common.em"

// Root frame kernel.
// Intended role:
// - own the frame tick
// - observe child kernel outputs
// - later far-signal domain kernels once local payload codegen is more complete

kernel(VM vm) {
  kernalId("orange.fortress.entry");
  request_threads(3);
  start_worker(entry_tick_ingress);
  start_worker(entry_render_feedback);
  start_worker(entry_hud_feedback);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    // Coordinator only for now.
  }
}

worker entry_tick_ingress(FrameTick tick) {
  // TODO: accept frame tick ingress from host and prepare domain dispatch payloads.
  kernel_signal();
}

worker entry_render_feedback(RenderProduct render_product) {
  // TODO: receive render-domain completion output.
  kernel_signal();
}

worker entry_hud_feedback(HudCommand hud) {
  // TODO: receive UI/HUD completion output.
  kernel_signal();
}
