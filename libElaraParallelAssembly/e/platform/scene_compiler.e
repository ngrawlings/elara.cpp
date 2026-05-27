#include "platform_common.em"

// Platform scene compiler kernel.
// Accepts a full scene snapshot (camera + object list) and emits the
// Elara 3D Scene Binary (E3SB) word stream into the host frame mailbox.
// The Vulkan widget on the host reads this stream and renders the frame.
//
// Kernel ID: elara.platform.scene_compiler
// ACL: open — any game kernel may far_signal into scene_compile.
//
// ---------------------------------------------------------------------------
// E3SB wire format (word stream in the host frame mailbox)
// ---------------------------------------------------------------------------
//
//   Frame header (from frame_begin — 7 words):
//     [0x45465231]           EFR1 magic
//     [1]                    frame-begin marker
//     [width]  [height]      output resolution in pixels
//     [3]                    E3SB frame type (3 = Vulkan 3D scene)
//     [frame_id]             monotonically increasing frame counter
//     [obj_count]            number of object entries that follow
//
//   Camera block (two frame_rect calls — 16 words):
//     [1] cam_pos_x  cam_pos_y  cam_pos_z  cam_yaw  cam_pitch  cam_fov  cam_near
//     [1] cam_far  0  0  0  0  0  0
//     All values in milliunits or millidegrees as documented in VkCamera.
//
//   Object entries (one frame_rect per object — 8 words each):
//     [1] mesh_id  material_id  pos_x  pos_y  pos_z  yaw  scale
//
//   End marker (from frame_commit — 1 word + HOST_SIGNAL):
//     [255]
//
// The host frame mailbox parser identifies E3SB frames by the [3] type word
// and routes them to the Vulkan widget 3D render path (ops SCENE_CAMERA and
// SCENE_OBJECT) instead of the 2D raster path (ops RECT/LINE/TEXT).

kernel(VM vm) {
  kernalId("elara.platform.scene_compiler");
  scene_compile(vm);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 1) {
      // scene_compile emitted the E3SB frame — host will pick it up.
    }
  }
}

// Open ACL: any kernel in the EPA mesh may dispatch into scene_compile.
// The host enforces resource limits; no kernel-level whitelist needed here.

acl {
  "*" -> scene_compile;
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
  signal();
}
