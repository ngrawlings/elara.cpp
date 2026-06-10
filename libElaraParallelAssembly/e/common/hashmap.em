#ifndef ELARA_COMMON_HASHMAP_EM
#define ELARA_COMMON_HASHMAP_EM

#include "common/bytes.em"

type HashMapBytesHandle(int value_id) {
  return value_id;
}

type HashMapKey64(
  int b00, int b01, int b02, int b03,
  int b04, int b05, int b06, int b07
) {
  return b00;
}

type HashMap64(int root_id, int count) {
  return root_id;
}

dynamic hashmap_bytes_values(byte[64], 32, 128, 32);
dynamic hashmap_u64_nodes(byte[1028], 32, 128, 32);

function int hashmap_empty_id() {
  return 0 - 1;
}

function int hashmap_bytes_value_size() {
  return 64;
}

function int hashmap_value_kind_empty() {
  return 0;
}

function int hashmap_value_kind_inline_blob() {
  return 1;
}

function int hashmap_value_kind_blob_ref() {
  return 2;
}

function int hashmap_value_kind_map_ref() {
  return 3;
}

function int hashmap_value_kind_typed_ref() {
  return 4;
}

function int hashmap_value_kind_authority_ref() {
  return 5;
}

function int hashmap_value_kind_generated_ref() {
  return 6;
}

function int hashmap_value_type_none() {
  return 0;
}

function int hashmap_value_kind_off() {
  return 0;
}

function int hashmap_value_type_off() {
  return 4;
}

function int hashmap_value_flags_off() {
  return 8;
}

function int hashmap_value_size_off() {
  return 12;
}

function int hashmap_value_ref_off() {
  return 16;
}

function int hashmap_value_payload_off() {
  return 20;
}

function int hashmap_value_payload_size() {
  return 44;
}

function int hashmap_u64_node_value_off() {
  return 1024;
}

function int hashmap_key64_get_byte(HashMapKey64 key, int depth) {
  if (depth == 0) { return bit_and_i32(key.b00, 255); }
  if (depth == 1) { return bit_and_i32(key.b01, 255); }
  if (depth == 2) { return bit_and_i32(key.b02, 255); }
  if (depth == 3) { return bit_and_i32(key.b03, 255); }
  if (depth == 4) { return bit_and_i32(key.b04, 255); }
  if (depth == 5) { return bit_and_i32(key.b05, 255); }
  if (depth == 6) { return bit_and_i32(key.b06, 255); }
  return bit_and_i32(key.b07, 255);
}

function int hashmap_bytes_alloc() {
  return dyn_alloc(hashmap_bytes_values);
}

function int hashmap_bytes_zero(int value_id) {
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];

  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }

  hashmap_bytes_values[value_id] = value;
  return value_id;
}

function int hashmap_bytes_store_i32(int value_id, int byte_offset, int value) {
  local byte[64] bytes;
  bytes = hashmap_bytes_values[value_id:value_id + 1];
  local_store_i32(bytes, byte_offset, value);
  hashmap_bytes_values[value_id] = bytes;
  return value;
}

function int hashmap_bytes_load_i32(int value_id, int byte_offset) {
  local byte[64] bytes;
  bytes = hashmap_bytes_values[value_id:value_id + 1];
  return local_load_i32(bytes, byte_offset);
}

function int hashmap_bytes_free(int value_id) {
  dyn_free(hashmap_bytes_values, value_id);
  return value_id;
}

function int hashmap_value_alloc() {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  hashmap_bytes_values[value_id] = value;
  return value_id;
}

