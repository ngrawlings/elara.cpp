// Platform types shared between the EPA engine kernels and game code.
// Game developers include this to build far_signal payloads for platform kernels.
// Game developers never interact with the binary wire format — only these types.

// ---------------------------------------------------------------------------
// Scene compiler types (kernel: "elara.platform.scene_compiler")
// ---------------------------------------------------------------------------

// Camera pose for a 3D scene frame.
// Positions are in milliunits (1000 = 1 world unit).
// Angles are in millidegrees (1000 = 1 degree).
//   yaw  : rotation around Y axis; 0 = +Z forward, positive = clockwise
//   pitch: rotation around X axis; positive = look down
//   fov  : vertical field of view (e.g. 60000 = 60 degrees)
//   near_z / far_z : clip plane distances in milliunits
type VkCamera(
  int pos_x,
  int pos_y,
  int pos_z,
  int yaw,
  int pitch,
  int fov,
  int near_z,
  int far_z
) {
  return pos_x;
}

// A single renderable object submitted to the scene.
// scale is uniform in milliunits (1000 = 1.0).
type VkObject(
  int object_id,
  int mesh_id,
  int material_id,
  int pos_x,
  int pos_y,
  int pos_z,
  int yaw,
  int scale
) {
  return object_id;
}

// Full scene frame payload for scene_compile worker.
// Camera fields are required. Objects are optional up to obj_count (max 4 in v1).
type VkSceneCompile(
  int frame_id,
  int width,
  int height,
  int flags,
  int cam_pos_x,
  int cam_pos_y,
  int cam_pos_z,
  int cam_yaw,
  int cam_pitch,
  int cam_fov,
  int cam_near,
  int cam_far,
  int obj_count,
  int obj0_mesh, int obj0_mat, int obj0_x, int obj0_y, int obj0_z, int obj0_yaw, int obj0_scale,
  int obj1_mesh, int obj1_mat, int obj1_x, int obj1_y, int obj1_z, int obj1_yaw, int obj1_scale,
  int obj2_mesh, int obj2_mat, int obj2_x, int obj2_y, int obj2_z, int obj2_yaw, int obj2_scale,
  int obj3_mesh, int obj3_mat, int obj3_x, int obj3_y, int obj3_z, int obj3_yaw, int obj3_scale
) {
  return frame_id;
}
