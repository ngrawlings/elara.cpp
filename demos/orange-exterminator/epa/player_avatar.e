#include "engine_common.em"

// Player avatar kernel.
// Intended mesh links:
// - ingress from gameplay rules
// - ingress from world state
// - output actor state to gameplay and render.scene

kernel(VM vm) {
  request_threads(2);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 1) {
      KeyInput input = kernal_get_ghs(1);
      // TODO: this worker now owns the first real controllable player state.
      log("player input wid={d}", wid);
    } else if (wid == 2) {
      WorldState world = kernal_get_ghs(2);
      // TODO: fold world feedback into movement/collision posture.
      log("player world wid={d}", wid);
    } else if (wid == 3) {
      WeaponCommand weapon = kernal_get_ghs(3);
      // TODO: integrate current weapon feedback into avatar state.
      log("player weapon wid={d}", wid);
    } else {
      log("player unknown wid={d}", wid);
    }
  }
}

@attributes signal_mail_box_size:2048
worker player_intent_ingress(KeyInput input) {
  int initialized;
  int x;
  int y;
  int size;
  int key_code = input.key_code;
  int pressed = input.pressed;

  if (initialized) {
  } else {
    initialized = 1;
    x = 640;
    y = 360;
    size = 48;
  }

  if (pressed) {
    if (key_code == 65361) {
      x = x - 24;
    } else if (key_code == 65363) {
      x = x + 24;
    } else if (key_code == 65362) {
      y = y - 24;
    } else if (key_code == 65364) {
      y = y + 24;
    } else {
      // TODO: later map additional keys to gameplay actions.
    }
  }

  frame_begin(1280, 720, 12, 14, 18);
  frame_rect(0, 0, 1280, 250, 24, 28, 34);
  frame_rect(0, 250, 1280, 120, 52, 56, 60);
  frame_rect(0, 370, 1280, 350, 92, 72, 46);
  frame_rect(0, 250, 210, 470, 26, 28, 32);
  frame_rect(1070, 250, 210, 470, 26, 28, 32);
  frame_rect(535, 180, 210, 190, 70, 74, 78);
  frame_rect(590, 228, 100, 94, 170, 146, 108);
  frame_line(210, 720, 560, 310, 2, 118, 102, 78);
  frame_line(1070, 720, 720, 310, 2, 118, 102, 78);
  frame_line(340, 720, 595, 370, 2, 150, 134, 104);
  frame_line(940, 720, 685, 370, 2, 150, 134, 104);
  frame_line(470, 720, 625, 430, 1, 186, 170, 138);
  frame_line(810, 720, 655, 430, 1, 186, 170, 138);
  frame_line(640, 200, 640, 650, 1, 126, 130, 134);
  frame_line(0, 370, 1280, 370, 2, 132, 116, 84);
  frame_line(0, 250, 1280, 250, 1, 86, 90, 96);
  frame_rect(x, y, size, size, 252, 128, 18);
  frame_rect(x + 8, y + 8, size - 16, size - 16, 255, 210, 96);
  frame_commit();

  kernel_signal();
}

worker player_world_feedback(WorldState world) {
  // TODO: apply world collision/zone feedback here.
  kernel_signal();
}

worker player_weapon_feedback(WeaponCommand weapon) {
  // TODO: reflect weapon recoil, cooldown, or state changes.
  kernel_signal();
}
