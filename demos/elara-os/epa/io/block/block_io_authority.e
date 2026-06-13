#include "storage_protocol.em"
#include "../../dynamic_acl_protocol.em"

type BlockDeviceState(int drive_id, int block_size, int block_count, int flags) {
  return drive_id;
}

type BlockIoStatus(int drive_id, int status, int arg0, int arg1) {
  return status;
}

type PendingBlockIoRequest(
  int drive_id,
  int lba,
  int block_count,
  int phase,
  int target_inode,
  int block_size,
  int inode_table_block
) {
  return drive_id;
}

dynamic block_devices(BlockDeviceState, 4, 64, 8);
dynamic block_io_pending(PendingBlockIoRequest, 8, 128, 16);

kernel(VM vm) {
  kernalId("elara.os.block_io");
  start_worker(register_block_device);
  start_worker(block_io_ingress);
  start_worker(block_io_read_result_ingress);
}

acl {
  "elara.os.boot" -> register_block_device;
  "elara.os.filesystem" -> block_io_ingress;
  "elara.os.shell" -> block_io_ingress;
  "elara.app.example" -> block_io_ingress;
}

worker register_block_device(BlockDeviceRegistration registration) {
  static int registered_count;
  static int registered;
  int slot = dyn_alloc(block_devices);
  local BlockDeviceState device;
  local BlockDeviceRegistration staged;
  local DynamicACLRequest acl_request;

  static {
    registered_count = 0;
  }

  if (registered == 0) {
    acl_request.opcode = dynamic_acl_opcode_register();
    acl_request.route_id = dynamic_acl_authority_block_io();
    acl_request.flags = dynamic_acl_authority_registry();
    acl_request.reserved = 0;
    far_signal("elara.os.entry", dynamic_acl_authority, acl_request);
    registered = 1;
  }

  staged.drive_id = registration.drive_id;
  staged.block_size = registration.block_size;
  staged.block_count = registration.block_count;
  staged.flags = registration.flags;

  device.drive_id = staged.drive_id;
  device.block_size = staged.block_size;
  device.block_count = staged.block_count;
  device.flags = staged.flags;
  block_devices[slot] = device;

  registered_count = registered_count + 1;
  host_signal();
}

worker block_io_ingress(BlockIoRequest request) {
  int device_iter = dynamic_iterator(block_devices);
  int pending_slot = 0 - 1;
  local PendingBlockIoRequest pending;
  device_iter = device_iter;

  if (request.opcode == block_io_opcode_read_blocks()) {
    pending_slot = dyn_alloc(block_io_pending);
    pending.drive_id = request.drive_id;
    pending.lba = request.lba;
    pending.block_count = request.block_count;
    pending.phase = request.phase;
    pending.target_inode = request.target_inode;
    pending.block_size = request.block_size;
    pending.inode_table_block = request.inode_table_block;
    block_io_pending[pending_slot] = pending;

    frame_begin(
      block_io_mailbox_magic(),
      request.drive_id,
      request.opcode,
      request.lba,
      request.block_count
    );
    frame_commit();
  } else {
    host_signal();
  }
}

worker block_io_read_result_ingress(BlockIoReadResult raw_result) {
  int pending_iter = dynamic_iterator(block_io_pending);
  local BlockIoReadResult result;

  result.opcode = raw_result.opcode;
  result.drive_id = raw_result.drive_id;
  result.lba = raw_result.lba;
  result.block_count = raw_result.block_count;
  result.byte_count = raw_result.byte_count;
  result.phase = 0;
  result.target_inode = 0;
  result.block_size = 0;
  result.inode_table_block = 0;
  result.arg0 = 0;
  result.arg1 = 0;

  while (PendingBlockIoRequest pending = dynamic_next(pending_iter)) {
    if (result.phase == 0) {
      if (pending.drive_id == raw_result.drive_id) {
        if (pending.lba == raw_result.lba) {
          if (pending.block_count == raw_result.block_count) {
            result.phase = pending.phase;
            result.target_inode = pending.target_inode;
            result.block_size = pending.block_size;
            result.inode_table_block = pending.inode_table_block;
          }
        }
      }
    }
  }

  far_signal_current_payload("elara.os.filesystem", 4, result);
}
