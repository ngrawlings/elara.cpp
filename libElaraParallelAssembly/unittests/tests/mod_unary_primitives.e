#include "common/bytes.em"

type ModUnaryIngress(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("test.mod.unary.primitives");
  start_worker(main);
}

worker main(ModUnaryIngress msg) {
  int pos_mod = 7 % 3;
  int neg_left = -7 % 3;
  int neg_right = 7 % -3;
  int neg_both = -7 % -3;
  int mod_zero = 7 % 0;
  int unary_plus = +msg.value;
  int unary_minus = -msg.value;
  int unary_bitnot = ~msg.value;
  int nested = -(+(-5 % 3));
}
