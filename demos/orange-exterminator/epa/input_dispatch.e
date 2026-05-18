#include "engine_common.em"

// Input dispatch kernel.
// Intended mesh links:
// - ingress from host
// - output toward gameplay rules and UI kernels

kernel(VM vm) {
  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 1) {
      KeyInput input = kernal_get_ghs(1);
      // TODO: classify host input into gameplay/UI intent lanes.
      log("input key wid={d}", wid);
    } else if (wid == 2) {
      FrameTick tick = kernal_get_ghs(2);
      // TODO: reset transient input edges for the next frame.
      log("input tick wid={d}", wid);
    } else {
      log("input unknown wid={d}", wid);
    }
  }
}

worker input_key_ingress(KeyInput input) {
  // TODO: capture host key or pointer input.
  kernel_signal();
}

worker input_tick_reset(FrameTick tick) {
  // TODO: clear one-frame input latches here.
  kernel_signal();
}
