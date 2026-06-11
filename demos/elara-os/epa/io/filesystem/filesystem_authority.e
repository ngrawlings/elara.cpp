#include "../block/storage_protocol.em"
#include "../../dynamic_acl_protocol.em"
#include "ext4_protocol.em"

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

kernel(VM vm) {
  kernalId("elara.os.filesystem");
  start_worker(register_mount);
  start_worker(filesystem_ingress);
  start_worker(register_directory_entry);
}

acl {
  "elara.os.boot" -> register_mount;
  "elara.os.shell" -> filesystem_ingress;
  "elara.app.example" -> filesystem_ingress;
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
  local DynamicACLRequest acl_request;

  static {
    mount_count = 0;
  }

  if (registered == 0) {
    acl_request.opcode = dynamic_acl_opcode_register();
    acl_request.route_id = dynamic_acl_authority_filesystem();
    acl_request.flags = dynamic_acl_authority_registry();
    acl_request.reserved = 0;
    far_signal("elara.os.entry", dynamic_acl_authority, acl_request);
    registered = 1;
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
