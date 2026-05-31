#include "platform_common.em"

// Orange Fortress local copy of the platform scene compiler kernel.
// Accepts a full scene snapshot (camera + object list) and emits the
// Elara 3D Scene Binary (E3SB) word stream into the host frame mailbox.
// The Vulkan widget on the host reads this stream and renders the frame.
//
// Kernel ID: orange.fortress.scene_compiler
// ACL: open — any game kernel may far_signal into scene_compile.
//
// ---------------------------------------------------------------------------
// E3SB wire format (word stream in the host frame mailbox)
// ---------------------------------------------------------------------------
//
//   Frame header (from frame_begin - 7 words):
//     [0x45465231]           EFR1 magic
//     [1]                    frame-begin marker / mailbox frame version
//     [width]  [height]      output resolution in pixels
//     [3]                    E3SB frame type (3 = Vulkan 3D scene)
//     [frame_id]             monotonically increasing frame counter
//     [record_count]          number of scene records that follow
//
//   V1 compatibility records:
//
//   Camera block (two frame_rect calls - 16 words):
//     [1] cam_pos_x  cam_pos_y  cam_pos_z  cam_yaw  cam_pitch  cam_fov  cam_near
//     [1] cam_far  0  0  0  0  0  0
//     All values in milliunits or millidegrees as documented in VkCamera.
//
//   Object entries (one frame_rect per object - 8 words each):
//     [1] mesh_id  material_id  pos_x  pos_y  pos_z  yaw  scale
//
//   V2 command records:
//
//     [2] scene_op  a0  a1  a2  a3  a4  a5  a6
//
//   scene_op values are documented in platform_common.em. The V2 path is
//   designed to be close to Vulkan staging data: the host can validate the
//   frame and copy records into camera, environment, material, mesh, instance,
//   light, terrain, sky, decal, billboard, particle, volume, and post-process
//   buffers with minimal reshaping.
//
//   End marker (from frame_commit - 1 word + HOST_SIGNAL):
//     [255]
//
// The host frame mailbox parser identifies E3SB frames by the [3] type word
// and routes them to the Vulkan widget 3D render path instead of the 2D raster
// path (ops RECT/LINE/TEXT).

kernel(VM vm) {
  kernalId("orange.fortress.scene_compiler");
  start_worker(scene_compile);
  start_worker(scene_compile_full);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 1) {
      // scene_compile emitted the compatibility E3SB frame.
    }
    if (wid == 2) {
      // scene_compile_full emitted the V2 primitive command stream.
    }
  }
}

// Open ACL: any kernel in the EPA mesh may dispatch into scene_compile.
// The host enforces resource limits; no kernel-level whitelist needed here.

acl {
  "*" -> scene_compile;
  "*" -> scene_compile_full;
}

@attributes signal_mail_box_size:2048
worker scene_compile(VkSceneCompile scene) {
  // Frame header: E3SB type = 3, carries frame_id and object count.
  frame_begin(scene.width, scene.height, 3, scene.frame_id, scene.obj_count);

  // Camera block part 1: position + orientation + fov + near plane.
  frame_rect(
    scene.cam_pos_x,
    scene.cam_pos_y,
    scene.cam_pos_z,
    scene.cam_yaw,
    scene.cam_pitch,
    scene.cam_fov,
    scene.cam_near
  );

  // Camera block part 2: far plane (remaining words reserved for future use).
  frame_rect(scene.cam_far, 0, 0, 0, 0, 0, 0);

  // Object draw entries — one frame_rect per object.
  if (scene.obj_count > 0) {
    frame_rect(
      scene.obj0_mesh, scene.obj0_mat,
      scene.obj0_x, scene.obj0_y, scene.obj0_z,
      scene.obj0_yaw, scene.obj0_scale
    );
  }
  if (scene.obj_count > 1) {
    frame_rect(
      scene.obj1_mesh, scene.obj1_mat,
      scene.obj1_x, scene.obj1_y, scene.obj1_z,
      scene.obj1_yaw, scene.obj1_scale
    );
  }
  if (scene.obj_count > 2) {
    frame_rect(
      scene.obj2_mesh, scene.obj2_mat,
      scene.obj2_x, scene.obj2_y, scene.obj2_z,
      scene.obj2_yaw, scene.obj2_scale
    );
  }
  if (scene.obj_count > 3) {
    frame_rect(
      scene.obj3_mesh, scene.obj3_mat,
      scene.obj3_x, scene.obj3_y, scene.obj3_z,
      scene.obj3_yaw, scene.obj3_scale
    );
  }

  frame_commit();
  kernel_signal();
}