function int hashmap_value_init(
  int value_id,
  int kind,
  int type_id,
  int ref_id,
  int size,
  int flags
) {
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

function int hashmap_value_new(
  int kind,
  int type_id,
  int ref_id,
  int size,
  int flags
) {
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

function int hashmap_value_kind(int value_id) {
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  return local_load_i32(value, 0);
}

function int hashmap_value_type_id(int value_id) {
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  return local_load_i32(value, 4);
}

function int hashmap_value_flags(int value_id) {
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  return local_load_i32(value, 8);
}

function int hashmap_value_size(int value_id) {
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  return local_load_i32(value, 12);
}

function int hashmap_value_ref_id(int value_id) {
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  return local_load_i32(value, 16);
}

function int hashmap_value_set_kind(int value_id, int kind) {
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  local_store_i32(value, 0, kind);
  hashmap_bytes_values[value_id] = value;
  return kind;
}

function int hashmap_value_set_type_id(int value_id, int type_id) {
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  local_store_i32(value, 4, type_id);
  hashmap_bytes_values[value_id] = value;
  return type_id;
}

function int hashmap_value_set_flags(int value_id, int flags) {
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  local_store_i32(value, 8, flags);
  hashmap_bytes_values[value_id] = value;
  return flags;
}

function int hashmap_value_set_size(int value_id, int size) {
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  local_store_i32(value, 12, size);
  hashmap_bytes_values[value_id] = value;
  return size;
}

function int hashmap_value_set_ref_id(int value_id, int ref_id) {
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  local_store_i32(value, 16, ref_id);
  hashmap_bytes_values[value_id] = value;
  return ref_id;
}

function int hashmap_value_payload_store_i32(int value_id, int word_index, int value) {
  local byte[64] bytes;
  bytes = hashmap_bytes_values[value_id:value_id + 1];
  local_store_i32(bytes, 20 + (word_index * 4), value);
  hashmap_bytes_values[value_id] = bytes;
  return value;
}

function int hashmap_value_payload_load_i32(int value_id, int word_index) {
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  return local_load_i32(value, 20 + (word_index * 4));
}

function int hashmap_value_new_inline_i32(int type_id, int value, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] bytes;
  int slot = 0;
  bytes = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(bytes, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(bytes, 0, 1);
  local_store_i32(bytes, 4, type_id);
  local_store_i32(bytes, 8, flags);
  local_store_i32(bytes, 12, 4);
  local_store_i32(bytes, 16, hashmap_empty_id());
  local_store_i32(bytes, 20, value);
  hashmap_bytes_values[value_id] = bytes;
  return value_id;
}

function int hashmap_value_new_inline_i32_pair(int type_id, int left, int right, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] bytes;
  int slot = 0;
  bytes = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(bytes, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(bytes, 0, 1);
  local_store_i32(bytes, 4, type_id);
  local_store_i32(bytes, 8, flags);
  local_store_i32(bytes, 12, 8);
  local_store_i32(bytes, 16, hashmap_empty_id());
  local_store_i32(bytes, 20, left);
  local_store_i32(bytes, 24, right);
  hashmap_bytes_values[value_id] = bytes;
  return value_id;
}

function int hashmap_value_new_map(int map_root_id, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 3);
  local_store_i32(value, 4, 0);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, 0);
  local_store_i32(value, 16, map_root_id);
  hashmap_bytes_values[value_id] = value;
  return value_id;
}

function int hashmap_value_new_typed_ref(int type_id, int ref_id, int size, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 4);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, size);
  local_store_i32(value, 16, ref_id);
  hashmap_bytes_values[value_id] = value;
  return value_id;
}

function int hashmap_value_new_blob_ref(int type_id, int ref_id, int size, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 2);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, size);
  local_store_i32(value, 16, ref_id);
  hashmap_bytes_values[value_id] = value;
  return value_id;
}

function int hashmap_value_new_authority_ref(int authority_id, int type_id, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 5);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, 0);
  local_store_i32(value, 16, authority_id);
  hashmap_bytes_values[value_id] = value;
  return value_id;
}

function int hashmap_value_new_generated_ref(int generator_id, int type_id, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 6);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, 0);
  local_store_i32(value, 16, generator_id);
  hashmap_bytes_values[value_id] = value;
  return value_id;
}

function int hashmap_value_is_map(int value_id) {
  if (value_id == hashmap_empty_id()) {
    return 0;
  }
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  if (local_load_i32(value, 0) == 3) {
    return 1;
  }
  return 0;
}

function int hashmap_value_as_map_root(int value_id) {
  if (value_id == hashmap_empty_id()) {
    return hashmap_empty_id();
  }
  local byte[64] value;
  value = hashmap_bytes_values[value_id:value_id + 1];
  if (local_load_i32(value, 0) != 3) {
    return hashmap_empty_id();
  }
  return local_load_i32(value, 16);
}

