#include "../block/storage_protocol.em"
#include "../frame/frame_io_protocol.em"
#include "../../dynamic_acl_protocol.em"
#include "ext4_protocol.em"
#include "../../registry/shell/shell_boot_protocol.em"

type FileSystemMountState(int mount_id, int drive_id, int fs_kind, int flags, int mount_path) {
  return mount_id;
}

type FileNodeState(int mount_id, int path_handle, int object_id, int flags) {
  return path_handle;
}

dynamic fs_mounts(FileSystemMountState, 4, 64, 8);
dynamic fs_nodes(FileNodeState, 8, 128, 16);
dynamic ext4_mounts(Ext4MountState, 4, 64, 8);
dynamic ext4_root_inodes(Ext4RootInodeState, 4, 64, 8);
dynamic ext4_dir_entries(Ext4DirectoryEntryState, 16, 256, 32);

type FileSystemBlockProbeState(int drive_id, int lba, int byte_count, int signature_lo, int signature_hi) {
  return drive_id;
}

dynamic fs_block_probes(FileSystemBlockProbeState, 4, 64, 8);

type FileSystemBootWalkState(
  int drive_id,
  int mount_id,
  int phase,
  int block_size,
  int inode_size,
  int inodes_per_group,
  int inode_table_block,
  int target_inode,
  int target_hash,
  int last_lba,
  int last_block_count,
  int parsed0,
  int parsed1
) {
  return phase;
}

dynamic fs_boot_walks(FileSystemBootWalkState, 16, 128, 16);

function int fs_boot_walk_phase_group_desc() {
  return 1;
}

function int fs_boot_walk_phase_pending_boot_entry() {
  return 0;
}

function int fs_boot_walk_phase_boot_inode() {
  return 2;
}

function int fs_boot_walk_phase_boot_inode_parsed() {
  return 3;
}

function int fs_path_hash_boot() {
  return 0 - 795565611;
}

function int fs_result_payload_u8(BlockIoReadResult result, int payload_offset) {
  return local_load_u8(result.payload, payload_offset);
}

function int fs_result_payload_u16_le(BlockIoReadResult result, int payload_offset) {
  return fs_result_payload_u8(result, payload_offset)
    + (fs_result_payload_u8(result, payload_offset + 1) * 256);
}

function int fs_result_payload_u32_le(BlockIoReadResult result, int payload_offset) {
  return fs_result_payload_u8(result, payload_offset)
    + (fs_result_payload_u8(result, payload_offset + 1) * 256)
    + (fs_result_payload_u8(result, payload_offset + 2) * 65536)
    + (fs_result_payload_u8(result, payload_offset + 3) * 16777216);
}

kernel(VM vm) {
  kernalId("elara.os.filesystem");
  static {
    set_worker_privilege(filesystem_boot_shell_image_ingress, 100);
  }
  start_worker(register_mount);
  start_worker(filesystem_ingress);
  start_worker(register_directory_entry);
  start_worker(filesystem_boot_assets_ingress);
  start_worker(filesystem_boot_shell_image_ingress);
  start_worker(filesystem_block_read_result_ingress);
}

acl {
  "elara.os.boot" -> register_mount;
  "elara.os.block_io" -> filesystem_block_read_result_ingress;
  "elara.os.shell" -> filesystem_ingress;
  "elara.app.example" -> filesystem_ingress;
  "elara.os.filesystem" -> filesystem_boot_assets_ingress;
  "elara.os.filesystem" -> filesystem_boot_shell_image_ingress;
}

