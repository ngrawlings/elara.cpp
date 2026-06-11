#include "common/hash.em"

type RegistryRequest(int opcode, int path_hash_lo, int path_hash_hi, int flags) {
  return opcode;
}

type RegistryPartitionRegistration(
  int drive_id,
  int partition_drive_id,
  int first_lba,
  int last_lba,
  int fs_kind,
  int flags
) {
  return partition_drive_id;
}

type RegistryMountRegistration(
  int mount_id,
  int drive_id,
  int fs_kind,
  int flags,
  int mount_path_hash
) {
  return mount_id;
}

function int registry_request_status() {
  return 1;
}

function int registry_request_touch_generation() {
  return 2;
}

function int registry_request_register_authority() {
  return 10;
}

function int registry_path_proc() {
  return hash_u32("proc");
}

function int registry_path_self() {
  return hash_u32("self");
}

function int registry_path_mounts() {
  return hash_u32("mounts");
}

function int registry_path_fs() {
  return hash_u32("fs");
}

function int registry_path_meminfo() {
  return hash_u32("meminfo");
}

function int registry_path_cpuinfo() {
  return hash_u32("cpuinfo");
}

function int registry_path_uptime() {
  return hash_u32("uptime");
}

function int registry_path_registry() {
  return hash_u32("registry");
}

function int registry_path_auths() {
  return hash_u32("auths");
}

function int registry_path_chips() {
  return hash_u32("chips");
}
