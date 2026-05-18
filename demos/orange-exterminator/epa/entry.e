#include "engine_common.em"

// Root frame kernel.
// Intended role:
// - own the frame tick
// - observe child kernel outputs
// - later far-signal domain kernels once local payload codegen is more complete

kernel(VM vm) {
  request_threads(3);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 1) {
      FrameTick tick = kernal_get_ghs(1);
      // TODO: route frame tick to gameplay, world, and render domain kernels.
      log("entry tick worker signalled wid={d}", wid);
    } else if (wid == 2) {
      RenderProduct render_product = kernal_get_ghs(2);
      // TODO: integrate scene output and decide frame completion.
      log("entry render worker signalled wid={d}", wid);
    } else if (wid == 3) {
      HudCommand hud = kernal_get_ghs(3);
      // TODO: merge HUD state before final present/present-signal path.
      log("entry hud worker signalled wid={d}", wid);
    } else {
      log("entry unknown worker wid={d}", wid);
    }
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
