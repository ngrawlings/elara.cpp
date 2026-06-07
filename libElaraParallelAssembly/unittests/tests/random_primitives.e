#include "common/random.em"

type RandomPrimitiveIngress(int seed) {
  return seed;
}

kernel(VM vm) {
  kernalId("test.random.primitives");
  start_worker(main);
}

worker main(RandomPrimitiveIngress msg) {
  int seed = prng_seed(msg.seed);
  int n1 = prng_next(seed);
  int n2 = prng_next(n1);
  int n3 = prng_next(n2);
  int b1 = prng_next_byte(seed);
  int b2 = prng_next_byte(n1);
  int r1 = prng_next_range(seed, 100);
  int r2 = prng_next_range(n1, 7);
}
