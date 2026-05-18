declare default_in_words 256
declare default_out_words 256
declare default_signal_mail_box_size 128

// Shared ingress/message types for the first Orange Exterminator kernel mesh.
// Keep these intentionally small until the runtime paths are exercised.

type FrameTick(int frame_id, int phase, int mode) {
  return frame_id;
}

type KeyInput(int key_code, int pressed, int modifiers) {
  return key_code;
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
