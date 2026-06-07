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
  hashmap_u64_node_init(root_id);
  return root_id;
}

function int hashmap_u64_child_get(int node_id, int key_byte) {
  local byte[1028] node;
  node = hashmap_u64_nodes[node_id:node_id + 1];
  return local_load_i32(node, hashmap_u64_node_child_off(key_byte));
}

function int hashmap_u64_child_set(int node_id, int key_byte, int child_id) {
  local byte[1028] node;
  node = hashmap_u64_nodes[node_id:node_id + 1];
  local_store_i32(node, hashmap_u64_node_child_off(key_byte), child_id);
  hashmap_u64_nodes[node_id] = node;
  return child_id;
}

function int hashmap_u64_value_get(int node_id) {
  local byte[1028] node;
  node = hashmap_u64_nodes[node_id:node_id + 1];
  return local_load_i32(node, hashmap_u64_node_value_off());
}

function int hashmap_u64_value_set(int node_id, int value_id) {
  local byte[1028] node;
  node = hashmap_u64_nodes[node_id:node_id + 1];
  local_store_i32(node, hashmap_u64_node_value_off(), value_id);
  hashmap_u64_nodes[node_id] = node;
  return value_id;
}

function int hashmap_u64_find(int root_id, HashMapKey64 key) {
  int depth = 0;
  int node_id = root_id;

  while (depth < 8) {
    int key_byte = hashmap_key64_get_byte(key, depth);
    int child_id = hashmap_u64_child_get(node_id, key_byte);
    if (child_id == hashmap_empty_id()) {
      return hashmap_empty_id();
    }
    node_id = child_id;
    depth = depth + 1;
  }

  return hashmap_u64_value_get(node_id);
}

function int hashmap_u64_contains(int root_id, HashMapKey64 key) {
  if (hashmap_u64_find(root_id, key) == hashmap_empty_id()) {
    return 0;
  }
  return 1;
}

function int hashmap_u64_put(int root_id, HashMapKey64 key, int value_id) {
  int depth = 0;
  int node_id = root_id;

  while (depth < 8) {
    int key_byte = hashmap_key64_get_byte(key, depth);
    int child_id = hashmap_u64_child_get(node_id, key_byte);
    if (child_id == hashmap_empty_id()) {
      child_id = dyn_alloc(hashmap_u64_nodes);
      hashmap_u64_node_init(child_id);
      hashmap_u64_child_set(node_id, key_byte, child_id);
    }
    node_id = child_id;
    depth = depth + 1;
  }

  hashmap_u64_value_set(node_id, value_id);
  return value_id;
}

function int hashmap_u64_remove(int root_id, HashMapKey64 key) {
  int depth = 0;
  int node_id = root_id;

  while (depth < 8) {
    int key_byte = hashmap_key64_get_byte(key, depth);
    int child_id = hashmap_u64_child_get(node_id, key_byte);
    if (child_id == hashmap_empty_id()) {
      return 0;
    }
    node_id = child_id;
    depth = depth + 1;
  }

  if (hashmap_u64_value_get(node_id) == hashmap_empty_id()) {
    return 0;
  }

  hashmap_u64_value_set(node_id, hashmap_empty_id());
  return 1;
}
