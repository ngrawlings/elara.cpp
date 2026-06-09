#include "frame_protocol.em"
#include "storage_protocol.em"
#include "common/bytes.em"

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

kernel(VM vm) {
  kernalId("elara.os.boot");
  start_worker(boot_ingress);
}

acl {
  "elara.os.host" -> boot_ingress;
  "elara.os.security" -> boot_ingress;
}

worker boot_ingress(BootDeviceList trigger) {
  local FrameBoot boot_frame;

  boot_frame.phase = 1;
  boot_frame.flags = 0;
  far_signal("elara.os.frame_authority", publish_boot_frame, boot_frame);
  retire_worker();
}
