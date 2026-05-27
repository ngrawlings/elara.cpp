#include "engine_common.em"

// Scene render kernel.
// Intended mesh links:
// - ingress from frame tick
// - ingress from player/world state
// - later ingress from wall visibility / slice output
// - output RenderProduct back to entry kernel

kernel(VM vm) {
  kernalId("orange.exterminator.render_scene");
  request_threads(4);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    // Coordinator only for now.
  }
}

worker render_scene_tick_ingress(FrameTick tick) {
  // TODO: prepare the scene build pass for the current frame.
  kernel_signal();
}

worker render_scene_actor_ingress(ActorState actor) {
  // TODO: ingest actor render state here.
  kernel_signal();
}

worker render_scene_world_ingress(WorldState world) {
  // TODO: ingest world render state here.
  kernel_signal();
}