function int hashmap_u64_node_child_off(int key_byte) {
  return key_byte * 4;
}

function int hashmap_u64_node_init(int node_id) {
  local byte[1028] node;
  int slot = 0;
  node = hashmap_u64_nodes[node_id:node_id + 1];

  while (slot < 257) {
    local_store_i32(node, slot * 4, hashmap_empty_id());
    slot = slot + 1;
  }

  hashmap_u64_nodes[node_id] = node;
  return node_id;
}

function int hashmap_u64_init() {
  int root_id = dyn_alloc(hashmap_u64_nodes);
  local byte[1028] node;
  int slot = 0;
  node = hashmap_u64_nodes[root_id:root_id + 1];

  while (slot < 257) {
    local_store_i32(node, slot * 4, hashmap_empty_id());
    slot = slot + 1;
  }

  hashmap_u64_nodes[root_id] = node;
  return root_id;
}

function int hashmap_u64_child_get(int node_id, int key_byte) {
  local byte[1028] node;
  node = hashmap_u64_nodes[node_id:node_id + 1];
  return local_load_i32(node, key_byte * 4);
}

function int hashmap_u64_child_set(int node_id, int key_byte, int child_id) {
  local byte[1028] node;
  node = hashmap_u64_nodes[node_id:node_id + 1];
  local_store_i32(node, key_byte * 4, child_id);
  hashmap_u64_nodes[node_id] = node;
  return child_id;
}

function int hashmap_u64_value_get(int node_id) {
  local byte[1028] node;
  node = hashmap_u64_nodes[node_id:node_id + 1];
  return local_load_i32(node, 1024);
}

function int hashmap_u64_value_set(int node_id, int value_id) {
  local byte[1028] node;
  node = hashmap_u64_nodes[node_id:node_id + 1];
  local_store_i32(node, 1024, value_id);
  hashmap_u64_nodes[node_id] = node;
  return value_id;
}

function int hashmap_u64_find(int root_id, HashMapKey64 key) {
  int depth = 0;
  int node_id = root_id;

  while (depth < 8) {
    int key_byte = 0;
    if (depth == 0) { key_byte = bit_and_i32(key.b00, 255); }
    if (depth == 1) { key_byte = bit_and_i32(key.b01, 255); }
    if (depth == 2) { key_byte = bit_and_i32(key.b02, 255); }
    if (depth == 3) { key_byte = bit_and_i32(key.b03, 255); }
    if (depth == 4) { key_byte = bit_and_i32(key.b04, 255); }
    if (depth == 5) { key_byte = bit_and_i32(key.b05, 255); }
    if (depth == 6) { key_byte = bit_and_i32(key.b06, 255); }
    if (depth == 7) { key_byte = bit_and_i32(key.b07, 255); }
    local byte[1028] node;
    int child_id = 0;
    node = hashmap_u64_nodes[node_id:node_id + 1];
    child_id = local_load_i32(node, key_byte * 4);
    if (child_id == hashmap_empty_id()) {
      return hashmap_empty_id();
    }
    node_id = child_id;
    depth = depth + 1;
  }

  local byte[1028] value_node;
  value_node = hashmap_u64_nodes[node_id:node_id + 1];
  return local_load_i32(value_node, 1024);
}

function int hashmap_u64_contains(int root_id, HashMapKey64 key) {
  int depth = 0;
  int node_id = root_id;

  while (depth < 8) {
    int key_byte = 0;
    if (depth == 0) { key_byte = bit_and_i32(key.b00, 255); }
    if (depth == 1) { key_byte = bit_and_i32(key.b01, 255); }
    if (depth == 2) { key_byte = bit_and_i32(key.b02, 255); }
    if (depth == 3) { key_byte = bit_and_i32(key.b03, 255); }
    if (depth == 4) { key_byte = bit_and_i32(key.b04, 255); }
    if (depth == 5) { key_byte = bit_and_i32(key.b05, 255); }
    if (depth == 6) { key_byte = bit_and_i32(key.b06, 255); }
    if (depth == 7) { key_byte = bit_and_i32(key.b07, 255); }
    local byte[1028] node;
    int child_id = 0;
    node = hashmap_u64_nodes[node_id:node_id + 1];
    child_id = local_load_i32(node, key_byte * 4);
    if (child_id == hashmap_empty_id()) {
      return 0;
    }
    node_id = child_id;
    depth = depth + 1;
  }

  local byte[1028] value_node;
  value_node = hashmap_u64_nodes[node_id:node_id + 1];
  if (local_load_i32(value_node, 1024) != hashmap_empty_id()) {
    return 1;
  }
  return 0;
}

