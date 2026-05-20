#pragma once
#include <stdint.h>

/* Push one message into a kernel worker.  Returns 1 on success, 0 if queue full. */
typedef int (*e_fuzz_push_fn)(void *ctx, uint32_t wid, const void *data, uint32_t len);

/*
 * Pump n_msgs randomly-sized, randomly-filled messages into wid.
 * Message sizes cycle through 0, 4, 8, 12, 16, 20, 24, 28, 32 bytes.
 * seed=0 is replaced with a default non-zero seed.
 * Silently drops messages when push() returns 0 (queue full).
 */
void e_fuzz_pump(e_fuzz_push_fn push, void *ctx, uint32_t wid,
                 uint32_t seed, uint32_t n_msgs);
