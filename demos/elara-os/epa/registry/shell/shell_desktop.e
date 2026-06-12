#include "../../dynamic_acl_protocol.em"
#include "../../io/frame/frame_io_protocol.em"
#include "../../lib/png/png_protocol.em"
#include "shell_boot_protocol.em"

type ShellEvent(int opcode, int target_id, int arg0, int arg1) {
  return opcode;
}

type ShellSurface(int surface_id, int x, int y, int flags) {
  return surface_id;
}

dynamic shell_surfaces(ShellSurface, 4, 128, 8);

kernel(VM vm) {
  kernalId("elara.os.shell");
  static {
    set_worker_privilege(shell_png_library_ingress, 100);
  }
  start_worker(shell_ingress);
  start_worker(shell_png_library_ingress);
  start_worker(shell_boot_logo_ingress);
}

acl {
  "elara.os.hid_io" -> shell_ingress;
  "elara.os.frame_io" -> shell_ingress;
  "elara.app.example" -> shell_ingress;
  "elara.os.filesystem" -> shell_png_library_ingress;
  "elara.os.filesystem" -> shell_boot_logo_ingress;
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
    far_signal("elara.os.entry", 2, acl_request);
    registered = 1;
  }

  host_signal();
}

worker shell_png_library_ingress(ShellPngLibraryImage image) {
  static int imported;

  if (imported == 0) {
    dynamic_import(image, "modules.png");
    imported = 1;
  }

  host_signal();
}

worker shell_boot_logo_ingress(ShellBootLogoHeader logo) {
  local PngDecodeRequest request;
  local FrameRequest frame_request;

  request.opcode = png_decode_opcode_decode_header();
  request.request_id = logo.request_id;
  request.input_size = logo.input_size;
  request.signature_lo = logo.signature_lo;
  request.signature_hi = logo.signature_hi;
  request.ihdr_length = logo.ihdr_length;
  request.ihdr_type = logo.ihdr_type;
  request.width = logo.width;
  request.height = logo.height;
  request.bit_depth = logo.bit_depth;
  request.color_type = logo.color_type;
  request.compression_method = logo.compression_method;
  request.filter_method = logo.filter_method;
  request.interlace_method = logo.interlace_method;
  far_signal("modules.png", 1, request);

  frame_request.opcode = 3;
  frame_request.surface_id = 1;
  frame_request.arg0 = logo.width;
  frame_request.arg1 = logo.height;
  far_signal("elara.os.frame_io", 3, frame_request);

  host_signal();
}
