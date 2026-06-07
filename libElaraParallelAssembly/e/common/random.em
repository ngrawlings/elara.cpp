#include "common/bytes.em"

// Small deterministic PRNG helpers for E.
//
// The generator is Park-Miller minimal standard:
//   state(n+1) = (state(n) * 48271) % 2147483647
//
// State must be in [1, 2147483646]. Zero is normalized to 1.

function int prng_seed(int value) {
  int seeded = value % 2147483647;
  if (seeded < 0) {
    seeded = 0 - seeded;
  }
  if (seeded == 0) {
    seeded = 1;
  }
  return seeded;
}

function int prng_next(int state) {
  int seeded = prng_seed(state);
  return (seeded * 48271) % 2147483647;
}

function int prng_next_byte(int state) {
  return prng_next(state) % 256;
}

function int prng_next_range(int state, int limit) {
  int value = prng_next(state);
  if (limit <= 0) {
    return 0;
  }
  return value % limit;
}