function int hashmap_u64_put(int root_id, HashMapKey64 key, int value_id) {
  int depth = 0;
  int node_id = root_id;

  while (depth < 8) {
    int key_byte = 0;
    if (depth == 0) { key_byte = bit_and_i32(key.b00, 255); }
    if (depth == 1) { key_byte = bit_and_i32(key.b01, 255); }
    if (depth == 2) { key_byte = bit_and_i32(key.b02, 255); }
    if (depth == 3) { key_byte = bit_and_i32(key.b03, 255); }
    if (depth == 4) { key_byte = bit_and_i32(key.b04, 255); }
    if (depth == 5) { key_byte = bit_and_i32(key.b05, 255); }
    if (depth == 6) { key_byte = bit_and_i32(key.b06, 255); }
    if (depth == 7) { key_byte = bit_and_i32(key.b07, 255); }
    local byte[1028] parent_node;
    int child_id = 0;
    parent_node = hashmap_u64_nodes[node_id:node_id + 1];
    child_id = local_load_i32(parent_node, key_byte * 4);
    if (child_id == hashmap_empty_id()) {
      child_id = dyn_alloc(hashmap_u64_nodes);
      local byte[1028] child_node;
      int slot = 0;
      child_node = hashmap_u64_nodes[child_id:child_id + 1];
      while (slot < 257) {
        local_store_i32(child_node, slot * 4, hashmap_empty_id());
        slot = slot + 1;
      }
      hashmap_u64_nodes[child_id] = child_node;
      local_store_i32(parent_node, key_byte * 4, child_id);
      hashmap_u64_nodes[node_id] = parent_node;
    }
    node_id = child_id;
    depth = depth + 1;
  }

  local byte[1028] value_node;
  value_node = hashmap_u64_nodes[node_id:node_id + 1];
  local_store_i32(value_node, 1024, value_id);
  hashmap_u64_nodes[node_id] = value_node;
  return value_id;
}

function int hm_u64_put8(
  int root_id,
  int b00,
  int b01,
  int b02,
  int b03,
  int b04,
  int b05,
  int b06,
  int b07,
  int value_id
) {
  int depth = 0;
  int node_id = root_id;

  while (depth < 8) {
    int key_byte = 0;
    if (depth == 0) { key_byte = bit_and_i32(b00, 255); }
    if (depth == 1) { key_byte = bit_and_i32(b01, 255); }
    if (depth == 2) { key_byte = bit_and_i32(b02, 255); }
    if (depth == 3) { key_byte = bit_and_i32(b03, 255); }
    if (depth == 4) { key_byte = bit_and_i32(b04, 255); }
    if (depth == 5) { key_byte = bit_and_i32(b05, 255); }
    if (depth == 6) { key_byte = bit_and_i32(b06, 255); }
    if (depth == 7) { key_byte = bit_and_i32(b07, 255); }
    local byte[1028] parent_node;
    int child_id = 0;
    parent_node = hashmap_u64_nodes[node_id:node_id + 1];
    child_id = local_load_i32(parent_node, key_byte * 4);
    if (child_id == hashmap_empty_id()) {
      child_id = dyn_alloc(hashmap_u64_nodes);
      local byte[1028] child_node;
      int slot = 0;
      child_node = hashmap_u64_nodes[child_id:child_id + 1];
      while (slot < 257) {
        local_store_i32(child_node, slot * 4, hashmap_empty_id());
        slot = slot + 1;
      }
      hashmap_u64_nodes[child_id] = child_node;
      local_store_i32(parent_node, key_byte * 4, child_id);
      hashmap_u64_nodes[node_id] = parent_node;
    }
    node_id = child_id;
    depth = depth + 1;
  }

  local byte[1028] value_node;
  value_node = hashmap_u64_nodes[node_id:node_id + 1];
  local_store_i32(value_node, 1024, value_id);
  hashmap_u64_nodes[node_id] = value_node;
  return value_id;
}

