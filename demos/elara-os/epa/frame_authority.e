#include "frame_protocol.em"

type FrameRequest(int opcode, int surface_id, int arg0, int arg1) {
  return opcode;
}

type SurfaceRecord(
  int surface_id,
  int owner_uid_lo,
  int owner_uid_hi,
  int x,
  int y,
  int width,
  int height,
  int flags
) {
  return surface_id;
}

type FrameAuthorityState(
  int frame_id,
  int boot_complete,
  int focused_surface,
  int surface_count
) {
  return frame_id;
}

dynamic frame_surfaces(SurfaceRecord, 8, 64, 8);

kernel(VM vm) {
  kernalId("elara.os.frame_authority");
  start_worker(publish_boot_frame);
  start_worker(frame_ingress);
}

acl {
  "elara.os.boot" -> publish_boot_frame;
  "elara.os.shell" -> frame_ingress;
  "elara.os.input" -> frame_ingress;
  "elara.app.example" -> frame_ingress;
}

@attributes signal_mail_box_size:512
worker publish_boot_frame(FrameBoot boot) {
  static FrameAuthorityState state;

  static {
    state.frame_id = 1;
    state.boot_complete = 0;
    state.focused_surface = 0;
    state.surface_count = 0;
  }

  frame_begin(1280, 720, 2, state.frame_id, 15);

  // Background and top-band.
  frame_rect(0, 0, 1280, 720, 10, 14, 20);
  frame_rect(0, 0, 1280, 116, 23, 30, 40);

  // Main authority panel.
  frame_rect(104, 126, 1072, 468, 18, 23, 31);
  frame_rect(120, 142, 1040, 436, 26, 34, 46);
  frame_rect(120, 142, 1040, 12, 82, 146, 255);

  // Boot glyph: three stacked frame bars close to the chipset edge.
  frame_rect(168, 216, 92, 188, 82, 146, 255);
  frame_rect(284, 216, 92, 248, 117, 224, 163);
  frame_rect(400, 216, 92, 152, 255, 196, 92);

  // Status blocks for bootstrap authorities.
  frame_rect(560, 224, 312, 28, 82, 146, 255);
  frame_rect(560, 268, 244, 24, 117, 224, 163);
  frame_rect(560, 308, 212, 24, 255, 196, 92);
  frame_rect(560, 364, 468, 96, 31, 39, 52);
  frame_rect(560, 476, 340, 18, 61, 71, 86);

  // Focus frame and footer rail.
  frame_line(88, 610, 1192, 610, 70, 96, 130, 0);
  frame_rect(0, 664, 1280, 56, 14, 18, 24);
  frame_rect(28, 682, 220, 12, 82, 146, 255);
  frame_rect(1098, 682, 154, 12, 117, 224, 163);

  frame_commit();

  state.boot_complete = 1;
  state.frame_id = state.frame_id + 1;
  retire_worker();
}

worker frame_ingress(FrameRequest request) {
  static FrameAuthorityState state;

  static {
    state.frame_id = 2;
    state.boot_complete = 1;
    state.focused_surface = 0;
    state.surface_count = 0;
  }

  if (request.opcode == 1) {
    int slot = dyn_alloc(frame_surfaces);
    state.surface_count = state.surface_count + 1;
    state.focused_surface = request.surface_id;
    slot = slot;
  }

  if (request.opcode == 2) {
    state.focused_surface = request.surface_id;
  }

  if (request.opcode == 3) {
    frame_begin(1280, 720, 2, state.frame_id, 7);
    frame_rect(0, 0, 1280, 720, 10, 14, 20);
    frame_rect(0, 0, 1280, 88, 23, 30, 40);
    frame_rect(72, 116, 1136, 540, 18, 23, 31);
    frame_rect(88, 132, 1104, 508, 26, 34, 46);
    frame_rect(88, 132, 1104, 10, 82, 146, 255);
    frame_rect(116, 164, 200 + (state.surface_count * 48), 18, 117, 224, 163);
    frame_rect(116, 208, 160 + (state.focused_surface * 24), 14, 255, 196, 92);
    frame_commit();
    state.frame_id = state.frame_id + 1;
  }
}
