// test_ring_buffer.c
#include "ctest.h"
#include "memory/epa_ring_buffer.h"

CTEST(test_ring_basic) {
	IdRing rb;
    epa_ring_init(&rb, 8);

    char err[255];
    uint32_t val;

    ASSERT_EQ(epa_ring_count(&rb), 0);
    ASSERT_TRUE(epa_ring_push(&rb, 42, 0, err));
    epa_ring_pop(&rb, &val);
    ASSERT_EQ(val, 42);

    return 0;
}

void ctest_register_test_ring(void) {
    const char *F = "test_ring.c";
    REG(F, test_ring_basic);
}
