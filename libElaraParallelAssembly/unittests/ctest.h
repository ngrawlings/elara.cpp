// unittests/ctest.h
#pragma once

#include <stdio.h>

typedef int (*ctest_fn)(void);

// --- registry descriptor (linked list) ---
typedef struct ctest_desc {
    const char *file;   // group label (e.g. "test_at_sample_rng.c")
    const char *name;   // test name (e.g. "test_at_sample_rng_probs_basic")
    ctest_fn fn;
    struct ctest_desc *next;
} ctest_desc;

// Append to end of registry list.
void ctest_register(const char *file, const char *name, ctest_fn fn);

// Run all registered tests in registration order.
// Prints grouped by file name.
int ctest_run_all(void);

// --- assertions ---
#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        fprintf(stderr, "ASSERT_TRUE failed: %s (%s:%d)\n", #x, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

// --- test declaration macro ---
// This matches your existing usage: CTEST(test_name) { ... return 0; }
#define CTEST(name) int name(void)
#define REG(FILESTR, fn) ctest_register((FILESTR), #fn, (fn))

#define ASSERT_EQ(a,b) \
  do { if ((a) != (b)) { \
    fprintf(stderr, "[FAIL] %s:%d: %s != %s (%lld != %lld)\n", \
      __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
    return 1; \
  }} while (0)

// --- shared test state (optional) ---
// Lets a test publish state for subsequent tests.
// State is a single global slot. If you need multiple slots later, we can key it.
typedef void (*ctest_state_dtor)(void *p);

// Set the shared state pointer. If a destructor is provided, it will be called
// when the state is replaced and at the end of ctest_run_all().
void ctest_state_set(void *p, ctest_state_dtor dtor);

// Convenience: set without destructor.
static inline void ctest_state_set_ptr(void *p) { ctest_state_set(p, NULL); }

// Get current shared state pointer (may be NULL).
void *ctest_state_get(void);

// Clear state (calls destructor if present).
void ctest_state_clear(void);
