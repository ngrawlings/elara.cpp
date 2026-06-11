#include "../block/storage_protocol.em"
#include "../../dynamic_acl_protocol.em"

type FileSystemMountState(int mount_id, int drive_id, int fs_kind, int flags, int mount_path) {
  return mount_id;
}

type FileNodeState(int mount_id, int path_handle, int object_id, int flags) {
  return path_handle;
}

dynamic fs_mounts(FileSystemMountState, 4, 64, 8);
dynamic fs_nodes(FileNodeState, 8, 128, 16);

kernel(VM vm) {
  kernalId("elara.os.filesystem");
  start_worker(register_mount);
  start_worker(filesystem_ingress);
}

acl {
  "elara.os.boot" -> register_mount;
  "elara.os.shell" -> filesystem_ingress;
  "elara.app.example" -> filesystem_ingress;
}

worker register_mount(MountRegistration mount) {
  static int mount_count;
  static int registered;
  static int root_mount_id;
  static int root_drive_id;
  static int fstab_scan_pending;
  int slot = dyn_alloc(fs_mounts);
  local FileSystemMountState state;
  local MountRegistration staged;
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
