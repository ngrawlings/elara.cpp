#include "registry_protocol.em"
#include "common/hashmap.em"

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

type RegistryAuthorityRecord(
  int authority_id,
  int parent_authority_id,
  int node_id,
  int registered,
  int generation,
  int flags
) {
  return authority_id;
}

dynamic registry_status_records(RegistryStatusRecord, 4, 32, 4);
dynamic registry_authority_records(RegistryAuthorityRecord, 16, 64, 8);

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
  return hash_u32("frame");
}

function int registry_authority_block_io() {
  return hash_u32("blockio");
}

function int registry_authority_filesystem() {
  return hash_u32("fs");
}

function int registry_authority_security() {
  return hash_u32("security");
}

function int registry_authority_shell() {
  return hash_u32("shell");
}

function int registry_authority_registry() {
  return hash_u32("registry");
}

function int registry_authority_entry() {
  return hash_u32("entry");
}

function int registry_authority_dynamic_acl() {
  return hash_u32("dynacl");
}

function int registry_authority_boot() {
  return hash_u32("boot");
}

function int registry_authority_console() {
  return hash_u32("console");
}

function int registry_authority_window() {
  return hash_u32("window");
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

function int reg_authority_record_new(int authority_id, int parent_id, int node_id, int registered, int generation, int flags) {
  int record_id = dyn_alloc(registry_authority_records);
  local RegistryAuthorityRecord record;
  record.authority_id = authority_id;
  record.parent_authority_id = parent_id;
  record.node_id = node_id;
  record.registered = registered;
  record.generation = generation;
  record.flags = flags;
  registry_authority_records[record_id] = record;
  return record_id;
}

function int reg_authority_record_mark(int record_id, int registered, int generation, int flags) {
  local RegistryAuthorityRecord record;
  record = registry_authority_records[record_id:record_id + 1];
  record.registered = registered;
  record.generation = generation;
  record.flags = flags;
  registry_authority_records[record_id] = record;
  return record_id;
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
  "elara.os.entry" -> registry_ingress;
}

worker registry_ingress(RegistryRequest request) {
  static int initialized;
  static int root_id;
  static int proc_id;
  static int authorities_id;
  static int chipsets_id;
  static int status_id;
  static int generation;
  static int entry_record_id;
  static int dynamic_acl_record_id;
  static int registry_record_id;
  static int frame_record_id;
  static int boot_record_id;
  static int console_record_id;
  static int window_record_id;
  static int filesystem_record_id;
  static int block_io_record_id;
  static int security_record_id;
  static int shell_record_id;

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
    hashmap_u32_put(root_id, registry_path_proc(), reg_value_new(hashmap_value_kind_map_ref(), registry_type_directory(), proc_id, 0, 0));

    // /proc/self
    hashmap_u32_put(proc_id, registry_path_self(), reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_self(), 0, 0));

    // /proc/mounts
    hashmap_u32_put(proc_id, registry_path_mounts(), reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_mounts(), 0, 0));

    // /proc/filesystems
    hashmap_u32_put(proc_id, registry_path_fs(), reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_filesystems(), 0, 0));

    // /proc/meminfo
    hashmap_u32_put(proc_id, registry_path_meminfo(), reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_meminfo(), 0, 0));

    // /proc/cpuinfo
    hashmap_u32_put(proc_id, registry_path_cpuinfo(), reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_cpuinfo(), 0, 0));

    // /proc/uptime
    hashmap_u32_put(proc_id, registry_path_uptime(), reg_value_new(hashmap_value_kind_generated_ref(), registry_type_generated_file(), registry_generated_uptime(), 0, 0));

    // /proc/registry
    hashmap_u32_put(proc_id, registry_path_registry(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_status_record(), status_id, 24, 0));

    // /proc/authorities
    hashmap_u32_put(proc_id, registry_path_auths(), reg_value_new(hashmap_value_kind_map_ref(), registry_type_directory(), authorities_id, 0, 0));

    // /proc/chipsets
    hashmap_u32_put(proc_id, registry_path_chips(), reg_value_new(hashmap_value_kind_map_ref(), registry_type_directory(), chipsets_id, 0, 0));

    entry_record_id = reg_authority_record_new(registry_authority_entry(), 0, root_id, 0, generation, 0);
    dynamic_acl_record_id = reg_authority_record_new(registry_authority_dynamic_acl(), registry_authority_entry(), root_id, 0, generation, 0);
    registry_record_id = reg_authority_record_new(registry_authority_registry(), registry_authority_entry(), authorities_id, 1, generation, 0);
    frame_record_id = reg_authority_record_new(registry_authority_frame(), registry_authority_registry(), authorities_id, 0, generation, 0);
    boot_record_id = reg_authority_record_new(registry_authority_boot(), registry_authority_frame(), authorities_id, 0, generation, 0);
    console_record_id = reg_authority_record_new(registry_authority_console(), registry_authority_frame(), authorities_id, 0, generation, 0);
    window_record_id = reg_authority_record_new(registry_authority_window(), registry_authority_frame(), authorities_id, 0, generation, 0);
    filesystem_record_id = reg_authority_record_new(registry_authority_filesystem(), registry_authority_registry(), authorities_id, 0, generation, 0);
    block_io_record_id = reg_authority_record_new(registry_authority_block_io(), registry_authority_registry(), authorities_id, 0, generation, 0);
    security_record_id = reg_authority_record_new(registry_authority_security(), registry_authority_registry(), authorities_id, 0, generation, 0);
    shell_record_id = reg_authority_record_new(registry_authority_shell(), registry_authority_registry(), authorities_id, 0, generation, 0);

    // /proc/authorities/entry
    hashmap_u32_put(authorities_id, registry_authority_entry(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_authority(), entry_record_id, 24, 0));

    // /proc/authorities/dynacl
    hashmap_u32_put(authorities_id, registry_authority_dynamic_acl(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_authority(), dynamic_acl_record_id, 24, 0));

    // /proc/authorities/frame
    hashmap_u32_put(authorities_id, registry_authority_frame(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_authority(), frame_record_id, 24, 0));

    // /proc/authorities/blockio
    hashmap_u32_put(authorities_id, registry_authority_block_io(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_authority(), block_io_record_id, 24, 0));

    // /proc/authorities/fs
    hashmap_u32_put(authorities_id, registry_authority_filesystem(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_authority(), filesystem_record_id, 24, 0));

    // /proc/authorities/security
    hashmap_u32_put(authorities_id, registry_authority_security(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_authority(), security_record_id, 24, 0));

    // /proc/authorities/shell
    hashmap_u32_put(authorities_id, registry_authority_shell(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_authority(), shell_record_id, 24, 0));

    // /proc/authorities/registry
    hashmap_u32_put(authorities_id, registry_authority_registry(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_authority(), registry_record_id, 24, 0));

    // /proc/authorities/boot
    hashmap_u32_put(authorities_id, registry_authority_boot(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_authority(), boot_record_id, 24, 0));

    // /proc/authorities/console
    hashmap_u32_put(authorities_id, registry_authority_console(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_authority(), console_record_id, 24, 0));

    // /proc/authorities/window
    hashmap_u32_put(authorities_id, registry_authority_window(), reg_value_new(hashmap_value_kind_typed_ref(), registry_type_authority(), window_record_id, 24, 0));

    // /proc/chipsets/elara
    hashmap_u32_put(chipsets_id, hash_u32("elara"), reg_value_new_i32(registry_type_chipset(), registry_chipset_elara_silicon(), 0));

    // /proc/chipsets/io
    hashmap_u32_put(chipsets_id, hash_u32("io"), reg_value_new_i32(registry_type_chipset(), registry_chipset_io(), 0));

    initialized = 1;
  }

  if (request.opcode == registry_request_status()) {
    host_signal();
  }

  if (request.opcode == registry_request_touch_generation()) {
    generation = generation + 1;
    status = registry_status_records[status_id:status_id + 1];
    status.generation = generation;
    registry_status_records[status_id] = status;
    host_signal();
  }

  if (request.opcode == registry_request_register_authority()) {
    generation = generation + 1;
    if (request.path_hash_lo == registry_authority_entry()) {
      reg_authority_record_mark(entry_record_id, 1, generation, request.flags);
    }
    if (request.path_hash_lo == registry_authority_dynamic_acl()) {
      reg_authority_record_mark(dynamic_acl_record_id, 1, generation, request.flags);
    }
    if (request.path_hash_lo == registry_authority_registry()) {
      reg_authority_record_mark(registry_record_id, 1, generation, request.flags);
    }
    if (request.path_hash_lo == registry_authority_frame()) {
      reg_authority_record_mark(frame_record_id, 1, generation, request.flags);
    }
    if (request.path_hash_lo == registry_authority_boot()) {
      reg_authority_record_mark(boot_record_id, 1, generation, request.flags);
    }
    if (request.path_hash_lo == registry_authority_console()) {
      reg_authority_record_mark(console_record_id, 1, generation, request.flags);
    }
    if (request.path_hash_lo == registry_authority_window()) {
      reg_authority_record_mark(window_record_id, 1, generation, request.flags);
    }
    if (request.path_hash_lo == registry_authority_filesystem()) {
      reg_authority_record_mark(filesystem_record_id, 1, generation, request.flags);
    }
    if (request.path_hash_lo == registry_authority_block_io()) {
      reg_authority_record_mark(block_io_record_id, 1, generation, request.flags);
    }
    if (request.path_hash_lo == registry_authority_security()) {
      reg_authority_record_mark(security_record_id, 1, generation, request.flags);
    }
    if (request.path_hash_lo == registry_authority_shell()) {
      reg_authority_record_mark(shell_record_id, 1, generation, request.flags);
    }
    status = registry_status_records[status_id:status_id + 1];
    status.generation = generation;
    registry_status_records[status_id] = status;
    host_signal();
  }
}
