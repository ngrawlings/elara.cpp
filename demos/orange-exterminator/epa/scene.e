#include "engine_common.em"

// Scene kernel.
// For now the host supplies a calibration-oriented projected scene payload.
// This keeps camera testing honest while EPA rendering stays artifact-driven.

kernel(VM vm) {
  request_threads(2);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    // Worker 1 owns the current calibration scene frame.
  }
}

@attributes signal_mail_box_size:2048
worker scene_camera_update(ScenePoseInput input) {
  frame_begin(1280, 720, 18, 20, 24);

  frame_rect(0, 0, 1280, 340, 42, 44, 40);
  frame_rect(0, 340, 1280, 380, 106, 88, 60);
  frame_rect(0, 332, 1280, 8, 78, 72, 64);

  if (input.end_wall_visible) {
    frame_rect(input.end_wall_x, input.end_wall_y, input.end_wall_w, input.end_wall_h, 112, 104, 88);
    frame_rect(input.end_wall_x + 18, input.end_wall_y + 18, input.end_wall_w - 36, input.end_wall_h - 36, 154, 142, 118);
  }

  if (input.side_wall_visible) {
    frame_rect(input.side_wall_x, input.side_wall_y, input.side_wall_w, input.side_wall_h, 86, 76, 64);
    frame_rect(input.side_wall_x + 8, input.side_wall_y + 8, input.side_wall_w - 16, input.side_wall_h - 16, 124, 108, 88);
  }

  frame_rect(638, 328, 4, 64, 210, 212, 216);
  frame_rect(608, 358, 64, 4, 210, 212, 216);

  if (input.marker0_visible) {
    frame_rect(input.marker0_x - 8, input.marker0_y - 8, 16, 16, 112, 224, 250);
    frame_rect(input.marker0_x - 3, input.marker0_y - 3, 6, 6, 240, 252, 255);
  }

  if (input.marker1_visible) {
    frame_rect(input.marker1_x - 8, input.marker1_y - 8, 16, 16, 242, 196, 78);
    frame_rect(input.marker1_x - 3, input.marker1_y - 3, 6, 6, 255, 244, 166);
  }

  if (input.marker2_visible) {
    frame_rect(input.marker2_x - 8, input.marker2_y - 8, 16, 16, 164, 234, 126);
    frame_rect(input.marker2_x - 3, input.marker2_y - 3, 6, 6, 244, 255, 220);
  }

  frame_commit();
}
