// unittests/ctests/test_stack.c
#include <stdint.h>

#include "ctest.h"
#include "memory/epa_stack.h"

CTEST(test_stack_push_pop)
{
    EpaStack st;
    epa_stack_init(&st);

    ASSERT_EQ(st.sp, 0u);

    ASSERT_TRUE(epa_stack_push(&st, 0x11u));
    ASSERT_TRUE(epa_stack_push(&st, 0x22u));
    ASSERT_TRUE(epa_stack_push(&st, 0x33u));
    ASSERT_EQ(st.sp, 3u);

    uint32_t v = 0;

    // Peek does not pop
    ASSERT_TRUE(epa_stack_peek(&st, &v));
    ASSERT_EQ(v, 0x33u);
    ASSERT_EQ(st.sp, 3u);

    // Pop is LIFO
    ASSERT_TRUE(epa_stack_pop(&st, &v));
    ASSERT_EQ(v, 0x33u);

    ASSERT_TRUE(epa_stack_pop(&st, &v));
    ASSERT_EQ(v, 0x22u);

    ASSERT_TRUE(epa_stack_pop(&st, &v));
    ASSERT_EQ(v, 0x11u);

    ASSERT_EQ(st.sp, 0u);

    epa_stack_free(&st);
    return 0;
}

CTEST(test_stack_underflow)
{
    EpaStack st;
    epa_stack_init(&st);

    uint32_t v = 0xDEADBEEFu;

    // Pop on empty should fail and not modify v (nice-to-have)
    ASSERT_TRUE(epa_stack_pop(&st, &v) == 0);
    ASSERT_EQ(v, 0xDEADBEEFu);

    // Peek on empty should fail and not modify v
    ASSERT_TRUE(epa_stack_peek(&st, &v) == 0);
    ASSERT_EQ(v, 0xDEADBEEFu);

    epa_stack_free(&st);
    return 0;
}

CTEST(test_stack_overflow)
{
    EpaStack st;
    epa_stack_init(&st);

    // Fill to capacity (cap is defined by implementation; currently 4096)
    for (size_t i = 0; i < st.cap; i++) {
        ASSERT_TRUE(epa_stack_push(&st, (uint32_t)i));
    }
    ASSERT_EQ(st.sp, st.cap);

    // One more push must fail
    ASSERT_TRUE(epa_stack_push(&st, 0xFFFFFFFFu) == 0);
    ASSERT_EQ(st.sp, st.cap);

    // Pop a couple to ensure still consistent after failed push
    uint32_t v = 0;
    ASSERT_TRUE(epa_stack_pop(&st, &v));
    ASSERT_EQ(v, (uint32_t)(st.cap - 1));

    ASSERT_TRUE(epa_stack_pop(&st, &v));
    ASSERT_EQ(v, (uint32_t)(st.cap - 2));

    epa_stack_free(&st);
    return 0;
}

void ctest_register_test_stack(void) {
    const char *F = "test_stack.c";
    REG(F, test_stack_push_pop);
    REG(F, test_stack_underflow);
    REG(F, test_stack_overflow);
}