function int hashmap_u32_put(int root_id, int node_hash, int value_id) {
  local byte[4] key_bytes;
  local_store_i32(key_bytes, 0, node_hash);
  return hm_u64_put8(
    root_id,
    local_load_u8(key_bytes, 0),
    local_load_u8(key_bytes, 1),
    local_load_u8(key_bytes, 2),
    local_load_u8(key_bytes, 3),
    0,
    0,
    0,
    0,
    value_id
  );
}

function int hashmap_u32_put_map(int root_id, int node_hash, int child_root_id, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 3);
  local_store_i32(value, 4, 0);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, 0);
  local_store_i32(value, 16, child_root_id);
  hashmap_bytes_values[value_id] = value;
  hashmap_u32_put(root_id, node_hash, value_id);
  return value_id;
}

function int hashmap_u32_put_typed_ref(int root_id, int node_hash, int type_id, int ref_id, int size, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 4);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, size);
  local_store_i32(value, 16, ref_id);
  hashmap_bytes_values[value_id] = value;
  hashmap_u32_put(root_id, node_hash, value_id);
  return value_id;
}

function int hashmap_u32_put_generated_ref(int root_id, int node_hash, int generator_id, int type_id, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 6);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, 0);
  local_store_i32(value, 16, generator_id);
  hashmap_bytes_values[value_id] = value;
  hashmap_u32_put(root_id, node_hash, value_id);
  return value_id;
}

function int hashmap_u32_put_inline_i32(int root_id, int node_hash, int type_id, int value_word, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 1);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, 4);
  local_store_i32(value, 16, hashmap_empty_id());
  local_store_i32(value, 20, value_word);
  hashmap_bytes_values[value_id] = value;
  hashmap_u32_put(root_id, node_hash, value_id);
  return value_id;
}

function int hashmap_u64_put_inline_i32(int root_id, HashMapKey64 key, int type_id, int value, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] bytes;
  int slot = 0;
  bytes = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(bytes, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(bytes, 0, 1);
  local_store_i32(bytes, 4, type_id);
  local_store_i32(bytes, 8, flags);
  local_store_i32(bytes, 12, 4);
  local_store_i32(bytes, 16, hashmap_empty_id());
  local_store_i32(bytes, 20, value);
  hashmap_bytes_values[value_id] = bytes;
  hashmap_u64_put(root_id, key, value_id);
  return value_id;
}

function int hashmap_u64_put_map(int root_id, HashMapKey64 key, int child_root_id, int flags) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 3);
  local_store_i32(value, 4, 0);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, 0);
  local_store_i32(value, 16, child_root_id);
  hashmap_bytes_values[value_id] = value;
  hashmap_u64_put(root_id, key, value_id);
  return value_id;
}

function int hashmap_u64_put_typed_ref(
  int root_id,
  HashMapKey64 key,
  int type_id,
  int ref_id,
  int size,
  int flags
) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 4);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, size);
  local_store_i32(value, 16, ref_id);
  hashmap_bytes_values[value_id] = value;
  hashmap_u64_put(root_id, key, value_id);
  return value_id;
}

function int hashmap_u64_put_blob_ref(
  int root_id,
  HashMapKey64 key,
  int type_id,
  int ref_id,
  int size,
  int flags
) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 2);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, size);
  local_store_i32(value, 16, ref_id);
  hashmap_bytes_values[value_id] = value;
  hashmap_u64_put(root_id, key, value_id);
  return value_id;
}

function int hashmap_u64_put_authority_ref(
  int root_id,
  HashMapKey64 key,
  int authority_id,
  int type_id,
  int flags
) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 5);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, 0);
  local_store_i32(value, 16, authority_id);
  hashmap_bytes_values[value_id] = value;
  hashmap_u64_put(root_id, key, value_id);
  return value_id;
}

