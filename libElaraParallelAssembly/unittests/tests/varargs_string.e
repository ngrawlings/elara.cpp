#include "common/egress.em"

type VarargStringIngress(int value) {
  return value;
}

function int local_size(int off, int len, ...) {
  int argc = vararg_count();
  int first = vararg_i32(0);
  return off + len + argc + first;
}

kernel(VM vm) {
  kernalId("test.varargs.string");
  start_worker(main);
}

worker main(VarargStringIngress msg) {
  int a = local_size(0, 4, 3, 5, 7);
  int b = fmt_len(0, 8, 11, 22);
}