@attributes signal_mail_box_size:2048
worker register_mount(FileSystemMount mount) {
  static int mount_count;
  static int registered;
  static int root_mount_id;
  static int root_drive_id;
  static int fstab_scan_pending;
  int slot = dyn_alloc(fs_mounts);
  int node_slot = 0 - 1;
  int ext4_slot = 0 - 1;
  int root_inode_slot = 0 - 1;
  local FileSystemMountState state;
  local FileNodeState root_node;
  local FileSystemMount staged;
  local Ext4MountState ext4_state;
  local Ext4RootInodeState root_inode_state;

  static {
    mount_count = 0;
  }

  staged.mount_id = mount.mount_id;
  staged.drive_id = mount.drive_id;
  staged.fs_kind = mount.fs_kind;
  staged.flags = mount.flags;
  staged.mount_path_hash = mount.mount_path_hash;

  state.mount_id = staged.mount_id;
  state.drive_id = staged.drive_id;
  state.fs_kind = staged.fs_kind;
  state.flags = staged.flags;
  state.mount_path = staged.mount_path_hash;
  fs_mounts[slot] = state;

  if (staged.fs_kind == filesystem_kind_ext4()) {
    ext4_slot = dyn_alloc(ext4_mounts);
    root_inode_slot = dyn_alloc(ext4_root_inodes);
    ext4_state.mount_id = staged.mount_id;
    ext4_state.drive_id = staged.drive_id;
    ext4_state.status = ext4_status_bad_superblock();
    ext4_state.block_size = 0;
    ext4_state.block_count = 0;
    ext4_state.inode_count = 0;
    ext4_state.blocks_per_group = 0;
    ext4_state.inodes_per_group = 0;
    ext4_state.inode_size = 0;
    ext4_state.feature_compat = 0;
    ext4_state.feature_incompat = 0;
    ext4_state.feature_ro_compat = 0;

    root_inode_state.mount_id = staged.mount_id;
    root_inode_state.inode_number = ext4_inode_root_number();
    root_inode_state.status = ext4_inode_status_bad_root();
    root_inode_state.mode = mount.root_inode_mode;
    root_inode_state.size_lo = mount.root_inode_size_lo;
    root_inode_state.blocks_lo = mount.root_inode_blocks_lo;
    root_inode_state.flags = mount.root_inode_flags;
    root_inode_state.extent_magic = mount.root_extent_magic;
    root_inode_state.extent_entries = mount.root_extent_entries;
    root_inode_state.extent_depth = mount.root_extent_depth;

    if (mount.superblock_magic == ext4_superblock_magic_value()) {
      ext4_state.status = ext4_status_mounted();
      ext4_state.block_size = mount.block_size;
      ext4_state.block_count = mount.block_count;
      ext4_state.inode_count = mount.inode_count;
      ext4_state.blocks_per_group = mount.blocks_per_group;
      ext4_state.inodes_per_group = mount.inodes_per_group;
      ext4_state.inode_size = mount.inode_size;
      ext4_state.feature_compat = mount.feature_compat;
      ext4_state.feature_incompat = mount.feature_incompat;
      ext4_state.feature_ro_compat = mount.feature_ro_compat;

      if (ext4_inode_is_directory(mount.root_inode_mode) == 1) {
        if (mount.root_extent_magic == ext4_extent_magic_value()) {
          root_inode_state.status = ext4_inode_status_root_directory();
          node_slot = dyn_alloc(fs_nodes);
          root_node.mount_id = staged.mount_id;
          root_node.path_handle = staged.mount_path_hash;
          root_node.object_id = ext4_inode_root_number();
          root_node.flags = ext4_inode_mode_directory_mask();
          fs_nodes[node_slot] = root_node;
        }
      }
    }

    ext4_mounts[ext4_slot] = ext4_state;
    ext4_root_inodes[root_inode_slot] = root_inode_state;
  }

  if (filesystem_mount_is_root(staged.flags) == 1) {
    root_mount_id = staged.mount_id;
    root_drive_id = staged.drive_id;
    fstab_scan_pending = 1;
  }

  mount_count = mount_count + 1;

  // Root is mounted directly from boot metadata for now. The follow-up
  // /etc/fstab scan remains EPA-owned, but waits until filesystem reads exist.
  if (fstab_scan_pending == 1) {
    if (root_mount_id > 0) {
      if (root_drive_id > 0) {
        fstab_scan_pending = 0;
      }
    }
  }

  registered = 1;

  host_signal();
}

worker filesystem_boot_assets_ingress(FileSystemBootAssetRequest request) {
  int opcode = request.opcode;
  int drive_id = request.mount_id;
  int target_inode = request.phase;
  int block_size = request.flags;
  int boot_walk_slot = 0 - 1;
  int group_desc_byte_offset = 0;
  int group_desc_lba = 0;
  int group_desc_block_count = 1;
  local FrameRequest frame_request;
  local FileSystemBootWalkState boot_walk;
  local BlockIoRequest block_request;

  // EPA-owned boot asset loading starts here. The next implementation step is
  // ext4 path lookup + file byte reads for shell.epa.bin, png_decoder.epa.bin,
  // and assets/logo.png from the mounted root.
  if (opcode == filesystem_boot_assets_opcode_load_shell()) {
    frame_request.opcode = 3;
    frame_request.surface_id = 1;
    frame_request.arg0 = 1254;
    frame_request.arg1 = 1254;
    far_signal("elara.os.frame_io", 3, frame_request);
  }

  if (opcode == filesystem_boot_assets_opcode_kick_ext4_walk()) {
          if (block_size > 0) {
            boot_walk_slot = dyn_alloc(fs_boot_walks);
            group_desc_byte_offset = block_size;
            if (block_size == 1024) {
              group_desc_byte_offset = 2048;
            }
            group_desc_lba = group_desc_byte_offset / 512;
            group_desc_block_count = block_size / 512;
            if (group_desc_block_count < 1) {
              group_desc_block_count = 1;
            }
            if (group_desc_block_count > 8) {
              group_desc_block_count = 8;
            }

            boot_walk.drive_id = drive_id;
            boot_walk.mount_id = 1;
            boot_walk.phase = fs_boot_walk_phase_group_desc();
            boot_walk.block_size = block_size;
            boot_walk.inode_size = 256;
            boot_walk.inodes_per_group = 0;
            boot_walk.inode_table_block = 0;
            boot_walk.target_inode = target_inode;
            boot_walk.target_hash = fs_path_hash_boot();
            boot_walk.last_lba = group_desc_lba;
            boot_walk.last_block_count = group_desc_block_count;
            boot_walk.parsed0 = 0;
            boot_walk.parsed1 = 0;
            fs_boot_walks[boot_walk_slot] = boot_walk;

            block_request.opcode = block_io_opcode_read_blocks();
            block_request.drive_id = drive_id;
            block_request.lba = group_desc_lba;
            block_request.block_count = group_desc_block_count;
            frame_begin(7357001, drive_id, target_inode, group_desc_lba, group_desc_block_count);
            frame_commit();
            far_signal("elara.os.block_io", 2, block_request);
            retire_worker();
          }
  }

  opcode = opcode;
  drive_id = drive_id;
  target_inode = target_inode;
  block_size = block_size;
  host_signal();
}

