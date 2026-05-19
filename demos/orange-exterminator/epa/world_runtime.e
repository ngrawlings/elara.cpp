#include "engine_common.em"

// World runtime kernel.
// Intended mesh links:
// - ingress from frame tick
// - ingress from actor state
// - later far/local route wall primitives into the walls kernel
// - output world state toward player/render/gameplay

kernel(VM vm) {
  request_threads(2);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    // Coordinator only for now.
  }
}

worker world_tick_ingress(FrameTick tick) {
  // TODO: service world timeline events here.
  kernel_signal();
}

worker world_actor_feedback(ActorState actor) {
  // TODO: accept actor-state deltas relevant to world logic.
  kernel_signal();
}
