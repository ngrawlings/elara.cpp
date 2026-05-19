#include "engine_common.em"

// Scene kernel.
// Uses absolute pose input from the host-side coordinator for now.
// That avoids relying on worker-local state persistence while the core bridge is still being hardened.

kernel(VM vm) {
  request_threads(2);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    // Worker 1 owns the current corridor frame.
  }
}

@attributes signal_mail_box_size:2048
worker scene_camera_update(ScenePoseInput input) {
  int horizon = input.pitch * 3;
  int center_shift = input.yaw * 4;
  int wall_skew = input.yaw * 6;
  int marker_x = 0;
  int marker_y = 0;

  frame_begin(1280, 720, 12, 14, 18);

  if (input.depth == 1) {
    frame_rect(0, 0, 1280, 190 + horizon, 34, 36, 32);
    frame_rect(0, 190 + horizon, 1280, 170, 78, 70, 58);
    frame_rect(0, 360, 1280, 360, 112, 92, 62);

    frame_rect(0, 190 + horizon, 180 + wall_skew, 170, 48, 42, 34);
    frame_rect(1040 + center_shift - wall_skew, 190 + horizon, 240 + wall_skew, 170, 48, 42, 34);

    frame_rect(400 + input.lane + center_shift, 150 + horizon, 460, 320, 94, 86, 70);
    frame_rect(455 + input.lane + center_shift, 185 + horizon, 350, 235, 154, 138, 106);

    frame_rect(900 + input.lane + center_shift + (wall_skew / 2), 220 + horizon, 130, 210, 122, 108, 86);
    marker_x = 630 + input.lane + center_shift;
    marker_y = 302 + horizon;
  } else {
    frame_rect(0, 0, 1280, 220 + horizon, 34, 36, 32);
    frame_rect(0, 220 + horizon, 1280, 140, 78, 70, 58);
    frame_rect(0, 360, 1280, 360, 112, 92, 62);

    frame_rect(0, 220 + horizon, 150 + wall_skew, 140, 48, 42, 34);
    frame_rect(1070 + center_shift - wall_skew, 220 + horizon, 210 + wall_skew, 140, 48, 42, 34);

    frame_rect(480 + input.lane + center_shift, 210 + horizon, 300, 220, 94, 86, 70);
    frame_rect(520 + input.lane + center_shift, 230 + horizon, 220, 162, 154, 138, 106);

    frame_rect(820 + input.lane + center_shift + (wall_skew / 2), 250 + horizon, 92, 160, 122, 108, 86);
    marker_x = 630 + input.lane + center_shift;
    marker_y = 311 + horizon;
  }

  frame_rect(marker_x - 6, marker_y - 6, 12, 12, 120, 232, 255);
  frame_rect(marker_x - 2, marker_y - 2, 4, 4, 240, 252, 255);

  frame_commit();
}
