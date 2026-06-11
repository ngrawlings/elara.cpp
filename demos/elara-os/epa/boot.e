#include "io/frame/frame_io_protocol.em"
#include "dynamic_acl_protocol.em"

type BootDeviceList(
  int version,
  int device_count,
  int payload_size,
  byte[] payload
) {
  return version;
}

type BootAssets(int version, int flags) {
  return version;
}

type BootKernelImage(byte[] payload) {
  return payload;
}

kernel(VM vm) {
  kernalId("elara.os.boot");

  static {
    set_worker_privilege(boot_kernel_ingress, 100);
  }

  start_worker(boot_ingress);
  start_worker(boot_kernel_ingress);
}

acl {
  "elara.os.host" -> boot_ingress;
  "elara.os.security" -> boot_ingress;
}

worker boot_ingress(BootDeviceList trigger) {
  local FrameBoot boot_frame;
  local DynamicACLRequest acl_request;
  int version = trigger.version;
  int device_count = trigger.device_count;
  int payload_size = trigger.payload_size;
  version = version;
  device_count = device_count;
  payload_size = payload_size;

  acl_request.opcode = dynamic_acl_opcode_register();
  acl_request.route_id = dynamic_acl_authority_boot();
  acl_request.flags = dynamic_acl_authority_frame_io();
  acl_request.reserved = 0;
  far_signal("elara.os.entry", dynamic_acl_authority, acl_request);

  boot_frame.phase = 1;
  boot_frame.flags = 0;
  far_signal("elara.os.frame_io", publish_boot_frame, boot_frame);

  retire_worker();
}

worker boot_kernel_ingress(BootKernelImage image) {
  boot_stage_image(image, 0);
  boot_reset_to_staged(0);
  retire_worker();
}
