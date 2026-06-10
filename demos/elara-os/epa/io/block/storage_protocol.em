type BlockDeviceRegistration(
  int drive_id,
  int block_size,
  int block_count,
  int flags,
  int mount_id,
  int mount_size,
  byte[] mount_data
) {
  return drive_id;
}

type BlockIoRequest(int opcode, int drive_id, int lba, int block_count) {
  return opcode;
}

type FileSystemMount(
  int mount_id,
  int drive_id,
  int fs_kind,
  int flags,
  int mount_size,
  byte[] mount_data
) {
  return mount_id;
}

type FileSystemRequest(int opcode, int mount_id, int path_handle, int arg0) {
  return opcode;
}
