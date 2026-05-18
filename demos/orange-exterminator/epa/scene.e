#include "engine_common.em"

// Scene kernel.
// Owns the current corridor test scene and camera movement.

kernel(VM vm) {
  request_threads(2);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    // Scene worker 1 owns the corridor state and emits the frame artifact
    // directly. The kernel only needs to stay alive as a coordinator.
  }
}

@attributes signal_mail_box_size:2048
worker scene_corridor_input(KeyInput input) {
  int initialized;
  int lane;
  int depth;
  int key_code = input.key_code;
  int pressed = input.pressed;
  int spread;
  int inner;
  int dead_x;
  int dead_y;
  int dead_w;
  int dead_h;
  int glow_x;
  int glow_y;
  int glow_w;
  int glow_h;
  int center_x;
  int wall_outer;
  int wall_inner;
  int far_y;
  int mid_y;

  if (initialized) {
  } else {
    initialized = 1;
    lane = 0;
    depth = 0;
  }

  if (pressed) {
    if (key_code == 65361) {
      lane = lane - 24;
    } else if (key_code == 65363) {
      lane = lane + 24;
    } else if (key_code == 65362) {
      if (depth == 6) {
      } else {
        depth = depth + 1;
      }
    } else if (key_code == 65364) {
      if (depth == 0) {
      } else {
        depth = depth - 1;
      }
    } else {
      // TODO: map more input into scene/camera state later.
    }
  }

  spread = depth * 90;
  inner = depth * 54;
  center_x = 640 + lane;
  dead_x = 535 + lane - (spread / 2);
  dead_y = 210 - (depth * 26);
  dead_w = 210 + (depth * 150);
  dead_h = 170 + (depth * 58);
  glow_x = 590 + lane - (depth * 40);
  glow_y = 248 - (depth * 22);
  glow_w = 100 + (depth * 78);
  glow_h = 72 + (depth * 36);
  wall_outer = 210 + (depth * 18);
  wall_inner = 340 + (depth * 24);
  far_y = 330 - (depth * 18);
  mid_y = 410 - (depth * 12);

  frame_begin(1280, 720, 12, 14, 18);
  frame_rect(0, 0, 1280, 250, 24, 28, 34);
  frame_rect(0, 250, 1280, 120, 52, 56, 60);
  frame_rect(0, 370, 1280, 350, 92, 72, 46);
  frame_rect(0, 250, 210, 470, 26, 28, 32);
  frame_rect(1070, 250, 210, 470, 26, 28, 32);
  frame_rect(dead_x, dead_y, dead_w, dead_h, 70, 74, 78);
  frame_rect(glow_x, glow_y, glow_w, glow_h, 170, 146, 108);
  frame_line(wall_outer + lane, 720, 560 + lane - spread, far_y, 3, 118, 102, 78);
  frame_line(1280 - wall_outer + lane, 720, 720 + lane + spread, far_y, 3, 118, 102, 78);
  frame_line(wall_inner + lane, 720, 595 + lane - inner, 370, 2, 150, 134, 104);
  frame_line(1280 - wall_inner + lane, 720, 685 + lane + inner, 370, 2, 150, 134, 104);
  frame_line(470 + lane, 720, 625 + lane - inner, mid_y, 2, 186, 170, 138);
  frame_line(810 + lane, 720, 655 + lane + inner, mid_y, 2, 186, 170, 138);
  frame_line(center_x, 200, center_x, 650, 1, 126, 130, 134);
  frame_line(0, 370, 1280, 370, 2, 132, 116, 84);
  frame_line(0, 250, 1280, 250, 1, 86, 90, 96);
  frame_rect(592, 654, 96, 28, 36, 38, 42);
  frame_rect(610, 662, 60, 12, 118, 122, 126);
  frame_commit();

  kernel_signal();
}
