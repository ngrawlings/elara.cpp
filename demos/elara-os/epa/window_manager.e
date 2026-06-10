#include "frame_protocol.em"
#include "dynamic_acl_protocol.em"

type WindowManagerRequest(int opcode, int sequence, int active_surface, int surface_count) {
  return opcode;
}

kernel(VM vm) {
  kernalId("elara.os.window_manager");
  start_worker(window_manager_ingress);
}

acl {
  "elara.os.boot" -> window_manager_ingress;
  "elara.os.shell" -> window_manager_ingress;
  "elara.os.input" -> window_manager_ingress;
  "elara.app.example" -> window_manager_ingress;
}

worker window_manager_ingress(WindowManagerRequest request) {
  static int registered;
  local DynamicACLRequest acl_request;
  local FrameManagerFrame focus;
  local FrameManagerFrame frame;

  if (registered == 0) {
    acl_request.opcode = dynamic_acl_opcode_register();
    acl_request.route_id = dynamic_acl_authority_window();
    acl_request.flags = dynamic_acl_authority_frame();
    acl_request.reserved = 0;
    far_signal("elara.os.entry", dynamic_acl_authority, acl_request);
    registered = 1;
  }

  focus.manager_id = 2;
  focus.frame_id = request.sequence;
  focus.frame_kind = 0;
  focus.flags = 0;
  focus.arg0 = request.opcode;
  focus.arg1 = 0;
  focus.arg2 = 0;
  focus.arg3 = 0;
  far_signal("elara.os.frame_authority", manager_ingress, focus);

  frame.manager_id = 2;
  frame.frame_id = request.sequence;
  frame.frame_kind = 2;
  frame.flags = 0;
  frame.arg0 = request.active_surface;
  frame.arg1 = request.surface_count;
  frame.arg2 = 0;
  frame.arg3 = 0;
  far_signal("elara.os.frame_authority", manager_ingress, frame);

  retire_worker();
}
