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
  int yaw_signed;
  int pitch_signed;
  int near_size;
  int mid_size;
  int far_size;
  int near_x;
  int mid_x;
  int far_x;
  int near_y;
  int mid_y;
  int far_y;
  int near_depth;
  int mid_depth;
  int far_depth;
  int near_visible;
  int mid_visible;
  int far_visible;
  int focal;
  int block_world;
  int cam_height;
  int pitch_push;

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
  yaw_signed = input.yaw;
  pitch_signed = input.pitch;
  if (yaw_signed > 180) {
    yaw_signed = yaw_signed - 360;
  }
  if (pitch_signed > 89) {
    pitch_signed = 89;
  }
  if (pitch_signed < (0 - 89)) {
    pitch_signed = 0 - 89;
  }

  focal = 760;
  block_world = 110;
  cam_height = 150;

  near_depth = 700 + ((yaw_signed * 260) / 57);
  mid_depth = 1300;
  far_depth = 2200 - ((yaw_signed * 260) / 57);

  near_size = (block_world * focal) / near_depth;
  mid_size = (block_world * focal) / mid_depth;
  far_size = (block_world * focal) / far_depth;

  near_x = 640 + ((focal * (260 - ((yaw_signed * 700) / 57))) / near_depth) - (near_size / 2);
  mid_x = 640 + ((focal * (0 - ((yaw_signed * 1300) / 57))) / mid_depth) - (mid_size / 2);
  far_x = 640 + ((focal * ((0 - 260) - ((yaw_signed * 2200) / 57))) / far_depth) - (far_size / 2);

  pitch_push = (pitch_signed * 1800) / near_depth;
  near_y = horizon_y + ((cam_height * focal) / near_depth) - near_size - pitch_push;
  pitch_push = (pitch_signed * 1800) / mid_depth;
  mid_y = horizon_y + ((cam_height * focal) / mid_depth) - mid_size - pitch_push;
  pitch_push = (pitch_signed * 1800) / far_depth;
  far_y = horizon_y + ((cam_height * focal) / far_depth) - far_size - pitch_push;

  near_visible = 0;
  mid_visible = 0;
  far_visible = 0;
  if (near_depth > 120) {
    if (near_x < 1280) {
      if ((near_x + near_size) > 0) {
        if (near_y < 720) {
          if ((near_y + near_size) > 0) {
            near_visible = 1;
          }
        }
      }
    }
  }
  if (mid_depth > 120) {
    if (mid_x < 1280) {
      if ((mid_x + mid_size) > 0) {
        if (mid_y < 720) {
          if ((mid_y + mid_size) > 0) {
            mid_visible = 1;
          }
        }
      }
    }
  }
  if (far_depth > 120) {
    if (far_x < 1280) {
      if ((far_x + far_size) > 0) {
        if (far_y < 720) {
          if ((far_y + far_size) > 0) {
            far_visible = 1;
          }
        }
      }
    }
  }

  if (near_visible) {
    if (mid_visible) {
      if (near_x <= mid_x) {
        if (near_y <= mid_y) {
          if ((near_x + near_size) >= (mid_x + mid_size)) {
            if ((near_y + near_size) >= (mid_y + mid_size)) {
              mid_visible = 0;
            }
          }
        }
      }
    }
    if (far_visible) {
      if (near_x <= far_x) {
        if (near_y <= far_y) {
          if ((near_x + near_size) >= (far_x + far_size)) {
            if ((near_y + near_size) >= (far_y + far_size)) {
              far_visible = 0;
            }
          }
        }
      }
    }
  }

  if (mid_visible) {
    if (far_visible) {
      if (mid_x <= far_x) {
        if (mid_y <= far_y) {
          if ((mid_x + mid_size) >= (far_x + far_size)) {
            if ((mid_y + mid_size) >= (far_y + far_size)) {
              far_visible = 0;
            }
          }
        }
      }
    }
  }

  frame_begin(1280, 720, 18, 20, 24);

  frame_rect(0, 0, 1280, sky_height, 42, 44, 40);
  frame_rect(0, horizon_y, 1280, ground_height, 106, 88, 60);
  frame_rect(0, horizon_band_y, 1280, 8, 78, 72, 64);

  if (far_visible) {
    frame_rect(far_x, far_y, far_size, far_size, 120, 208, 232);
    frame_rect(far_x + 6, far_y + 6, far_size - 12, far_size - 12, 214, 246, 255);
  }

  if (mid_visible) {
    frame_rect(mid_x, mid_y, mid_size, mid_size, 242, 196, 78);
    frame_rect(mid_x + 8, mid_y + 8, mid_size - 16, mid_size - 16, 255, 242, 164);
  }

  if (near_visible) {
    frame_rect(near_x, near_y, near_size, near_size, 156, 224, 124);
    frame_rect(near_x + 10, near_y + 10, near_size - 20, near_size - 20, 236, 255, 208);
  }

  frame_commit();
}