worker filesystem_boot_shell_image_ingress(ShellProcessImage image) {
  static int spawned;

  if (spawned == 0) {
    process_spawn(image, 2);
    spawned = 1;
  }

  host_signal();
}

worker filesystem_block_read_result_ingress(BlockIoReadResult result) {
  int slot = dyn_alloc(fs_block_probes);
  int walk_slot = 0 - 1;
  int inode_index = 0;
  int inode_byte_offset = 0;
  int inode_lba = 0;
  int inode_block_count = 1;
  int inode_payload_offset = 0;
  local FileSystemBlockProbeState state;
  local FileSystemBootWalkState walk;
  local BlockIoRequest block_request;

  state.drive_id = result.drive_id;
  state.lba = result.lba;
  state.byte_count = result.byte_count;
  state.signature_lo = 0;
  state.signature_hi = 0;

  fs_block_probes[slot] = state;
  frame_begin(7357002, result.drive_id, result.lba, result.byte_count, slot);
  frame_commit();

  walk = fs_boot_walks[1:2];
  if (walk.phase > 0) {
    if (walk.drive_id == result.drive_id) {
      if (walk.last_lba == result.lba) {
        if (walk.phase == fs_boot_walk_phase_group_desc()) {
          if (result.byte_count > 11) {
            if (walk.target_inode > 0) {
              walk_slot = 0;
              walk.inode_table_block = fs_result_payload_u32_le(result, 8);
              walk.phase = fs_boot_walk_phase_boot_inode();
              inode_index = walk.target_inode - 1;
              inode_byte_offset = (walk.inode_table_block * walk.block_size) + (inode_index * walk.inode_size);
              inode_lba = inode_byte_offset / 512;
              inode_block_count = walk.block_size / 512;
              if (inode_block_count < 1) {
                inode_block_count = 1;
              }
              if (inode_block_count > 8) {
                inode_block_count = 8;
              }
              walk.last_lba = inode_lba;
              walk.last_block_count = inode_block_count;
              walk.parsed0 = walk.inode_table_block;
              walk.parsed1 = inode_byte_offset;
              fs_boot_walks[walk_slot] = walk;

              block_request.opcode = block_io_opcode_read_blocks();
              block_request.drive_id = walk.drive_id;
              block_request.lba = inode_lba;
              block_request.block_count = inode_block_count;
              far_signal("elara.os.block_io", 2, block_request);
            }
          }
        }

        if (walk.phase == fs_boot_walk_phase_boot_inode()) {
          if (result.byte_count > 127) {
            walk_slot = dyn_alloc(fs_boot_walks);
            inode_index = walk.target_inode - 1;
            inode_byte_offset = (walk.inode_table_block * walk.block_size) + (inode_index * walk.inode_size);
            inode_payload_offset = inode_byte_offset - (walk.last_lba * 512);
            walk.phase = fs_boot_walk_phase_boot_inode_parsed();
            walk.parsed0 = fs_result_payload_u16_le(result, inode_payload_offset + 40);
            walk.parsed1 = fs_result_payload_u32_le(result, inode_payload_offset + 60);
            fs_boot_walks[walk_slot] = walk;
          }
        }
      }
    }
  }

  host_signal();
}

worker filesystem_ingress(FileSystemRequest request) {
  int mount_iter = dynamic_iterator(fs_mounts);
  int node_iter = dynamic_iterator(fs_nodes);
  mount_iter = mount_iter;
  node_iter = node_iter;
  host_signal();
}

@attributes signal_mail_box_size:4096
worker register_directory_entry(Ext4DirectoryEntryRegistration entry) {
  int slot = dyn_alloc(ext4_dir_entries);
  int node_slot = 0 - 1;
  local Ext4DirectoryEntryState state;
  local FileNodeState node;

  state.mount_id = entry.mount_id;
  state.parent_inode = entry.parent_inode;
  state.inode_number = entry.inode_number;
  state.name_hash = entry.name_hash;
  state.file_type = entry.file_type;
  state.name_len = entry.name_len;
  state.rec_len = entry.rec_len;
  ext4_dir_entries[slot] = state;

  if (entry.inode_number > 0) {
    node_slot = dyn_alloc(fs_nodes);
    node.mount_id = entry.mount_id;
    node.path_handle = entry.name_hash;
    node.object_id = entry.inode_number;
    node.flags = entry.file_type;
    fs_nodes[node_slot] = node;
  }

  host_signal();
}
