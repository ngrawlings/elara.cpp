#include "common/bytes.em"

type BytePrimitiveIngress(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("test.byte.primitives");
  start_worker(main);
}

worker main(BytePrimitiveIngress msg) {
  int stored = byte_store(0, 65);
  int loaded = byte_load(0);
  int filled = byte_fill(4, 4, 7);
  int copied = byte_copy(8, 4, 4, 4);
  int moved = byte_move(9, 3, 8, 3);
  int cmp = byte_compare(4, 4, 8, 4);
  int same = byte_equal(4, 4, 8, 4);
  int found = byte_find(4, 4, 7);
  int counted = byte_count(4, 4, 7);
  int and32 = bit_and_i32(15, 6);
  int or32 = bit_or_i32(8, 3);
  int xor32 = bit_xor_i32(10, 5);
  int not32 = bit_not_i32(0);
  int band = byte_and(240, 60);
  int bor = byte_or(240, 15);
  int bxor = byte_xor(170, 15);
  int bnot = byte_not(0);
  int rol = byte_rol(129, 1);
  int ror = byte_ror(3, 1);
  int wrote32 = u32_store_le(16, 1234);
  int read32 = u32_load_le(16);
  int zeroed = byte_zero(4, 4);
}
