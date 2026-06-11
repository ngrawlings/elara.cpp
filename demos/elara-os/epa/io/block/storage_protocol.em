#include "common/hash.em"

type BlockDeviceRegistration(
  int drive_id,
  int block_size,
  int block_count,
  int flags
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

type MountRegistration(
  int mount_id,
  int drive_id,
  int fs_kind,
  int flags,
  int mount_path_hash
) {
  return mount_id;
}

type FileSystemRequest(int opcode, int mount_id, int path_handle, int arg0) {
  return opcode;
}

function int block_device_flag_root_filesystem() {
  return 1;
}

function int block_device_is_root_filesystem(int flags) {
  if (flags == block_device_flag_root_filesystem()) {
    return 1;
  }
  return 0;
}

function int filesystem_kind_ext4() {
  return 1;
}

function int filesystem_mount_flag_root() {
  return 1;
}

function int filesystem_mount_flag_scan_fstab() {
  return 2;
}

function int filesystem_mount_is_root(int flags) {
  if (flags == filesystem_mount_flag_root()) {
    return 1;
  }
  if (flags == (filesystem_mount_flag_root() + filesystem_mount_flag_scan_fstab())) {
    return 1;
  }
  return 0;
}

function int filesystem_mount_path_root() {
  return hash_u32("/");
}

function int filesystem_mount_path_etc_fstab() {
  return hash_u32("/etc/fstab");
}
