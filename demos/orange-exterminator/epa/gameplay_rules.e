#include "engine_common.em"

// Gameplay rules kernel.
// Intended mesh links:
// - ingress from entry tick
// - ingress from input dispatch
// - output toward player avatar / world simulation / weapon kernel

kernel(VM vm) {
  request_threads(2);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    // Coordinator only for now.
  }
}

worker gameplay_tick_ingress(FrameTick tick) {
  // TODO: prepare per-frame gameplay state.
  kernel_signal();
}

worker gameplay_player_intent(PlayerIntent intent) {
  // TODO: convert player intent into weapon/world/player commands.
  kernel_signal();
}

worker gameplay_actor_feedback(ActorState actor) {
  // TODO: gather actor state produced by child kernels.
  kernel_signal();
}
