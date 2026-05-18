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
  frame_begin(1280, 720, 12, 14, 18);

  if (input.depth == 1) {
    frame_rect(0, 0, 1280, 190, 34, 36, 32);
    frame_rect(0, 190, 1280, 170, 78, 70, 58);
    frame_rect(0, 360, 1280, 360, 112, 92, 62);

    frame_rect(0, 190, 160, 170, 48, 42, 34);
    frame_rect(1060, 190, 220, 170, 48, 42, 34);

    frame_rect(430 + input.lane, 150, 420, 320, 94, 86, 70);
    frame_rect(475 + input.lane, 185, 330, 235, 154, 138, 106);

    frame_rect(915 + input.lane, 220, 110, 210, 122, 108, 86);
  } else {
    frame_rect(0, 0, 1280, 220, 34, 36, 32);
    frame_rect(0, 220, 1280, 140, 78, 70, 58);
    frame_rect(0, 360, 1280, 360, 112, 92, 62);

    frame_rect(0, 220, 120, 140, 48, 42, 34);
    frame_rect(1100, 220, 180, 140, 48, 42, 34);

    frame_rect(520 + input.lane, 210, 240, 220, 94, 86, 70);
    frame_rect(548 + input.lane, 230, 184, 162, 154, 138, 106);

    frame_rect(840 + input.lane, 250, 72, 160, 122, 108, 86);
  }

  frame_commit();
}
