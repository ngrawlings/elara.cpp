#include "engine_common.em"

// Scene render kernel.
// Intended mesh links:
// - ingress from frame tick
// - ingress from player/world state
// - output RenderProduct back to entry kernel

kernel(VM vm) {
  request_threads(4);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 1) {
      FrameTick tick = kernal_get_ghs(1);
      // TODO: begin scene build for this frame.
      log("render scene tick wid={d}", wid);
    } else if (wid == 2) {
      ActorState actor = kernal_get_ghs(2);
      // TODO: fold actor transforms into the scene graph/product.
      log("render scene actor wid={d}", wid);
    } else if (wid == 3) {
      WorldState world = kernal_get_ghs(3);
      // TODO: fold world/static geometry state into the scene product.
      log("render scene world wid={d}", wid);
    } else {
      log("render scene unknown wid={d}", wid);
    }
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
