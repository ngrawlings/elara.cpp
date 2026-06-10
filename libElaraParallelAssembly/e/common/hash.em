#ifndef ELARA_COMMON_HASH_EM
#define ELARA_COMMON_HASH_EM

#include "common/bytes.em"

function int hash_u32_offset_basis() {
  return 2166136261;
}

function int hash_u32_prime() {
  return 16777619;
}

function int hash_u32_step(int hash, int byte_value) {
  int value = bit_and_i32(byte_value, 255);
  hash = bit_xor_i32(hash, value);
  hash = hash * hash_u32_prime();
  return hash;
}

function int hash_u32_bytes(int source_off, int source_len) {
  int hash = hash_u32_offset_basis();
  int index = 0;

  while (index < source_len) {
    hash = hash_u32_step(hash, byte_load(source_off + index));
    index = index + 1;
  }

  return hash;
}

function int hash_path_node_empty_u32() {
  return hash_u32("");
}

#endif
