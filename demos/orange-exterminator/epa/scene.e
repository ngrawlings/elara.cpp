#include "engine_common.em"

// Scene kernel.
// World block positions (fixed for parallax calibration):
//   Near : world ( 260,  700)
//   Mid  : world (   0, 1300)
//   Far  : world (-260, 2200)
// Camera pose (cam_x, cam_z, yaw, pitch) drives all projection.

kernel(VM vm) {
  request_threads(2);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
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
  int rel_x_near;
  int rel_z_near;
  int rel_x_mid;
  int rel_z_mid;
  int rel_x_far;
  int rel_z_far;
  int view_x_near;
  int view_x_mid;
  int view_x_far;

  focal      = 760;
  block_world = 110;
  cam_height  = 150;

  // Pitch/yaw clamped and sign-normalised.
  yaw_signed = input.yaw;
  if (yaw_signed > 180) {
    yaw_signed = yaw_signed - 360;
  }
  pitch_signed = input.pitch;
  if (pitch_signed > 89) {
    pitch_signed = 89;
  }
  if (pitch_signed < (0 - 89)) {
    pitch_signed = 0 - 89;
  }

  // Horizon rises when looking up (positive pitch).
  horizon_y = 360 + ((pitch_signed * focal) / 57);
  if (horizon_y < 60) {
    horizon_y = 60;
  }
  if (horizon_y > 660) {
    horizon_y = 660;
  }
  horizon_band_y = horizon_y - 8;
  sky_height    = horizon_y;
  ground_height = 720 - horizon_y;

  // World positions relative to camera (X and Z only; Y handled via cam_height).
  rel_x_near = 260 - input.cam_x;
  rel_z_near = 700 - input.cam_z;
  rel_x_mid  = 0 - input.cam_x;
  rel_z_mid  = 1300 - input.cam_z;
  rel_x_far  = (0 - 260) - input.cam_x;
  rel_z_far  = 2200 - input.cam_z;

  // Yaw rotation (integer small-angle: sin(d) ≈ d/57, cos(d) ≈ 1).
  // view_x = rel_x - yaw_rad * rel_z
  // view_z = rel_z + yaw_rad * rel_x  (= actual depth)
  view_x_near = rel_x_near - ((yaw_signed * rel_z_near) / 57);
  near_depth  = rel_z_near + ((yaw_signed * rel_x_near) / 57);

  view_x_mid = rel_x_mid - ((yaw_signed * rel_z_mid) / 57);
  mid_depth  = rel_z_mid + ((yaw_signed * rel_x_mid) / 57);

  view_x_far = rel_x_far - ((yaw_signed * rel_z_far) / 57);
  far_depth  = rel_z_far + ((yaw_signed * rel_x_far) / 57);

  // Size and screen position (only when depth > 0).
  near_size = 0;
  near_x    = 0;
  near_y    = 0;
  if (near_depth > 0) {
    near_size   = (block_world * focal) / near_depth;
    near_x      = 640 + ((focal * view_x_near) / near_depth) - (near_size / 2);
    pitch_push  = (pitch_signed * focal) / 57;
    near_y      = horizon_y + ((cam_height * focal) / near_depth) - near_size - pitch_push;
  }

  mid_size = 0;
  mid_x    = 0;
  mid_y    = 0;
  if (mid_depth > 0) {
    mid_size   = (block_world * focal) / mid_depth;
    mid_x      = 640 + ((focal * view_x_mid) / mid_depth) - (mid_size / 2);
    pitch_push = (pitch_signed * focal) / 57;
    mid_y      = horizon_y + ((cam_height * focal) / mid_depth) - mid_size - pitch_push;
  }

  far_size = 0;
  far_x    = 0;
  far_y    = 0;
  if (far_depth > 0) {
    far_size   = (block_world * focal) / far_depth;
    far_x      = 640 + ((focal * view_x_far) / far_depth) - (far_size / 2);
    pitch_push = (pitch_signed * focal) / 57;
    far_y      = horizon_y + ((cam_height * focal) / far_depth) - far_size - pitch_push;
  }

  // Visibility: block must be in front, have positive size, and overlap screen.
  near_visible = 0;
  mid_visible  = 0;
  far_visible  = 0;

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

  // Occlusion: hide a block fully covered by a closer one.
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
