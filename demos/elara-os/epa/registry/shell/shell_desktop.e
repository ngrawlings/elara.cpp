#include "../../dynamic_acl_protocol.em"

type ShellEvent(int opcode, int target_id, int arg0, int arg1) {
  return opcode;
}

type ShellSurface(int surface_id, int x, int y, int flags) {
  return surface_id;
}

dynamic shell_surfaces(ShellSurface, 4, 128, 8);

kernel(VM vm) {
  kernalId("elara.os.shell");
  start_worker(shell_ingress);
}

acl {
  "elara.os.hid_io" -> shell_ingress;
  "elara.os.frame_io" -> shell_ingress;
  "elara.app.example" -> shell_ingress;
}

worker shell_ingress(ShellEvent event) {
  static int registered;
  local DynamicACLRequest acl_request;
  int surface = dyn_alloc(shell_surfaces);

  if (registered == 0) {
    acl_request.opcode = dynamic_acl_opcode_register();
    acl_request.route_id = dynamic_acl_authority_shell();
    acl_request.flags = dynamic_acl_authority_registry();
    acl_request.reserved = 0;
    far_signal("elara.os.entry", dynamic_acl_authority, acl_request);
    registered = 1;
  }

  host_signal();
}
