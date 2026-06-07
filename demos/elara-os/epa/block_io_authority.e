#include "storage_protocol.em"

type BlockDeviceState(int drive_id, int block_size, int block_count, int flags, int mount_id, int mount_path) {
  return drive_id;
}

type BlockIoStatus(int drive_id, int status, int arg0, int arg1) {
  return status;
}

dynamic block_devices(BlockDeviceState, 4, 64, 8);

kernel(VM vm) {
  kernalId("elara.os.block_io");
  start_worker(register_block_device);
  start_worker(block_io_ingress);
}

acl {
  "elara.os.boot" -> register_block_device;
  "elara.os.filesystem" -> block_io_ingress;
  "elara.os.shell" -> block_io_ingress;
  "elara.app.example" -> block_io_ingress;
}

worker register_block_device(BlockDeviceRegistration registration) {
  static int registered_count;
  int slot = dyn_alloc(block_devices);
  local BlockDeviceState device;
  local BlockDeviceRegistration staged;

  static {
    registered_count = 0;
  }

  staged.drive_id = registration.drive_id;
  staged.block_size = registration.block_size;
  staged.block_count = registration.block_count;
  staged.flags = registration.flags;
  staged.mount_id = registration.mount_id;
  staged.mount_size = registration.mount_size;
  staged.mount_data = registration.mount_data;

  device.drive_id = staged.drive_id;
  device.block_size = staged.block_size;
  device.block_count = staged.block_count;
  device.flags = staged.flags;
  device.mount_id = staged.mount_id;
  device.mount_path = staged.mount_size;
  block_devices[slot] = device;

  registered_count = registered_count + 1;
  host_signal();
}

worker block_io_ingress(BlockIoRequest request) {
  int device_iter = dynamic_iterator(block_devices);
  device_iter = device_iter;
  host_signal();
}
