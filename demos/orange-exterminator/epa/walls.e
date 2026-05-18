#include "engine_common.em"

// Wall geometry kernel.
// First-pass role:
// - own flat-color wall primitives
// - accept wall definitions as typed ingress
// - later emit wall slices / visibility products toward scene or render kernels

kernel(VM vm) {
  request_threads(2);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    if (wid == 1) {
      WallSurface wall = kernal_get_ghs(1);
      // TODO: store/replace a wall primitive table for the active scene.
      log("walls primitive wid={d}", wid);
    } else if (wid == 2) {
      FrameTick tick = kernal_get_ghs(2);
      // TODO: evaluate visibility / slicing of registered walls for this frame.
      log("walls tick wid={d}", wid);
    } else {
      log("walls unknown wid={d}", wid);
    }
  }
}

worker walls_surface_ingress(WallSurface wall) {
  // TODO: ingest a wall primitive:
  // size_x/size_y/size_z = dimensions
  // pos_x/pos_y/pos_z    = world placement
  // pitch/yaw            = orientation
  // color_r/g/b          = flat wall color
  kernel_signal();
}

worker walls_tick_ingress(FrameTick tick) {
  // TODO: drive wall visibility / render prep per frame.
  kernel_signal();
}
