#include "../storage_protocol.em"

type PartitionScanRequest(
  int drive_id,
  int block_size,
  int block_count,
  int flags
) {
  return drive_id;
}

type PartitionIoStatus(
  int drive_id,
  int status,
  int arg0,
  int arg1
) {
  return status;
}

type PartitionRegistration(
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

function int partition_flag_mount_root() {
  return 1;
}
