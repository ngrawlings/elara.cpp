#include "engine_common.em"

// Input dispatch kernel.
// Intended mesh links:
// - ingress from host
// - output toward gameplay rules and UI kernels

kernel(VM vm) {
  kernalId("orange.exterminator.input_dispatch");
  int wid = 0;
  while (wid = kernel_wait_signal()) {
    // Coordinator only for now.
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
