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
    if (wid == 1) {
      FrameTick tick = kernal_get_ghs(1);
      // TODO: advance high-level rules for this frame.
      log("gameplay tick wid={d}", wid);
    } else if (wid == 2) {
      PlayerIntent intent = kernal_get_ghs(2);
      // TODO: turn input intent into domain commands.
      log("gameplay intent wid={d}", wid);
    } else if (wid == 3) {
      ActorState actor = kernal_get_ghs(3);
      // TODO: fold actor feedback back into gameplay rules.
      log("gameplay actor wid={d}", wid);
    } else {
      log("gameplay unknown wid={d}", wid);
    }
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
