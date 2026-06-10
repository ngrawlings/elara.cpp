#include "frame_protocol.em"

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
  "elara.os.input" -> console_ingress;
  "elara.os.shell" -> console_ingress;
}

worker console_ingress(ConsoleViewRequest request) {
  local FrameManagerFrame focus;
  local FrameManagerFrame frame;

  focus.manager_id = 1;
  focus.frame_id = request.sequence;
  focus.frame_kind = 0;
  focus.flags = 0;
  focus.arg0 = request.opcode;
  focus.arg1 = 0;
  focus.arg2 = 0;
  focus.arg3 = 0;
  far_signal("elara.os.frame_authority", manager_ingress, focus);

  frame.manager_id = 1;
  frame.frame_id = request.sequence;
  frame.frame_kind = 1;
  frame.flags = 0;
  frame.arg0 = request.cursor_x;
  frame.arg1 = request.cursor_y;
  frame.arg2 = 0;
  frame.arg3 = 0;
  far_signal("elara.os.frame_authority", manager_ingress, frame);

  retire_worker();
}
