#include "../frame_io_protocol.em"
#include "../../../dynamic_acl_protocol.em"

type ConsoleViewRequest(int opcode, int sequence, int cursor_x, int cursor_y) {
  return opcode;
}

kernel(VM vm) {
  kernalId("elara.os.console_view");
  start_worker(console_ingress);
}

acl {
  "elara.os.boot" -> console_ingress;
  "elara.os.filesystem" -> console_ingress;
  "elara.os.hid_io" -> console_ingress;
  "elara.os.shell" -> console_ingress;
}

worker console_ingress(ConsoleViewRequest request) {
  static int registered;
  local DynamicACLRequest acl_request;
  local FrameManagerFrame focus;
  local FrameManagerFrame frame;

  if (registered == 0) {
    acl_request.opcode = dynamic_acl_opcode_register();
    acl_request.route_id = dynamic_acl_authority_console();
    acl_request.flags = dynamic_acl_authority_frame_io();
    acl_request.reserved = 0;
    far_signal("elara.os.entry", dynamic_acl_authority, acl_request);
    registered = 1;
  }

  focus.manager_id = 1;
  focus.frame_id = request.sequence;
  focus.frame_kind = 0;
  focus.flags = 0;
  focus.arg0 = request.opcode;
  focus.arg1 = 0;
  focus.arg2 = 0;
  focus.arg3 = 0;
  far_signal("elara.os.frame_io", manager_ingress, focus);

  frame.manager_id = 1;
  frame.frame_id = request.sequence;
  frame.frame_kind = 1;
  frame.flags = 0;
  frame.arg0 = request.cursor_x;
  frame.arg1 = request.cursor_y;
  frame.arg2 = 0;
  frame.arg3 = 0;
  far_signal("elara.os.frame_io", manager_ingress, frame);

  retire_worker();
}
