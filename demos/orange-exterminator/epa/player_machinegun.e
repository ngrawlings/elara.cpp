#include "engine_common.em"

// Player weapon kernel.
// Intended mesh links:
// - ingress from gameplay/player avatar
// - output weapon state to player avatar and render UI
// - later spawn effect/projectile kernels via far_signal(...)

kernel(VM vm) {
  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 1) {
      WeaponCommand command = kernal_get_ghs(1);
      // TODO: update fire/cooldown/ammo state.
      log("weapon command wid={d}", wid);
    } else if (wid == 2) {
      FrameTick tick = kernal_get_ghs(2);
      // TODO: advance firing timers and burst cadence.
      log("weapon tick wid={d}", wid);
    } else {
      log("weapon unknown wid={d}", wid);
    }
  }
}

worker weapon_command_ingress(WeaponCommand command) {
  // TODO: turn command ingress into weapon-local state transitions.
  kernel_signal();
}

worker weapon_tick_ingress(FrameTick tick) {
  // TODO: service time-based weapon logic per frame.
  kernel_signal();
}
