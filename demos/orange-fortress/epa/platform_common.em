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

// E3SB v2 scene primitive opcodes. These values are written as the first word
// in each primitive command slot. The host/Vulkan surface can route them
// directly into renderer staging buffers.
type VkScenePrimitiveOps(
  int camera_view,
  int camera_clip,
  int environment,
  int fog,
  int material_pbr,
  int material_ext,
  int mesh,
  int instance,
  int instance_xform,
  int instance_color,
  int directional_light,
  int point_light,
  int spot_light,
  int light_ext,
  int terrain,
  int skybox,
  int decal,
  int billboard,
  int particles,
  int volume,
  int post_process
) {
  return camera_view;
}

// Full scene command stream for scene_compile_full.
//
// Each pN slot is one compact GPU-oriented primitive:
//   pN_op, pN_a0, pN_a1, pN_a2, pN_a3, pN_a4, pN_a5, pN_a6
//
// Common opcodes:
//   10 CAMERA_VIEW        pos_x pos_y pos_z yaw pitch roll fov
//   11 CAMERA_CLIP        near_z far_z exposure view_id flags reserved reserved
//   20 ENVIRONMENT        clear_r clear_g clear_b ambient_r ambient_g ambient_b flags
//   21 FOG                color_r color_g color_b density start_z end_z mode
//   30 MATERIAL_PBR       material_id base_r base_g base_b metallic roughness emissive
//   31 MATERIAL_EXT       material_id alpha albedo_tex normal_tex mr_tex emissive_tex flags
//   40 MESH               mesh_id vertex_buffer index_buffer index_count vertex_stride bounds flags
//   50 INSTANCE           instance_id mesh_id material_id pos_x pos_y pos_z flags
//   51 INSTANCE_XFORM     instance_id yaw pitch roll scale_x scale_y scale_z
//   52 INSTANCE_COLOR     instance_id color_r color_g color_b alpha emissive flags
//   60 DIRECTIONAL_LIGHT  light_id dir_x dir_y dir_z color_r color_g color_b
//   61 POINT_LIGHT        light_id pos_x pos_y pos_z radius color_r color_g
//   62 SPOT_LIGHT         light_id pos_x pos_y pos_z dir_yaw dir_pitch cone_angle
//   63 LIGHT_EXT          light_id color_b intensity shadow_mode shadow_bias flags reserved
//   70 TERRAIN            terrain_id heightfield material_id pos_x pos_z scale_x scale_z
//   80 SKYBOX             cubemap_id tint_r tint_g tint_b intensity rotation flags
//   90 DECAL              decal_id material_id pos_x pos_y pos_z yaw scale
//   100 BILLBOARD         billboard_id material_id pos_x pos_y pos_z size flags
//   110 PARTICLES         emitter_id material_id pos_x pos_y pos_z count flags
//   120 VOLUME            volume_id volume_type pos_x pos_y pos_z radius density
//   130 POST_PROCESS      tonemap exposure gamma bloom vignette flags reserved
//
// Values remain integer packed: milliunits for distances/scales, millidegrees
// for angles, and renderer-defined fixed-point values for colors/material terms.
type VkSceneCompileV2(
  int frame_id,
  int width,
  int height,
  int flags,
  int command_count,

  int p0_op, int p0_a0, int p0_a1, int p0_a2, int p0_a3, int p0_a4, int p0_a5, int p0_a6,
  int p1_op, int p1_a0, int p1_a1, int p1_a2, int p1_a3, int p1_a4, int p1_a5, int p1_a6,
  int p2_op, int p2_a0, int p2_a1, int p2_a2, int p2_a3, int p2_a4, int p2_a5, int p2_a6,
  int p3_op, int p3_a0, int p3_a1, int p3_a2, int p3_a3, int p3_a4, int p3_a5, int p3_a6,
  int p4_op, int p4_a0, int p4_a1, int p4_a2, int p4_a3, int p4_a4, int p4_a5, int p4_a6,
  int p5_op, int p5_a0, int p5_a1, int p5_a2, int p5_a3, int p5_a4, int p5_a5, int p5_a6,
  int p6_op, int p6_a0, int p6_a1, int p6_a2, int p6_a3, int p6_a4, int p6_a5, int p6_a6,
  int p7_op, int p7_a0, int p7_a1, int p7_a2, int p7_a3, int p7_a4, int p7_a5, int p7_a6,
  int p8_op, int p8_a0, int p8_a1, int p8_a2, int p8_a3, int p8_a4, int p8_a5, int p8_a6,
  int p9_op, int p9_a0, int p9_a1, int p9_a2, int p9_a3, int p9_a4, int p9_a5, int p9_a6,
  int p10_op, int p10_a0, int p10_a1, int p10_a2, int p10_a3, int p10_a4, int p10_a5, int p10_a6,
  int p11_op, int p11_a0, int p11_a1, int p11_a2, int p11_a3, int p11_a4, int p11_a5, int p11_a6,
  int p12_op, int p12_a0, int p12_a1, int p12_a2, int p12_a3, int p12_a4, int p12_a5, int p12_a6,
  int p13_op, int p13_a0, int p13_a1, int p13_a2, int p13_a3, int p13_a4, int p13_a5, int p13_a6,
  int p14_op, int p14_a0, int p14_a1, int p14_a2, int p14_a3, int p14_a4, int p14_a5, int p14_a6,
  int p15_op, int p15_a0, int p15_a1, int p15_a2, int p15_a3, int p15_a4, int p15_a5, int p15_a6
) {
  return frame_id;
}
