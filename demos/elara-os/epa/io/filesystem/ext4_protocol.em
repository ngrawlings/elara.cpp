#ifndef ELARA_OS_EXT4_PROTOCOL_EM
#define ELARA_OS_EXT4_PROTOCOL_EM

#include "common/bytes.em"

type Ext4MountState(
  int mount_id,
  int drive_id,
  int status,
  int block_size,
  int block_count,
  int inode_count,
  int blocks_per_group,
  int inodes_per_group,
  int inode_size,
  int feature_compat,
  int feature_incompat,
  int feature_ro_compat
) {
  return mount_id;
}

type Ext4RootInodeState(
  int mount_id,
  int inode_number,
  int status,
  int mode,
  int size_lo,
  int blocks_lo,
  int flags,
  int extent_magic,
  int extent_entries,
  int extent_depth
) {
  return mount_id;
}

type Ext4DirectoryEntryRegistration(
  int mount_id,
  int parent_inode,
  int inode_number,
  int name_hash,
  int file_type,
  int name_len,
  int rec_len
) {
  return inode_number;
}

type Ext4DirectoryEntryState(
  int mount_id,
  int parent_inode,
  int inode_number,
  int name_hash,
  int file_type,
  int name_len,
  int rec_len
) {
  return inode_number;
}

function int ext4_status_empty() {
  return 0;
}

function int ext4_status_mounted() {
  return 1;
}

function int ext4_status_bad_superblock() {
  return 0 - 1;
}

function int ext4_inode_status_bad_root() {
  return 0 - 2;
}

function int ext4_inode_status_root_directory() {
  return 2;
}

function int ext4_inode_root_number() {
  return 2;
}

function int ext4_inode_mode_directory_mask() {
  return 16384;
}

function int ext4_inode_is_directory(int mode) {
  if (bit_and_i32(mode, ext4_inode_mode_directory_mask()) == ext4_inode_mode_directory_mask()) {
    return 1;
  }
  return 0;
}

function int ext4_extent_magic_value() {
  return 62218;
}

function int ext4_dir_file_type_directory() {
  return 2;
}

function int ext4_dir_file_type_regular() {
  return 1;
}

function int ext4_superblock_size() {
  return 1024;
}

function int ext4_superblock_magic_value() {
  return 61267;
}

function int ext4_superblock_magic(int sb_off) {
  return u16_load_le(sb_off + 56);
}

function int ext4_superblock_inode_count(int sb_off) {
  return u32_load_le(sb_off + 0);
}

function int ext4_superblock_block_count_lo(int sb_off) {
  return u32_load_le(sb_off + 4);
}

function int ext4_superblock_log_block_size(int sb_off) {
  return u32_load_le(sb_off + 24);
}

function int ext4_superblock_block_size(int sb_off) {
  int log_size = ext4_superblock_log_block_size(sb_off);
  int size = 1024;
  while (log_size > 0) {
    size = size * 2;
    log_size = log_size - 1;
  }
  return size;
}

function int ext4_superblock_blocks_per_group(int sb_off) {
  return u32_load_le(sb_off + 32);
}

function int ext4_superblock_inodes_per_group(int sb_off) {
  return u32_load_le(sb_off + 40);
}

function int ext4_superblock_inode_size(int sb_off) {
  return u16_load_le(sb_off + 88);
}

function int ext4_superblock_feature_compat(int sb_off) {
  return u32_load_le(sb_off + 92);
}

function int ext4_superblock_feature_incompat(int sb_off) {
  return u32_load_le(sb_off + 96);
}

function int ext4_superblock_feature_ro_compat(int sb_off) {
  return u32_load_le(sb_off + 100);
}

#endif