@attributes signal_mail_box_size:4096
worker scene_compile_full(VkSceneCompileV2 scene) {
  // E3SB V2: command_count counts the primitive records below.
  frame_begin(scene.width, scene.height, 3, scene.frame_id, scene.command_count);

  if (scene.command_count > 0) {
    frame_line(scene.p0_op, scene.p0_a0, scene.p0_a1, scene.p0_a2, scene.p0_a3, scene.p0_a4, scene.p0_a5, scene.p0_a6);
  }
  if (scene.command_count > 1) {
    frame_line(scene.p1_op, scene.p1_a0, scene.p1_a1, scene.p1_a2, scene.p1_a3, scene.p1_a4, scene.p1_a5, scene.p1_a6);
  }
  if (scene.command_count > 2) {
    frame_line(scene.p2_op, scene.p2_a0, scene.p2_a1, scene.p2_a2, scene.p2_a3, scene.p2_a4, scene.p2_a5, scene.p2_a6);
  }
  if (scene.command_count > 3) {
    frame_line(scene.p3_op, scene.p3_a0, scene.p3_a1, scene.p3_a2, scene.p3_a3, scene.p3_a4, scene.p3_a5, scene.p3_a6);
  }
  if (scene.command_count > 4) {
    frame_line(scene.p4_op, scene.p4_a0, scene.p4_a1, scene.p4_a2, scene.p4_a3, scene.p4_a4, scene.p4_a5, scene.p4_a6);
  }
  if (scene.command_count > 5) {
    frame_line(scene.p5_op, scene.p5_a0, scene.p5_a1, scene.p5_a2, scene.p5_a3, scene.p5_a4, scene.p5_a5, scene.p5_a6);
  }
  if (scene.command_count > 6) {
    frame_line(scene.p6_op, scene.p6_a0, scene.p6_a1, scene.p6_a2, scene.p6_a3, scene.p6_a4, scene.p6_a5, scene.p6_a6);
  }
  if (scene.command_count > 7) {
    frame_line(scene.p7_op, scene.p7_a0, scene.p7_a1, scene.p7_a2, scene.p7_a3, scene.p7_a4, scene.p7_a5, scene.p7_a6);
  }
  if (scene.command_count > 8) {
    frame_line(scene.p8_op, scene.p8_a0, scene.p8_a1, scene.p8_a2, scene.p8_a3, scene.p8_a4, scene.p8_a5, scene.p8_a6);
  }
  if (scene.command_count > 9) {
    frame_line(scene.p9_op, scene.p9_a0, scene.p9_a1, scene.p9_a2, scene.p9_a3, scene.p9_a4, scene.p9_a5, scene.p9_a6);
  }
  if (scene.command_count > 10) {
    frame_line(scene.p10_op, scene.p10_a0, scene.p10_a1, scene.p10_a2, scene.p10_a3, scene.p10_a4, scene.p10_a5, scene.p10_a6);
  }
  if (scene.command_count > 11) {
    frame_line(scene.p11_op, scene.p11_a0, scene.p11_a1, scene.p11_a2, scene.p11_a3, scene.p11_a4, scene.p11_a5, scene.p11_a6);
  }
  if (scene.command_count > 12) {
    frame_line(scene.p12_op, scene.p12_a0, scene.p12_a1, scene.p12_a2, scene.p12_a3, scene.p12_a4, scene.p12_a5, scene.p12_a6);
  }
  if (scene.command_count > 13) {
    frame_line(scene.p13_op, scene.p13_a0, scene.p13_a1, scene.p13_a2, scene.p13_a3, scene.p13_a4, scene.p13_a5, scene.p13_a6);
  }
  if (scene.command_count > 14) {
    frame_line(scene.p14_op, scene.p14_a0, scene.p14_a1, scene.p14_a2, scene.p14_a3, scene.p14_a4, scene.p14_a5, scene.p14_a6);
  }
  if (scene.command_count > 15) {
    frame_line(scene.p15_op, scene.p15_a0, scene.p15_a1, scene.p15_a2, scene.p15_a3, scene.p15_a4, scene.p15_a5, scene.p15_a6);
  }

  frame_commit();
  kernel_signal();
}
