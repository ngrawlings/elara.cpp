#include "engine_common.em"

// UI render kernel.
// Intended mesh links:
// - ingress from input and weapon/gameplay state
// - output HudCommand / UI render products toward entry kernel

kernel(VM vm) {
  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 1) {
      KeyInput input = kernal_get_ghs(1);
      // TODO: drive menu/focus/hotbar selection state.
      log("render ui input wid={d}", wid);
    } else if (wid == 2) {
      WeaponCommand weapon = kernal_get_ghs(2);
      // TODO: update HUD ammo and weapon affordance state.
      log("render ui weapon wid={d}", wid);
    } else if (wid == 3) {
      FrameTick tick = kernal_get_ghs(3);
      // TODO: advance transient UI animations.
      log("render ui tick wid={d}", wid);
    } else {
      log("render ui unknown wid={d}", wid);
    }
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
