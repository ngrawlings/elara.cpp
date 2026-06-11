#include "partition_io_protocol.em"
#include "../../../dynamic_acl_protocol.em"

type PartitionDeviceState(
  int drive_id,
  int partition_drive_id,
  int partition_index,
  int first_lba,
  int last_lba,
  int fs_kind,
  int flags
) {
  return partition_drive_id;
}

dynamic partition_devices(PartitionDeviceState, 8, 64, 8);

kernel(VM vm) {
  kernalId("elara.os.partition_io");
  start_worker(scan_partition_table);
  start_worker(register_partition);
}

acl {
  "elara.os.boot" -> scan_partition_table;
  "elara.os.host" -> register_partition;
}

@attributes signal_mail_box_size:64
worker scan_partition_table(PartitionScanRequest request) {
  static int registered;
  local DynamicACLRequest acl_request;
  int drive_id = request.drive_id;
  int block_size = request.block_size;
  int block_count = request.block_count;
  int flags = request.flags;

  if (registered == 0) {
    acl_request.opcode = dynamic_acl_opcode_register();
    acl_request.route_id = dynamic_acl_authority_partition_io();
    acl_request.flags = dynamic_acl_authority_block_io();
    acl_request.reserved = 0;
    far_signal("elara.os.entry", dynamic_acl_authority, acl_request);
    registered = 1;
  }
  drive_id = drive_id;
  block_size = block_size;
  block_count = block_count;
  flags = flags;
}

worker register_partition(PartitionRegistration registration) {
  int slot = dyn_alloc(partition_devices);
  local PartitionDeviceState state;

  state.drive_id = registration.drive_id;
  state.partition_drive_id = registration.partition_drive_id;
  state.partition_index = registration.partition_index;
  state.first_lba = registration.first_lba;
  state.last_lba = registration.last_lba;
  state.fs_kind = registration.fs_kind;
  state.flags = registration.flags;
  partition_devices[slot] = state;

  host_signal();
}
