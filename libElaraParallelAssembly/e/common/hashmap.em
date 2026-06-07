// E stdlib HashMap.
//
// First concrete real-world map: i32 -> i32.
//
// Current E dynamic pools are compile-time named pools, not first-class values.
// So this module exposes one process/kernel-local HashMapI32 store backed by a
// stdlib-owned dynamic pool. A future generic macro or first-class pool handle
// can layer multiple independently named maps over the same algorithms.

#include "common/bytes.em"

type HashMapI32Entry(int key, int value, int state, int chain) {
  return key;
}

type HashMapI32(int buckets_off, int capacity, int count) {
  return buckets_off;
}

dynamic hashmap_i32_entries(HashMapI32Entry, 32, 128, 32);

function int hashmap_i32_empty_id() {
  return 0 - 1;
}

function int hashmap_i32_required_bytes(int capacity) {
  return capacity * 4;
}

function int hashmap_i32_hash(int key) {
  int hash = 0;

  EPA {
    LOAD_LW 0
    PUSH 16
    ROR_BYTE
    PUSH 16777619
    MUL_I32
    LOAD_LW 0
    XOR_I32
    STORE_LW 1
  }

  hash = hash;
  return hash;
}

function int hashmap_i32_init(int buckets_off, int capacity) {
  byte_fill(buckets_off, capacity * 4, 255);
  return capacity;
}

function int hashmap_i32_bucket_off(int buckets_off, int bucket_index) {
  return buckets_off + (bucket_index * 4);
}

function int hashmap_i32_bucket_get(int buckets_off, int bucket_index) {
  int off = hashmap_i32_bucket_off(buckets_off, bucket_index);
  return u32_load_le(off);
}

function int hashmap_i32_bucket_set(int buckets_off, int bucket_index, int entry_id) {
  int off = hashmap_i32_bucket_off(buckets_off, bucket_index);
  u32_store_le(off, entry_id);
  return entry_id;
}

function int hashmap_i32_find_in_bucket(int bucket_head, int key) {
  int current = bucket_head;

  while (current != hashmap_i32_empty_id()) {
    HashMapI32Entry entry = hashmap_i32_entries[current];
    if (entry.state == 1) {
      if (entry.key == key) {
        return current;
      }
    }
    current = entry.chain;
  }

  return hashmap_i32_empty_id();
}

function int hashmap_i32_find(int buckets_off, int capacity, int key) {
  int hash = hashmap_i32_hash(key);
  int positive_hash = bit_and_i32(hash, 2147483647);
  int bucket = positive_hash - ((positive_hash / capacity) * capacity);
  int head = hashmap_i32_bucket_get(buckets_off, bucket);
  return hashmap_i32_find_in_bucket(head, key);
}

function int hashmap_i32_contains(int buckets_off, int capacity, int key) {
  int entry_id = hashmap_i32_find(buckets_off, capacity, key);
  if (entry_id == hashmap_i32_empty_id()) {
    return 0;
  }
  return 1;
}

function int hashmap_i32_get_or(int buckets_off, int capacity, int key, int fallback) {
  int entry_id = hashmap_i32_find(buckets_off, capacity, key);
  if (entry_id == hashmap_i32_empty_id()) {
    return fallback;
  }

  HashMapI32Entry entry = hashmap_i32_entries[entry_id];
  return entry.value;
}

function int hashmap_i32_put(int buckets_off, int capacity, int key, int value) {
  int hash = hashmap_i32_hash(key);
  int positive_hash = bit_and_i32(hash, 2147483647);
  int bucket = positive_hash - ((positive_hash / capacity) * capacity);
  int head = hashmap_i32_bucket_get(buckets_off, bucket);
  int existing = hashmap_i32_find_in_bucket(head, key);

  if (existing != hashmap_i32_empty_id()) {
    local HashMapI32Entry updated;
    HashMapI32Entry old = hashmap_i32_entries[existing];
    updated.key = key;
    updated.value = value;
    updated.state = 1;
    updated.chain = old.chain;
    hashmap_i32_entries[existing] = updated;
    return existing;
  }

  int entry_id = dyn_alloc(hashmap_i32_entries);
  local HashMapI32Entry entry;
  entry.key = key;
  entry.value = value;
  entry.state = 1;
  entry.chain = head;
  hashmap_i32_entries[entry_id] = entry;
  hashmap_i32_bucket_set(buckets_off, bucket, entry_id);
  return entry_id;
}

function int hashmap_i32_remove(int buckets_off, int capacity, int key) {
  int hash = hashmap_i32_hash(key);
  int positive_hash = bit_and_i32(hash, 2147483647);
  int bucket = positive_hash - ((positive_hash / capacity) * capacity);
  int current = hashmap_i32_bucket_get(buckets_off, bucket);
  int previous = hashmap_i32_empty_id();

  while (current != hashmap_i32_empty_id()) {
    HashMapI32Entry entry = hashmap_i32_entries[current];
    if (entry.state == 1) {
      if (entry.key == key) {
        if (previous == hashmap_i32_empty_id()) {
          hashmap_i32_bucket_set(buckets_off, bucket, entry.chain);
        } else {
          local HashMapI32Entry prev_entry;
          HashMapI32Entry old_prev = hashmap_i32_entries[previous];
          prev_entry.key = old_prev.key;
          prev_entry.value = old_prev.value;
          prev_entry.state = old_prev.state;
          prev_entry.chain = entry.chain;
          hashmap_i32_entries[previous] = prev_entry;
        }
        dyn_free(hashmap_i32_entries, current);
        return 1;
      }
    }
    previous = current;
    current = entry.chain;
  }

  return 0;
}

function int hashmap_i32_clear(int buckets_off, int capacity) {
  int bucket = 0;
  int removed = 0;

  while (bucket < capacity) {
    int current = hashmap_i32_bucket_get(buckets_off, bucket);
    while (current != hashmap_i32_empty_id()) {
      HashMapI32Entry entry = hashmap_i32_entries[current];
      int chain = entry.chain;
      dyn_free(hashmap_i32_entries, current);
      removed = removed + 1;
      current = chain;
    }
    hashmap_i32_bucket_set(buckets_off, bucket, hashmap_i32_empty_id());
    bucket = bucket + 1;
  }

  return removed;
}