function int hashmap_u64_put_generated_ref(
  int root_id,
  HashMapKey64 key,
  int generator_id,
  int type_id,
  int flags
) {
  int value_id = dyn_alloc(hashmap_bytes_values);
  local byte[64] value;
  int slot = 0;
  value = hashmap_bytes_values[value_id:value_id + 1];
  while (slot < 16) {
    local_store_i32(value, slot * 4, 0);
    slot = slot + 1;
  }
  local_store_i32(value, 0, 6);
  local_store_i32(value, 4, type_id);
  local_store_i32(value, 8, flags);
  local_store_i32(value, 12, 0);
  local_store_i32(value, 16, generator_id);
  hashmap_bytes_values[value_id] = value;
  hashmap_u64_put(root_id, key, value_id);
  return value_id;
}

function int hashmap_u64_remove(int root_id, HashMapKey64 key) {
  int depth = 0;
  int node_id = root_id;

  while (depth < 8) {
    int key_byte = 0;
    if (depth == 0) { key_byte = bit_and_i32(key.b00, 255); }
    if (depth == 1) { key_byte = bit_and_i32(key.b01, 255); }
    if (depth == 2) { key_byte = bit_and_i32(key.b02, 255); }
    if (depth == 3) { key_byte = bit_and_i32(key.b03, 255); }
    if (depth == 4) { key_byte = bit_and_i32(key.b04, 255); }
    if (depth == 5) { key_byte = bit_and_i32(key.b05, 255); }
    if (depth == 6) { key_byte = bit_and_i32(key.b06, 255); }
    if (depth == 7) { key_byte = bit_and_i32(key.b07, 255); }
    local byte[1028] node;
    int child_id = 0;
    node = hashmap_u64_nodes[node_id:node_id + 1];
    child_id = local_load_i32(node, key_byte * 4);
    if (child_id == hashmap_empty_id()) {
      return 0;
    }
    node_id = child_id;
    depth = depth + 1;
  }

  local byte[1028] value_node;
  value_node = hashmap_u64_nodes[node_id:node_id + 1];
  if (local_load_i32(value_node, 1024) == hashmap_empty_id()) {
    return 0;
  }

  local_store_i32(value_node, 1024, hashmap_empty_id());
  hashmap_u64_nodes[node_id] = value_node;
  return 1;
}

function int hashmap_u64_serialized_size() {
  return 20
       + dynamic_serialized_size(hashmap_bytes_values)
       + dynamic_serialized_size(hashmap_u64_nodes);
}

function int hashmap_u64_restore_root_id(byte[1] blob, int blob_offset) {
  return local_load_i32(blob, blob_offset + 4);
}

function int hashmap_u64_restore_count(byte[1] blob, int blob_offset) {
  return local_load_i32(blob, blob_offset + 8);
}

function int hashmap_u64_serialize(int root_id, int count, byte[1] blob, int blob_offset) {
  int values_bytes = 0;
  int nodes_bytes = 0;

  local_store_i32(blob, blob_offset + 0, 1);
  local_store_i32(blob, blob_offset + 4, root_id);
  local_store_i32(blob, blob_offset + 8, count);

  values_bytes = dynamic_serialize(hashmap_bytes_values, blob, blob_offset + 20);
  if (values_bytes == 0) {
    return 0;
  }

  local_store_i32(blob, blob_offset + 12, values_bytes);

  nodes_bytes = dynamic_serialize(hashmap_u64_nodes, blob, blob_offset + 20 + values_bytes);
  if (nodes_bytes == 0) {
    return 0;
  }

  local_store_i32(blob, blob_offset + 16, nodes_bytes);
  return 20 + values_bytes + nodes_bytes;
}

function int hashmap_u64_restore(byte[1] blob, int blob_offset) {
  int version = local_load_i32(blob, blob_offset + 0);
  int values_bytes = 0;
  int nodes_bytes = 0;

  if (version != 1) {
    return 0;
  }

  values_bytes = local_load_i32(blob, blob_offset + 12);
  nodes_bytes = local_load_i32(blob, blob_offset + 16);

  if (dynamic_restore(hashmap_bytes_values, blob, blob_offset + 20) == 0) {
    return 0;
  }

  if (dynamic_restore(hashmap_u64_nodes, blob, blob_offset + 20 + values_bytes) == 0) {
    return 0;
  }

  return 20 + values_bytes + nodes_bytes;
}

#endif
