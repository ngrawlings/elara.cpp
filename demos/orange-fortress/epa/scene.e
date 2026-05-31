#include "engine_common.em"
#include "platform_common.em"

// Scene constructor kernel.
// The C++ host pushes camera pose into this worker. This kernel owns the game
// scene shape, then delegates E3SB encoding to the local Vulkan scene compiler.

kernel(VM vm) {
  kernalId("orange.fortress.scene");
  request_threads(2);
  start_worker(scene_camera_update);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
  }
}

@attributes signal_mail_box_size:4096
worker scene_camera_update(ScenePoseInput input) {
  local VkSceneCompileV2 scene;
  int yaw_milli;
  int pitch_milli;
  int cam_z;

  yaw_milli = input.yaw * 1000;
  pitch_milli = input.pitch * 1000;
  cam_z = input.cam_z - 900;

  scene.frame_id = input.depth;
  scene.width = 1280;
  scene.height = 720;
  scene.flags = 0;
  scene.command_count = 1;

  // Camera records. Positions are milliunits; angles are millidegrees.
  scene.p0_op = 10;
  scene.p0_a0 = input.cam_x;
  scene.p0_a1 = 620;
  scene.p0_a2 = cam_z;
  scene.p0_a3 = yaw_milli;
  scene.p0_a4 = pitch_milli;
  scene.p0_a5 = 0;
  scene.p0_a6 = 60000;

  scene.p1_op = 11;
  scene.p1_a0 = 80;
  scene.p1_a1 = 12000;
  scene.p1_a2 = 1000;
  scene.p1_a3 = 0;
  scene.p1_a4 = 0;
  scene.p1_a5 = 0;
  scene.p1_a6 = 0;

  // Environment and two simple materials.
  scene.p2_op = 20;
  scene.p2_a0 = 18;
  scene.p2_a1 = 20;
  scene.p2_a2 = 24;
  scene.p2_a3 = 54;
  scene.p2_a4 = 48;
  scene.p2_a5 = 42;
  scene.p2_a6 = 0;

  scene.p3_op = 30;
  scene.p3_a0 = 1;
  scene.p3_a1 = 255;
  scene.p3_a2 = 132;
  scene.p3_a3 = 22;
  scene.p3_a4 = 0;
  scene.p3_a5 = 760;
  scene.p3_a6 = 0;

  scene.p4_op = 30;
  scene.p4_a0 = 2;
  scene.p4_a1 = 92;
  scene.p4_a2 = 150;
  scene.p4_a3 = 88;
  scene.p4_a4 = 0;
  scene.p4_a5 = 880;
  scene.p4_a6 = 0;

  // Built-in mesh ids: 1 = cube, 2 = plane/grid marker in the Vulkan widget.
  scene.p5_op = 40;
  scene.p5_a0 = 1;
  scene.p5_a1 = 0;
  scene.p5_a2 = 0;
  scene.p5_a3 = 36;
  scene.p5_a4 = 0;
  scene.p5_a5 = 1000;
  scene.p5_a6 = 0;

  scene.p6_op = 40;
  scene.p6_a0 = 2;
  scene.p6_a1 = 0;
  scene.p6_a2 = 0;
  scene.p6_a3 = 6;
  scene.p6_a4 = 0;
  scene.p6_a5 = 1000;
  scene.p6_a6 = 0;

  // Three scene instances in depth: near/mid/floor landmark.
  scene.p7_op = 50;
  scene.p7_a0 = 1;
  scene.p7_a1 = 1;
  scene.p7_a2 = 1;
  scene.p7_a3 = 260;
  scene.p7_a4 = 700;
  scene.p7_a5 = 900;
  scene.p7_a6 = 0;

  scene.p8_op = 51;
  scene.p8_a0 = 1;
  scene.p8_a1 = 0;
  scene.p8_a2 = 0;
  scene.p8_a3 = 0;
  scene.p8_a4 = 260;
  scene.p8_a5 = 260;
  scene.p8_a6 = 260;

  scene.p9_op = 50;
  scene.p9_a0 = 2;
  scene.p9_a1 = 1;
  scene.p9_a2 = 1;
  scene.p9_a3 = 0;
  scene.p9_a4 = 520;
  scene.p9_a5 = 1700;
  scene.p9_a6 = 0;

  scene.p10_op = 51;
  scene.p10_a0 = 2;
  scene.p10_a1 = 22000;
  scene.p10_a2 = 0;
  scene.p10_a3 = 0;
  scene.p10_a4 = 420;
  scene.p10_a5 = 420;
  scene.p10_a6 = 420;

  scene.p11_op = 50;
  scene.p11_a0 = 3;
  scene.p11_a1 = 1;
  scene.p11_a2 = 2;
  scene.p11_a3 = 0 - 420;
  scene.p11_a4 = 0;
  scene.p11_a5 = 2600;
  scene.p11_a6 = 0;

  scene.p12_op = 51;
  scene.p12_a0 = 3;
  scene.p12_a1 = 0;
  scene.p12_a2 = 0;
  scene.p12_a3 = 0;
  scene.p12_a4 = 2200;
  scene.p12_a5 = 60;
  scene.p12_a6 = 2200;

  frame_begin(scene.width, scene.height, 3, scene.frame_id, scene.command_count);
  frame_line(scene.p0_op, scene.p0_a0, scene.p0_a1, scene.p0_a2, scene.p0_a3, scene.p0_a4, scene.p0_a5, scene.p0_a6);
  frame_commit();
}
