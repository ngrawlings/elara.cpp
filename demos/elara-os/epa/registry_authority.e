#include "common/hashmap.em"

type RegistryRequest(int opcode, int path_hash_lo, int path_hash_hi, int flags) {
  return opcode;
}

type RegistryStatusRecord(
  int root_id,
  int proc_id,
  int authorities_id,
  int chipsets_id,
  int initialized,
  int generation
) {
  return root_id;
}

dynamic registry_status_records(RegistryStatusRecord, 4, 32, 4);

function int registry_type_directory() {
  return 1;
}

function int registry_type_status_record() {
  return 2;
}

function int registry_type_authority() {
  return 3;
}

function int registry_type_generated_file() {
  return 4;
}

function int registry_type_chipset() {
  return 5;
}

function int registry_authority_frame() {
  return 1;
}

function int registry_authority_block_io() {
  return 2;
}

function int registry_authority_filesystem() {
  return 3;
}

function int registry_authority_security() {
  return 4;
}

function int registry_authority_shell() {
  return 5;
}

function int registry_authority_registry() {
  return 6;
}

function int registry_chipset_elara_silicon() {
  return 1;
}

function int registry_chipset_io() {
  return 2;
}

function int registry_generated_self() {
  return 1;
}

function int registry_generated_mounts() {
  return 2;
}

function int registry_generated_filesystems() {
  return 3;
}

function int registry_generated_meminfo() {
  return 4;
}

function int registry_generated_cpuinfo() {
  return 5;
}

function int registry_generated_uptime() {
  return 6;
}

function int reg_value_new(int kind, int type_id, int ref_id, int size, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;

  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }

  local_store_i32(value, 0, kind);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, size);
  local_store_i32(value, 16, ref_id);
  hashmap_bytes_values[value_id] = value;
  return value_id;
}

function int reg_value_new_i32(int type_id, int value_word, int flags) {
  int value_id = reg_value_new(hashmap_value_kind_inline_blob(), type_id, hashmap_empty_id(), 4, flags);
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  local_store_i32(value, 20, value_word);
  hashmap_bytes_values[value_id] = value;
  return value_id;
}

kernel(VM vm) {
  kernalId("elara.os.registry");
  start_worker(registry_ingress);
}

acl {
  "elara.os.boot" -> registry_ingress;
  "elara.os.filesystem" -> registry_ingress;
  "elara.os.block_io" -> registry_ingress;
  "elara.os.security" -> registry_ingress;
  "elara.os.shell" -> registry_ingress;
}

worker registry_ingress(RegistryRequest request) {
  static int initialized;
  static int root_id;
  static int proc_id;
  static int authorities_id;
  static int chipsets_id;
  static int status_id;
  static int generation;

  local RegistryStatusRecord status;

  if (initialized == 0) {
    root_id = hashmap_u64_init();
    proc_id = hashmap_u64_init();
    authorities_id = hashmap_u64_init();
    chipsets_id = hashmap_u64_init();
    status_id = dyn_alloc(registry_status_records);
    generation = 1;

    status.root_id = root_id;
    status.proc_id = proc_id;
    status.authorities_id = authorities_id;
    status.chipsets_id = chipsets_id;
    status.initialized = 1;
    status.generation = generation;
    registry_status_records[status_id] = status;

    // /proc
    hm_u64_put8(root_id, 112, 114, 111, 99, 0, 0, 0, 0, reg_value_new(hashmap_value_kind_map_ref(), registry_type_directory(), proc_id, 0, 0));

    // /proc/self
    hm_u64_put8(proc_id, 115, 101, 108, 102, 0, 0, 0, 0, reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_self(), 0, 0));

    // /proc/mounts
    hm_u64_put8(proc_id, 109, 111, 117, 110, 116, 115, 0, 0, reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_mounts(), 0, 0));

    // /proc/filesystems
    hm_u64_put8(proc_id, 102, 115, 0, 0, 0, 0, 0, 0, reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_filesystems(), 0, 0));

    // /proc/meminfo
    hm_u64_put8(proc_id, 109, 101, 109, 105, 110, 102, 111, 0, reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_meminfo(), 0, 0));

    // /proc/cpuinfo
    hm_u64_put8(proc_id, 99, 112, 117, 105, 110, 102, 111, 0, reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_cpuinfo(), 0, 0));

    // /proc/uptime
    hm_u64_put8(proc_id, 117, 112, 116, 105, 109, 101, 0, 0, reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_uptime(), 0, 0));

    // /proc/registry
    hm_u64_put8(proc_id, 114, 101, 103, 105, 115, 116, 114, 121, reg_value_new(hashmap_value_kind_typed_ref(), registry_type_status_record(), status_id, 24, 0));

    // /proc/authorities
    hm_u64_put8(proc_id, 97, 117, 116, 104, 115, 0, 0, 0, reg_value_new(hashmap_value_kind_map_ref(), registry_type_directory(), authorities_id, 0, 0));

    // /proc/chipsets
    hm_u64_put8(proc_id, 99, 104, 105, 112, 115, 0, 0, 0, reg_value_new(hashmap_value_kind_map_ref(), registry_type_directory(), chipsets_id, 0, 0));

    // /proc/authorities/frame
    hm_u64_put8(authorities_id, 102, 114, 97, 109, 101, 0, 0, 0, reg_value_new(hashmap_value_kind_authority_ref(), registry_type_authority(), registry_authority_frame(), 0, 0));

    // /proc/authorities/blockio
    hm_u64_put8(authorities_id, 98, 108, 111, 99, 107, 105, 111, 0, reg_value_new(hashmap_value_kind_authority_ref(), registry_type_authority(), registry_authority_block_io(), 0, 0));

    // /proc/authorities/fs
    hm_u64_put8(authorities_id, 102, 115, 0, 0, 0, 0, 0, 0, reg_value_new(hashmap_value_kind_authority_ref(), registry_type_authority(), registry_authority_filesystem(), 0, 0));

    // /proc/authorities/security
    hm_u64_put8(authorities_id, 115, 101, 99, 117, 114, 105, 116, 121, reg_value_new(hashmap_value_kind_authority_ref(), registry_type_authority(), registry_authority_security(), 0, 0));

    // /proc/authorities/shell
    hm_u64_put8(authorities_id, 115, 104, 101, 108, 108, 0, 0, 0, reg_value_new(hashmap_value_kind_authority_ref(), registry_type_authority(), registry_authority_shell(), 0, 0));

    // /proc/authorities/registry
    hm_u64_put8(authorities_id, 114, 101, 103, 105, 115, 116, 114, 121, reg_value_new(hashmap_value_kind_authority_ref(), registry_type_authority(), registry_authority_registry(), 0, 0));

    // /proc/chipsets/elara
    hm_u64_put8(chipsets_id, 101, 108, 97, 114, 97, 0, 0, 0, reg_value_new_i32(registry_type_chipset(), registry_chipset_elara_silicon(), 0));

    // /proc/chipsets/io
    hm_u64_put8(chipsets_id, 105, 111, 0, 0, 0, 0, 0, 0, reg_value_new_i32(registry_type_chipset(), registry_chipset_io(), 0));

    initialized = 1;
  }

  if (request.opcode == 1) {
    host_signal();
  }

  if (request.opcode == 2) {
    generation = generation + 1;
    status = registry_status_records[status_id:status_id + 1];
    status.generation = generation;
    registry_status_records[status_id] = status;
    host_signal();
  }
}
