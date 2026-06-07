#include "storage_protocol.em"

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

worker register_mount(FileSystemMount mount) {
  static int mount_count;
  int slot = dyn_alloc(fs_mounts);
  local FileSystemMountState state;
  local FileSystemMount staged;

  static {
    mount_count = 0;
  }

  staged.mount_id = mount.mount_id;
  staged.drive_id = mount.drive_id;
  staged.fs_kind = mount.fs_kind;
  staged.flags = mount.flags;
  staged.mount_size = mount.mount_size;
  staged.mount_data = mount.mount_data;

  state.mount_id = staged.mount_id;
  state.drive_id = staged.drive_id;
  state.fs_kind = staged.fs_kind;
  state.flags = staged.flags;
  state.mount_path = staged.mount_size;
  fs_mounts[slot] = state;

  mount_count = mount_count + 1;
  host_signal();
}

worker filesystem_ingress(FileSystemRequest request) {
  int mount_iter = dynamic_iterator(fs_mounts);
  int node_iter = dynamic_iterator(fs_nodes);
  mount_iter = mount_iter;
  node_iter = node_iter;
  host_signal();
}
