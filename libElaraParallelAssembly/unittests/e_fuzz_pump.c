#include "e_fuzz_pump.h"

static uint32_t xorshift32(uint32_t *s)
{
    *s ^= *s << 13u;
    *s ^= *s >> 17u;
    *s ^= *s << 5u;
    return *s;
}

void e_fuzz_pump(e_fuzz_push_fn push, void *ctx, uint32_t wid,
                 uint32_t seed, uint32_t n_msgs)
{
    uint32_t s = seed ? seed : 0xdeadbeef;
    uint32_t buf[8];
    uint32_t i, j;
    for (i = 0; i < n_msgs; i++) {
        uint32_t words = xorshift32(&s) % 9u;   /* 0–8 words = 0–32 bytes */
        for (j = 0; j < 8u; j++) buf[j] = xorshift32(&s);
        push(ctx, wid, buf, words * 4u);
    }
}
