#include "common/hashmap.em"

type HashMapPrimitiveIngress(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("test.hashmap.primitives");
  start_worker(main);
}

worker main(HashMapPrimitiveIngress msg) {
  local HashMap64 map;
  map.root_id = hashmap_u64_init();
  map.count = 2;
  local byte[4096] blob;

  int payload_a = hashmap_bytes_alloc();
  int payload_b = hashmap_bytes_alloc();
  int zero_a = hashmap_bytes_zero(payload_a);
  int zero_b = hashmap_bytes_zero(payload_b);
  int write_a0 = hashmap_bytes_store_i32(payload_a, 0, 100);
  int write_a1 = hashmap_bytes_store_i32(payload_a, 4, 101);
  int write_b0 = hashmap_bytes_store_i32(payload_b, 0, 200);

  local HashMapKey64 key_a;
  key_a.b00 = 1;  key_a.b01 = 2;  key_a.b02 = 3;  key_a.b03 = 4;
  key_a.b04 = 5;  key_a.b05 = 6;  key_a.b06 = 7;  key_a.b07 = 8;

  local HashMapKey64 key_b;
  key_b.b00 = 8; key_b.b01 = 7; key_b.b02 = 6; key_b.b03 = 5;
  key_b.b04 = 4; key_b.b05 = 3; key_b.b06 = 2; key_b.b07 = 1;

  int put_a = hashmap_u64_put(map.root_id, key_a, payload_a);
  int put_b = hashmap_u64_put(map.root_id, key_b, payload_b);
  int has_a = hashmap_u64_contains(map.root_id, key_a);
  int has_b = hashmap_u64_contains(map.root_id, key_b);
  int found_a = hashmap_u64_find(map.root_id, key_a);
  int found_b = hashmap_u64_find(map.root_id, key_b);
  int read_a0 = hashmap_bytes_load_i32(found_a, 0);
  int read_a1 = hashmap_bytes_load_i32(found_a, 4);
  int read_b0 = hashmap_bytes_load_i32(found_b, 0);
  int blob_need = hashmap_u64_serialized_size();
  int blob_written = hashmap_u64_serialize(map.root_id, map.count, blob, 0);
  int removed_b = hashmap_u64_remove(map.root_id, key_b);
  int has_b_after = hashmap_u64_contains(map.root_id, key_b);
  int blob_restored = hashmap_u64_restore(blob, 0);
  map.root_id = hashmap_u64_restore_root_id(blob, 0);
  map.count = hashmap_u64_restore_count(blob, 0);
  int restored_has_a = hashmap_u64_contains(map.root_id, key_a);
  int restored_has_b = hashmap_u64_contains(map.root_id, key_b);
  int restored_found_a = hashmap_u64_find(map.root_id, key_a);
  int restored_found_b = hashmap_u64_find(map.root_id, key_b);
  int restored_read_a0 = hashmap_bytes_load_i32(restored_found_a, 0);
  int restored_read_b0 = hashmap_bytes_load_i32(restored_found_b, 0);
  int freed_b = hashmap_bytes_free(payload_b);
}
