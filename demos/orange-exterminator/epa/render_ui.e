#include "engine_common.em"

// UI render kernel.
// Intended mesh links:
// - ingress from input and weapon/gameplay state
// - output HudCommand / UI render products toward entry kernel

kernel(VM vm) {
  while (kernel_wait_signal()) {
    // Coordinator only for now.
  }
}

worker render_ui_input_ingress(KeyInput input) {
  // TODO: turn raw input into UI-local state.
  kernel_signal();
}

worker render_ui_weapon_ingress(WeaponCommand weapon) {
  // TODO: receive gameplay/weapon HUD deltas here.
  kernel_signal();
}

worker render_ui_tick_ingress(FrameTick tick) {
  // TODO: update UI animation clocks or transitions.
  kernel_signal();
}
