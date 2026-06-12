#include "common/hash.em"
#include "common/bytes.em"

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

type BlockIoReadResult(
  int opcode,
  int drive_id,
  int lba,
  int block_count,
  int byte_count,
  byte[] payload
) {
  return opcode;
}

type FileSystemMount(
  int mount_id,
  int drive_id,
  int fs_kind,
  int flags,
  int mount_path_hash,
  int superblock_magic,
  int block_size,
  int block_count,
  int inode_count,
  int blocks_per_group,
  int inodes_per_group,
  int inode_size,
  int feature_compat,
  int feature_incompat,
  int feature_ro_compat,
  int root_inode_mode,
  int root_inode_size_lo,
  int root_inode_blocks_lo,
  int root_inode_flags,
  int root_extent_magic,
  int root_extent_entries,
  int root_extent_depth
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

type FileSystemBootAssetRequest(int opcode, int mount_id, int phase, int flags) {
  return opcode;
}

function int block_device_flag_root_filesystem() {
  return 1;
}

function int block_io_opcode_read_blocks() {
  return 1;
}

function int block_io_opcode_read_result() {
  return 2;
}

function int block_io_mailbox_magic() {
  return 7253218;
}

function int block_device_is_root_filesystem(int flags) {
  if (bit_and_i32(flags, block_device_flag_root_filesystem()) == block_device_flag_root_filesystem()) {
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
  if (bit_and_i32(flags, filesystem_mount_flag_root()) == filesystem_mount_flag_root()) {
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

function int filesystem_boot_assets_opcode_load_shell() {
  return 1;
}

function int filesystem_boot_assets_opcode_kick_ext4_walk() {
  return 2;
}
