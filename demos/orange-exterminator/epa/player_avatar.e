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

  frame_begin(1280, 720, 23, 28, 38);
  frame_rect(0, 380, 1280, 340, 58, 62, 50);
  frame_line(160, 620, 1120, 620, 2, 132, 132, 118);
  frame_line(220, 560, 1060, 560, 1, 96, 102, 90);
  frame_line(280, 500, 1000, 500, 1, 86, 92, 84);
  frame_line(340, 440, 940, 440, 1, 80, 86, 80);
  frame_line(400, 380, 880, 380, 1, 74, 80, 78);
  frame_line(640, 250, 640, 620, 1, 82, 88, 84);
  frame_line(500, 380, 420, 620, 1, 82, 88, 84);
  frame_line(780, 380, 860, 620, 1, 82, 88, 84);
  frame_rect(x, y, size, size, 240, 120, 24);
  frame_rect(x + 8, y + 8, size - 16, size - 16, 252, 176, 76);
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
