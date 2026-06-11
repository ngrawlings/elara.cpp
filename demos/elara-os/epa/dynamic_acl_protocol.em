#include "common/hash.em"

type DynamicACLRequest(int opcode, int route_id, int flags, int reserved) {
  return opcode;
}

function int dynamic_acl_opcode_grant() {
  return 1;
}

function int dynamic_acl_opcode_revoke() {
  return 2;
}

function int dynamic_acl_opcode_revoke_all() {
  return 3;
}

function int dynamic_acl_opcode_register() {
  return 10;
}

function int dynamic_acl_permission_asked() {
  return 1;
}

function int dynamic_acl_permission_granted() {
  return 2;
}

function int dynamic_acl_authority_frame_io() {
  return hash_u32("frameio");
}

function int dynamic_acl_authority_block_io() {
  return hash_u32("blockio");
}

function int dynamic_acl_authority_filesystem() {
  return hash_u32("fs");
}

function int dynamic_acl_authority_security() {
  return hash_u32("security");
}

function int dynamic_acl_authority_shell() {
  return hash_u32("shell");
}

function int dynamic_acl_authority_registry() {
  return hash_u32("registry");
}

function int dynamic_acl_authority_entry() {
  return hash_u32("entry");
}

function int dynamic_acl_authority_dynamic_acl() {
  return hash_u32("dynacl");
}

function int dynamic_acl_authority_boot() {
  return hash_u32("boot");
}

function int dynamic_acl_authority_console() {
  return hash_u32("console");
}

function int dynamic_acl_authority_window() {
  return hash_u32("window");
}

function int dynamic_acl_authority_stream_io() {
  return hash_u32("streamio");
}

function int dynamic_acl_authority_partition_io() {
  return hash_u32("partitionio");
}

function int dynamic_acl_route_frame_io_boot() {
  return hash_u32("frameio/boot");
}

function int dynamic_acl_route_frame_io_console() {
  return hash_u32("frameio/console");
}

function int dynamic_acl_route_frame_io_window() {
  return hash_u32("frameio/window");
}

function int dynamic_acl_route_registry_fs() {
  return hash_u32("registry/fs");
}

function int dynamic_acl_route_registry_blockio() {
  return hash_u32("registry/blockio");
}

function int dynamic_acl_route_registry_streamio() {
  return hash_u32("registry/streamio");
}
