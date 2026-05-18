#include "engine_common.em"

// Scene kernel.
// Owns the 3D camera state and corridor rendering.
// Camera position: cam_x, cam_y, cam_z (world units, 8 per step).
// Camera orientation: cam_yaw (0..719 ticks, 720 per full circle),
//                    cam_pitch (ticks, clamped to -90..+90).
// Mouse look: look_dx -> yaw, look_dy -> pitch (down = positive pitch).
// Movement: move_x -> strafe, move_z -> forward/back.
// Collision is stubbed to always allow movement.

kernel(VM vm) {
  request_threads(2);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    // Worker 1 owns all camera state and frame output.
  }
}

@attributes signal_mail_box_size:2048
worker scene_camera_update(CameraInput input) {
  int cam_x;
  int cam_y;
  int cam_z;
  int cam_yaw;
  int cam_pitch;
  int initialized;

  if (initialized) {
  } else {
    initialized = 1;
    cam_x = 0;
    cam_y = 0;
    cam_z = 0;
    cam_yaw = 0;
    cam_pitch = 0;
  }

  // --- Mouse look ---
  // Sensitivity: divide raw mouse pixel delta by 5.
  cam_yaw = cam_yaw + (input.look_dx / 5);
  cam_pitch = cam_pitch + (input.look_dy / 5);

  // Wrap yaw to 0..719 (720 ticks = 360 degrees).
  // Add a large multiple of 720 before modulo to ensure positive dividend.
  cam_yaw = (cam_yaw + 7200) - (((cam_yaw + 7200) / 720) * 720);

  // Clamp pitch: wrap into -90..+89 range using mod 180.
  // Add 90 to shift to 0..179, mod 180, subtract 90.
  cam_pitch = ((cam_pitch + 90 + 18000) - (((cam_pitch + 90 + 18000) / 180) * 180)) - 90;

  // --- Keyboard movement (collision always allows) ---
  // move_x: -1 = strafe left, +1 = strafe right
  // move_z: -1 = back, +1 = forward
  cam_x = cam_x + (input.move_x * 8);
  cam_z = cam_z + (input.move_z * 8);

  // --- Derive view parameters ---

  // Depth cycles through 0..6 as the player moves along z.
  // Add large multiple of 7 to avoid negative modulo issues.
  int depth = ((cam_z / 64) + 7000) - ((((cam_z / 64) + 7000) / 7) * 7);

  // Yaw deviation from straight ahead: range -360..359.
  // Shift cam_yaw by 360, floor-divide by 720 to get a correction factor,
  // then subtract that many full circles.
  int yaw_dev = cam_yaw - (((cam_yaw + 360) / 720) * 720);

  // Lateral offset combines world x position and yaw lean.
  // 1 yaw tick = 2 pixels of vanishing-point shift.
  int lane = (cam_x / 64) - (yaw_dev * 2);

  // Pitch shifts the horizon and ceiling bands.
  // 1 pitch tick = 2 pixels. Positive pitch (looking down) raises the horizon.
  int pitch_adj = cam_pitch * 2;
  int ceiling_y = 250 - pitch_adj;
  int horizon_y = 370 - pitch_adj;

  if (ceiling_y == ceiling_y) {
    // clamp ceiling_y to 0..680 via arithmetic
    // ceiling_y = max(0, min(680, ceiling_y))
    // Use: clamp = ((x + abs_margin) - abs(x + abs_margin - mid)) / 2 + mid/2
    // Simpler: keep it unbounded for now — extreme pitch wraps due to pitch clamp above.
  }

  // Corridor perspective geometry derived from camera state.
  int spread = depth * 90;
  int inner = depth * 54;
  int center_x = 640 + lane;
  int dead_x = center_x - 105 - (spread / 2);
  int dead_y = ceiling_y - 40 - (depth * 26);
  int dead_w = 210 + (depth * 150);
  int dead_h = 170 + (depth * 58);
  int glow_x = center_x - 50 - (depth * 40);
  int glow_y = ceiling_y - 2 - (depth * 22);
  int glow_w = 100 + (depth * 78);
  int glow_h = 72 + (depth * 36);
  int wall_outer = 210 + (depth * 18);
  int wall_inner = 340 + (depth * 24);
  int far_y = horizon_y - 40 - (depth * 18);
  int mid_y = horizon_y + 40 - (depth * 12);
  int wall_h = horizon_y - ceiling_y;

  // --- Render ---
  frame_begin(1280, 720, 12, 14, 18);

  // Sky / ceiling band.
  frame_rect(0, 0, 1280, ceiling_y, 24, 28, 34);
  // Upper wall band between ceiling and horizon.
  frame_rect(0, ceiling_y, 1280, wall_h, 52, 56, 60);
  // Floor from horizon to bottom.
  frame_rect(0, horizon_y, 1280, 720 - horizon_y, 92, 72, 46);

  // Side wall panels (solid, no perspective detail needed).
  frame_rect(0, ceiling_y, wall_outer + lane, wall_h, 26, 28, 32);
  frame_rect(1280 - wall_outer + lane, ceiling_y, wall_outer - lane, wall_h, 26, 28, 32);

  // Back wall (dead end) and glow highlight.
  frame_rect(dead_x, dead_y, dead_w, dead_h, 70, 74, 78);
  frame_rect(glow_x, glow_y, glow_w, glow_h, 170, 146, 108);

  // Perspective wall lines converging to vanishing point.
  frame_line(wall_outer + lane, 720, center_x - 80 - spread, far_y, 3, 118, 102, 78);
  frame_line(1280 - wall_outer + lane, 720, center_x + 80 + spread, far_y, 3, 118, 102, 78);
  frame_line(wall_inner + lane, 720, center_x - 45 - inner, horizon_y, 2, 150, 134, 104);
  frame_line(1280 - wall_inner + lane, 720, center_x + 45 + inner, horizon_y, 2, 150, 134, 104);
  frame_line(470 + lane, 720, center_x - 15 - inner, mid_y, 2, 186, 170, 138);
  frame_line(810 + lane, 720, center_x + 15 + inner, mid_y, 2, 186, 170, 138);

  // Centre crosshair guide line and horizon/ceiling markers.
  frame_line(center_x, ceiling_y + 50, center_x, 650, 1, 126, 130, 134);
  frame_line(0, horizon_y, 1280, horizon_y, 2, 132, 116, 84);
  frame_line(0, ceiling_y, 1280, ceiling_y, 1, 86, 90, 96);

  // HUD bar (fixed position).
  frame_rect(592, 654, 96, 28, 36, 38, 42);
  frame_rect(610, 662, 60, 12, 118, 122, 126);

  frame_commit();

  kernel_signal();
}
