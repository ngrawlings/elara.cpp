# Elara OS Authority Map

Living map of the current Elara OS authority structure. Update this file when
an authority, route, registry node, or Dynamic ACL rule changes.

## Authority Tree

Each tree edge means the child is inside the parent's authority namespace. The
Registry stores each path segment by `hash_u32("segment")`, so a path lookup is
one hashed node per level.

```text
elara.os.entry
|
+-- dynamic_acl_authority
|
+-- /proc
    |
    +-- registry
    |   |
    |   +-- frame_io_authority
    |   |   |
    |   |   +-- boot
    |   |   |
    |   |   +-- console_view
    |   |   |
    |   |   +-- window_manager
    |   |
    |   +-- filesystem_authority
    |   |   |
    |   |   +-- ext4_mounts
    |   |   |
    |   |   +-- ext4_root_inodes
    |   |   |
    |   |   +-- ext4_dir_entries
    |   |
    |   +-- block_io_authority
    |   |   |
    |   |   +-- partition_io_authority
    |   |
    |   +-- security_authority
    |   |
    |   +-- shell_desktop
    |
    +-- chips
        |
        +-- elara
        |
        +-- io
```

## Registry Nodes

```text
/proc
|
+-- self        generated
+-- mounts      generated
+-- fs          generated
+-- meminfo     generated
+-- cpuinfo     generated
+-- uptime      generated
+-- registry    RegistryStatusRecord
+-- auths       authority records
+-- chips       chipset records
```

Authority records are typed values. Each record stores:

```text
authority_id
parent_authority_id
node_id
registered
generation
flags
```

## Dynamic ACL

`elara.os.entry` is the root trust anchor. Its `dynamic_acl_authority` worker is
the only current ACL-admin worker:

```text
set_worker_privilege(dynamic_acl_authority, 100)
```

An authority registers by sending:

```text
DynamicACLRequest {
  opcode  = dynamic_acl_opcode_register()
  route_id = hash_u32("authority-node")
  flags   = hash_u32("parent-node")
}
```

The Dynamic ACL Authority forwards that registration into Registry, then marks
the authority as registered in its internal state.

## Current Route Grants

Dynamic grants are allowed only when both endpoints are registered and the route
matches an ancestor/descendant relationship in the tree.

```text
hash_u32("frameio/boot")
  elara.os.boot -> elara.os.frame_io.publish_boot_frame

hash_u32("frameio/console")
  elara.os.console_view -> elara.os.frame_io.manager_ingress

hash_u32("frameio/window")
  elara.os.window_manager -> elara.os.frame_io.manager_ingress

hash_u32("registry/fs")
  elara.os.filesystem -> elara.os.registry.registry_ingress

hash_u32("registry/blockio")
  elara.os.block_io -> elara.os.registry.registry_ingress
```

The Dynamic ACL Authority also keeps an internal hashmap of route state:

```text
route_hash -> asked
route_hash -> granted
```

## Filesystem Boot State

The boot bridge currently mounts the root GPT partition by sending fixed-width
ext4 metadata to `elara.os.filesystem.register_mount`. The Filesystem Authority
stores:

```text
fs_mounts
  mount_id -> drive_id, fs_kind, flags, mount path hash

ext4_mounts
  mount_id -> superblock geometry and feature flags

ext4_root_inodes
  mount_id -> inode 2 mode, size, block count, extent header

ext4_dir_entries
  parent inode + name hash -> inode number, ext4 file type
```

At the current milestone, the host reads the ext4 superblock, group descriptor
0, inode 2, and the first root directory extent. EPA receives the root mount,
root inode, and streamed root directory entries. This is enough to prove the
root filesystem is mounted and ready for the next step: walking an on-disk path
to an EPA kernel image.

## Update Checklist

When adding or moving an authority:

```text
1. Add or update the authority id in dynamic_acl_protocol.em.
2. Add or update the RegistryAuthorityRecord in registry_authority.e.
3. Register the authority on first ingress or launch worker.
4. Add Dynamic ACL route ids for any allowed parent/child route.
5. Update this map.
```
