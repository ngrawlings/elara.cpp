#include "common/hashmap.em"

type HashMapPrimitiveIngress(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("test.hashmap.primitives");
  start_worker(main);
}

worker main(HashMapPrimitiveIngress msg) {
  local HashMapI32 map;
  map.buckets_off = 0;
  map.capacity = 16;
  map.count = 0;

  int required = hashmap_i32_required_bytes(map.capacity);
  int initialized = hashmap_i32_init(map.buckets_off, map.capacity);
  int id_a = hashmap_i32_put(map.buckets_off, map.capacity, 10, 100);
  int id_b = hashmap_i32_put(map.buckets_off, map.capacity, 26, 260);
  int id_c = hashmap_i32_put(map.buckets_off, map.capacity, 10, 101);
  int has_a = hashmap_i32_contains(map.buckets_off, map.capacity, 10);
  int value_a = hashmap_i32_get_or(map.buckets_off, map.capacity, 10, 0);
  int missing = hashmap_i32_get_or(map.buckets_off, map.capacity, 99, 777);
  int removed = hashmap_i32_remove(map.buckets_off, map.capacity, 26);
  int has_b = hashmap_i32_contains(map.buckets_off, map.capacity, 26);
  int cleared = hashmap_i32_clear(map.buckets_off, map.capacity);
}
