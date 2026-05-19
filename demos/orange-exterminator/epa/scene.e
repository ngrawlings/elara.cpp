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
  int horizon_y;
  int horizon_band_y;
  int sky_height;
  int ground_height;
  int ladder_x;
  int ladder_top_y;
  int ladder_mid_y;
  int ladder_bot_y;

  horizon_y = 340 + (input.pitch * 4);
  if (horizon_y < 120) {
    horizon_y = 120;
  }
  if (horizon_y > 620) {
    horizon_y = 620;
  }

  horizon_band_y = horizon_y - 8;
  sky_height = horizon_y;
  ground_height = 720 - horizon_y;

  frame_begin(1280, 720, 18, 20, 24);

  frame_rect(0, 0, 1280, sky_height, 42, 44, 40);
  frame_rect(0, horizon_y, 1280, ground_height, 106, 88, 60);
  frame_rect(0, horizon_band_y, 1280, 8, 78, 72, 64);

  frame_rect(638, 328, 4, 64, 210, 212, 216);
  frame_rect(608, 358, 64, 4, 210, 212, 216);

  ladder_x = 640;
  ladder_top_y = horizon_y - 140;
  ladder_mid_y = horizon_y;
  ladder_bot_y = horizon_y + 140;

  frame_rect(ladder_x - 8, ladder_top_y - 8, 16, 16, 112, 224, 250);
  frame_rect(ladder_x - 3, ladder_top_y - 3, 6, 6, 240, 252, 255);

  frame_rect(ladder_x - 8, ladder_mid_y - 8, 16, 16, 242, 196, 78);
  frame_rect(ladder_x - 3, ladder_mid_y - 3, 6, 6, 255, 244, 166);

  frame_rect(ladder_x - 8, ladder_bot_y - 8, 16, 16, 164, 234, 126);
  frame_rect(ladder_x - 3, ladder_bot_y - 3, 6, 6, 244, 255, 220);

  frame_commit();
}
