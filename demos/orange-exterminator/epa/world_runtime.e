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
    if (wid == 1) {
      FrameTick tick = kernal_get_ghs(1);
      // TODO: advance moving doors, triggers, lifts, hazards, and scripted state.
      log("world tick wid={d}", wid);
    } else if (wid == 2) {
      ActorState actor = kernal_get_ghs(2);
      // TODO: update zone occupancy and actor/world interaction state.
      log("world actor wid={d}", wid);
    } else {
      log("world unknown wid={d}", wid);
    }
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
