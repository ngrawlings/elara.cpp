#include "engine_common.em"

// Player weapon kernel.
// Intended mesh links:
// - ingress from gameplay/player avatar
// - output weapon state to player avatar and render UI
// - later spawn effect/projectile kernels via far_signal(...)

kernel(VM vm) {
  while (kernel_wait_signal()) {
    // Coordinator only for now.
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
