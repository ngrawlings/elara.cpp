declare default_in_words 256
declare default_out_words 256
declare default_signal_mail_box_size 128

// Shared ingress/message types for the first Orange Fortress kernel mesh.
// Keep these intentionally small until the runtime paths are exercised.

type FrameTick(int frame_id, int phase, int mode) {
  return frame_id;
}

type KeyInput(int key_code, int pressed, int modifiers) {
  return key_code;
}

// Combined per-frame camera input.
// move_x: -1 = strafe left, 0 = none, +1 = strafe right
// move_z: -1 = move back, 0 = none, +1 = move forward
// look_dx: accumulated mouse delta x (right = positive)
// look_dy: accumulated mouse delta y (down = positive)
type CameraInput(int move_x, int move_z, int look_dx, int look_dy) {
  return move_x;
}

type ScenePoseInput(
  int cam_x,
  int cam_z,
  int depth,
  int lane,
  int yaw,
  int pitch,
  int end_wall_x,
  int end_wall_y,
  int end_wall_w,
  int end_wall_h,
  int end_wall_visible,
  int side_wall_x,
  int side_wall_y,
  int side_wall_w,
  int side_wall_h,
  int side_wall_visible,
  int marker0_x,
  int marker0_y,
  int marker0_visible,
  int marker1_x,
  int marker1_y,
  int marker1_visible,
  int marker2_x,
  int marker2_y,
  int marker2_visible
) {
  return cam_x;
}

type PlayerIntent(int move_x, int move_y, int fire_mode, int look_dx) {
  return move_x;
}

type WeaponCommand(int mode, int trigger, int ammo_hint) {
  return mode;
}

type ActorState(int actor_id, int posture, int flags) {
  return actor_id;
}

type WorldState(int zone_id, int dirty_flags, int threat_level) {
  return zone_id;
}

type SceneRequest(int camera_id, int scene_flags, int focus_actor_id) {
  return camera_id;
}

type RenderProduct(int product_id, int dirty_flags, int layer) {
  return product_id;
}

type HudCommand(int hud_mode, int dirty_flags, int selected_weapon) {
  return hud_mode;
}

// Solid wall primitive.
// size_*: dimensions of the wall volume
// pos_*: world-space origin/placement
// pitch/yaw: orientation in engine ticks
// color_*: flat solid-color surface for the first wall pass
type WallSurface(
  int size_x,
  int size_y,
  int size_z,
  int pos_x,
  int pos_y,
  int pos_z,
  int pitch,
  int yaw,
  int color_r,
  int color_g,
  int color_b
) {
  return size_x;
}
