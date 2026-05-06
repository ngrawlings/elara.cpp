# EPA Unit Test Authoring Guide

This document is a self-contained guide for writing unit tests in the EPA codebase using the project’s `ctest` framework and the linked-list registry model.

---

## 1) Goals and philosophy

### What unit tests should do
- Validate **behavior** and **invariants**, not implementation details.
- Be **deterministic** and **repeatable**.
- Keep tests **small** and **focused**: one concept per test.
- Prefer **cheap**, CPU-side validation for primitives and correctness.

### What unit tests should not do
- Not depend on external services.
- Not require GPU unless explicitly building GPU test tiers.
- Not assume global state is pristine unless the test itself sets it.
- Not leak resources (GHS handles, mmaps, heap allocations).

---

## 2) Test framework overview

### The `CTEST()` macro
A unit test is a C function with signature:

- `int test_name(void)`

Declared via:

- `CTEST(test_name) { ... return 0; }`

**Return value**
- `return 0;` → PASS
- non-zero → FAIL (framework prints `[FAIL]`)

### Assertions
Use the assertion macros from `ctest.h`:

- `ASSERT_TRUE(expr)`

This prints a failure message and returns `1` from the test.

Guidelines:
- Put the *most important* assertions first.
- Use assertions to protect against null pointers before dereferencing.

---

## 3) Registry model: how tests get discovered

EPA uses a **linked-list registry** of `ctest_desc` descriptors.

Each test file provides a single registration function:

- `void ctest_register_<file_group>(void)`

Inside it, you append tests:

- `ctest_register("test_file.c", "test_name", test_name);`

### Recommended helper macro
In test files, define:

- `#define REG(F, fn) ctest_register((F), #fn, (fn))`

Then registration becomes:

```c
void ctest_register_test_at_sample_rng(void) {
    const char *F = "test_at_sample_rng.c";
    REG(F, test_at_sample_rng_probs_basic);
    REG(F, test_at_sample_rng_topk_probs);
}
```

### Grouping in output
The runner prints tests grouped by the descriptor’s `file` string. This is why each file sets a single `const char *F = "...";` and uses it for all tests in the file.

---

## 4) Where tests live

Standard layout:

- `unittests/ctests/test_*.c`

The runner groups tests by `file` label, which should match the source file name for readability:

- `"test_ghs.c"`
- `"test_at_linear_o.c"`
- etc.

---

## 5) How to structure a test file

A typical test file has:

1. Includes
2. Local helper functions (epsilon compare, reference implementation)
3. One or more `CTEST(...)` blocks
4. One registry function `ctest_register_<group>()`

### Template

```c
#include "ctest.h"
#include "memory/epa_ghs.h"

#include <stdint.h>
#include <string.h>

static int feq(float a, float b) {
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d < 1e-4f;
}

CTEST(test_some_feature_basic)
{
    // Arrange
    // Act
    // Assert
    return 0;
}

void ctest_register_test_some_feature(void)
{
    const char *F = "test_some_feature.c";
    REG(F, test_some_feature_basic);
}
```

---

## 6) Memory and resources: preferred patterns

### Use GHS for test-owned buffers
When testing ATs and memory-facing components, allocate buffers via the GHS API. This matches runtime behavior and avoids ad-hoc heap assumptions.

Typical pattern:

1. Create a GHS:
   - `epa_ghs_t *ghs = epa_ghs_create(capacity, NULL, NULL, NULL);`
2. Allocate:
   - `epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, bytes, &h)`
3. Map pointer:
   - `epa_ghs_get_ptr(ghs, h, &base)`
4. Write header/payload into mapped memory
5. Run the AT / function under test
6. Validate outputs
7. Free handle and destroy GHS

### Always clean up
Every successful `epa_ghs_alloc` should have:
- `epa_ghs_free(ghs, h)`

Every `epa_ghs_create` should have:
- `epa_ghs_destroy(ghs)`

Put assertions *after* allocation and *before* use:

- `ASSERT_TRUE(ghs != NULL);`
- `ASSERT_TRUE(epa_ghs_alloc(...) == EPA_GHS_OK);`

---

## 7) Arrange / Act / Assert discipline

Even in C, follow AAA:

### Arrange
- Allocate resources
- Initialize headers and buffers
- Set deterministic seeds

### Act
- Call the function under test exactly once

### Assert
- Validate outputs and invariants
- Validate return codes
- Validate boundaries and error cases

This makes tests easier to read and debug.

---

## 8) Determinism rules

### RNG
If the test involves randomness:
- Set deterministic RNG state in the header.
- Assert that the result is within expected constraints.

### Floating point
Use an epsilon compare helper (`feq`) for probability, softmax, RMSNorm output, etc.

### Avoid time and ordering dependencies
- Do not rely on wall clock time.
- Do not rely on filesystem ordering.

---

## 9) Reference implementations

When validating numerical kernels, include small CPU reference functions in the test file:

- Softmax reference
- Top-k reference
- Minimal GEMM reference (for tiny shapes)

Guidelines:
- Keep reference code correct and stable.
- Use stable forms (e.g., max-subtracted softmax).
- Validate a subset of cases if full reference is expensive.

---

## 10) Testing error paths

Every module should have tests for:

- Missing required inputs
- Invalid flags
- Out-of-bounds offsets
- Capacity full
- Overflow conditions

Pattern:

```c
ASSERT_TRUE(fn_under_test(...) == EXPECTED_ERROR_CODE);
```

And validate outputs are untouched if required.

---

## 11) Optional / environment-gated tests

Some tests depend on external state (e.g., a local HF model directory). These should be **gated** so CI passes by default.

Pattern:

```c
const char *dir = getenv("EPA_TEST_MODEL_DIR");
if (!dir || !dir[0]) return 0; // skip/pass
```

Use this for:
- HF loader tests that require real model files
- Large thrash tests (optional tier)

---

## 12) Naming conventions

### Test function names
Prefer descriptive, unique names:

- `test_at_sample_rng_probs_basic`
- `test_at_linear_o_missing_weights_fails`

### File grouping
Set the file label to the actual file name:

- `const char *F = "test_at_sample_rng.c";`

This is what the runner prints as the group header.

---

## 13) Writing new tests: checklist

Before submitting a new test:

- [ ] Test is deterministic.
- [ ] Uses `ASSERT_TRUE` for all critical invariants.
- [ ] Cleans up all resources.
- [ ] Has at least one error-path test if applicable.
- [ ] Registered via the file’s `ctest_register_*()` function.
- [ ] Group label matches the file name.

---

## 14) Common pitfalls

- Forgetting to free a GHS handle.
- Dereferencing `base` before checking `epa_ghs_get_ptr` result.
- Using float equality (`==`) instead of epsilon compare.
- Writing tests that require specific machine conditions.
- Failing fast without printing context (always print key state in debug-only tests).

---

## 15) Patterns for “dump” / “diagnostic” tests

A dump test is allowed when:
- It is environment-gated.
- It prints a single cohesive block of state.
- It still asserts core invariants when enabled.

Keep dump output bracketed:

- `BEGIN ... END` markers

This makes logs searchable.

---

## 16) Example: a good AT test pattern

1. Create GHS
2. Allocate bytes for header + payload
3. Fill header flags and sizes
4. Fill inputs
5. Run AT
6. Validate outputs and invariants
7. Free + destroy

This is the canonical pattern used by `test_at_sample_rng.c`.
