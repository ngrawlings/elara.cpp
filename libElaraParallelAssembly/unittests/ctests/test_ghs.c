#include <stdint.h>
#include <string.h>

#include "ctest.h"
#include "memory/epa_ghs.h"
#include "memory/epa_ring_buffer.h"

#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#define EPA_GHS_TAG(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b)<<8) | ((uint32_t)(c)<<16) | ((uint32_t)(d)<<24))

/*
 * Basic GHS allocation / free sanity
 */
CTEST(test_ghs_alloc_free)
{
	epa_ghs_t *ghs;
	epa_ghs_handle_t h;
	uint8_t *out;

	ghs = epa_ghs_create(4096, NULL, NULL, NULL);

    epa_ghs_err_t err = epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, 16, &h);
    ASSERT_TRUE(err == EPA_GHS_OK);

    err = epa_ghs_get_ptr(ghs, h, (void**)&out);
    ASSERT_TRUE(err == EPA_GHS_OK);

    memset(out, 0xA5, 16);

    epa_ghs_free(ghs, h);

    /* after free, handle must no longer resolve */
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, (void**)&out) != EPA_GHS_OK);

    return 0;
}

/*
 * Ownership transfer: alloc → xfer → free
 */
CTEST(test_ghs_xfer_ownership)
{
	epa_ghs_t *ghs;
	epa_ghs_handle_t h;
	uint8_t *out;

	ghs = epa_ghs_create(4096, NULL, NULL, NULL);

    epa_ghs_err_t err = epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, 16, &h);
    ASSERT_TRUE(err == EPA_GHS_OK);

    /* kernel -> worker transfer */
    ASSERT_TRUE(epa_ghs_transfer(ghs, h, 1) == EPA_GHS_OK);

    /* worker must see valid memory */
    epa_ghs_err_t p = epa_ghs_get_ptr(ghs, h, (void**)&out);
    ASSERT_TRUE(p == EPA_GHS_OK);

    /* worker frees */
    epa_ghs_free(ghs, h);

    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, (void**)&out) != EPA_GHS_OK);

    return 0;
}

/*
 * Double free must be safe (no crash, no resurrection)
 */
CTEST(test_ghs_double_free)
{
	epa_ghs_t *ghs;
	epa_ghs_handle_t h;

	ghs = epa_ghs_create(4096, NULL, NULL, NULL);

    epa_ghs_err_t err = epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, 16, &h);
    ASSERT_TRUE(err == EPA_GHS_OK);

    epa_ghs_free(ghs, h);

    /* second free should be ignored or safely rejected */
    epa_ghs_free(ghs, h);

    uint8_t *out;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, (void**)&out) != EPA_GHS_OK);

    return 0;
}

CTEST(test_ghs_handle_roundtrip_ring)
{
    // ---- Setup ----
    char err[256] = {0};
    IdRing rb;
    ASSERT_TRUE(epa_ring_init(&rb, 8) == 1);

    epa_ghs_t *ghs = epa_ghs_create(64, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    // ---- Allocate a payload ----
    epa_ghs_handle_t h = 0;
    epa_ghs_handle_t h_alt = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, /*owner=*/0, /*size=*/32, &h) == EPA_GHS_OK);
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, /*owner=*/0, /*size=*/32, &h_alt) == EPA_GHS_OK);

    void *p0 = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &p0) == EPA_GHS_OK);
    ASSERT_TRUE(p0 != NULL);

    // Write a sentinel pattern
    uint8_t *b0 = (uint8_t *)p0;
    for (int i = 0; i < 32; i++) b0[i] = (uint8_t)(0xA0u + (uint8_t)i);

    // ---- Push handle through ring as two u32 words ----
    uint32_t lo = (uint32_t)(h & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(h >> 32);

    ASSERT_TRUE(epa_ring_push(&rb, lo, /*soft=*/0, err) == 1);
    ASSERT_TRUE(epa_ring_push(&rb, hi, /*soft=*/0, err) == 1);

    // ---- Pop and reconstruct ----
    uint32_t lo2 = 0, hi2 = 0;
    ASSERT_TRUE(epa_ring_pop(&rb, &lo2) == 1);
    ASSERT_TRUE(epa_ring_pop(&rb, &hi2) == 1);

    epa_ghs_handle_t h2 = ((uint64_t)hi2 << 32) | (uint64_t)lo2;

    // Sanity: reconstructed handle should match
    ASSERT_TRUE(h2 == h);

    // ---- Validate + read back ----
    ASSERT_TRUE(epa_ghs_validate(ghs, h2) == EPA_GHS_OK);

    void *p1 = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h2, &p1) == EPA_GHS_OK);
    ASSERT_TRUE(p1 != NULL);

    uint8_t *b1 = (uint8_t *)p1;
    for (int i = 0; i < 32; i++) {
        ASSERT_TRUE(b1[i] == (uint8_t)(0xA0u + (uint8_t)i));
    }

    // ---- Cleanup ----
    ASSERT_TRUE(epa_ghs_free(ghs, h2) == EPA_GHS_OK);
    ASSERT_TRUE(epa_ghs_free(ghs, h_alt) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);
    epa_ring_free(&rb);
    return 0;
}

CTEST(test_ghs_full_and_thrash_10m)
{

	clock_t t0 = clock();

    // ---- config ----
    const uint32_t payload_size = 16;

    // Default 10 million iterations, override with:
    //   EPA_GHS_STRESS_ITERS=5000000 ./build/unittests/ctest_runner
    uint32_t iters = 10000000u;
    const char *env = getenv("EPA_GHS_STRESS_ITERS");
    if (env && *env) {
        long v = strtol(env, NULL, 10);
        if (v > 0) iters = (uint32_t)v;
    }

    // ---- create GHS ----
    // Use whatever capacity you normally use; "full" is discovered by allocating until failure.
    epa_ghs_t *ghs = epa_ghs_create(64, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    // ---- allocate until full ----
    uint32_t cap_guess = 64;
    epa_ghs_handle_t *handles = (epa_ghs_handle_t *)calloc(cap_guess, sizeof(*handles));
    uint8_t *alive = (uint8_t *)calloc(cap_guess, 1);
    ASSERT_TRUE(handles != NULL && alive != NULL);

    uint32_t n = 0;
    for (;;) {
        if (n == cap_guess) {
            cap_guess *= 2;
            handles = (epa_ghs_handle_t *)realloc(handles, cap_guess * sizeof(*handles));
            alive   = (uint8_t *)realloc(alive, cap_guess);
            ASSERT_TRUE(handles != NULL && alive != NULL);
            memset(handles + n, 0, (cap_guess - n) * sizeof(*handles));
            memset(alive + n, 0, (cap_guess - n));
        }

        epa_ghs_handle_t h = 0;
        int rc = epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, /*owner=*/0, payload_size, &h);
        if (rc != EPA_GHS_OK) break; // full

        handles[n] = h;
        alive[n] = 1;

        // touch memory once to ensure ptr is valid
        void *p = NULL;
        ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &p) == EPA_GHS_OK);
        ASSERT_TRUE(p != NULL);
        ((uint8_t*)p)[0] = (uint8_t)(n & 0xFF);

        n++;
    }

    // Must have at least 1 slot
    ASSERT_TRUE(n > 0);

    // Next alloc must fail (still full)
    {
        epa_ghs_handle_t h = 0;
        int rc = epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, /*owner=*/0, payload_size, &h);
        ASSERT_TRUE(rc != EPA_GHS_OK);
    }

    // ---- thrash alloc/free up/down for iters ----
    // Deterministic xorshift32
    uint32_t rng = 0xC0FFEEu;
    uint32_t live_count = n;

    // Helper lambdas (C-style)
    #define XRAND() (rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5, rng)

    for (uint32_t i = 0; i < iters; i++) {
        uint32_t r = XRAND();
        uint32_t idx = r % n;

        // Decide action, but keep invariants sane
        int want_alloc = (r & 1);

        if (live_count == 0) want_alloc = 1;
        if (live_count == n) want_alloc = 0;

        if (want_alloc) {
            // allocate into a "dead" slot
            if (alive[idx]) continue;

            epa_ghs_handle_t h = 0;
            int rc = epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, /*owner=*/0, payload_size, &h);

            // If there is space, alloc must succeed
            ASSERT_TRUE(rc == EPA_GHS_OK);
            ASSERT_TRUE(h != 0);

            handles[idx] = h;
            alive[idx] = 1;
            live_count++;

            // Occasionally validate pointer and write a sentinel
            if ((i & 1023u) == 0) {
                void *p = NULL;
                ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &p) == EPA_GHS_OK);
                ASSERT_TRUE(p != NULL);
                ((uint8_t*)p)[0] = 0xAB;
                ((uint8_t*)p)[payload_size - 1] = 0xCD;
            }
        } else {
            // free an "alive" slot
            if (!alive[idx]) continue;

            epa_ghs_handle_t h = handles[idx];
            ASSERT_TRUE(h != 0);

            ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);

            alive[idx] = 0;
            handles[idx] = 0;
            live_count--;
        }

        // Invariant checks: when full, alloc must fail
        if ((i & 4095u) == 0 && live_count == n) {
            epa_ghs_handle_t h = 0;
            int rc = epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, /*owner=*/0, payload_size, &h);
            ASSERT_TRUE(rc != EPA_GHS_OK);
        }
    }

    // ---- cleanup: free anything still alive ----
    for (uint32_t i = 0; i < n; i++) {
        if (alive[i]) {
            ASSERT_TRUE(epa_ghs_free(ghs, handles[i]) == EPA_GHS_OK);
        }
    }


    free(handles);
    free(alive);
    epa_ghs_destroy(ghs);

    clock_t t1 = clock();
    double sec = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;

    fprintf(stderr, "[GHS] thrash iters=%u time=%.6f sec (%.2f M it/s)\n", iters, sec, (iters / 1e6) / sec);

    return 0;

    #undef XRAND
}

CTEST(test_ghs_transfer_preserves_payload)
{
    epa_ghs_t *ghs = epa_ghs_create(64, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n = 256;
    const uint32_t owner_a = 0;
    const uint32_t owner_b = 7;

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, owner_a, n, &h) == EPA_GHS_OK);
    ASSERT_TRUE(h != 0);

    // Write deterministic pattern
    void *p0 = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &p0) == EPA_GHS_OK);
    ASSERT_TRUE(p0 != NULL);

    uint8_t *b0 = (uint8_t*)p0;
    for (uint32_t i = 0; i < n; i++) {
        b0[i] = (uint8_t)(0x5Au ^ (uint8_t)i);
    }

    // Transfer ownership
    ASSERT_TRUE(epa_ghs_transfer(ghs, h, owner_b) == EPA_GHS_OK);

    // Read back and verify bytes unchanged
    void *p1 = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &p1) == EPA_GHS_OK);
    ASSERT_TRUE(p1 != NULL);

    uint8_t *b1 = (uint8_t*)p1;
    for (uint32_t i = 0; i < n; i++) {
        ASSERT_TRUE(b1[i] == (uint8_t)(0x5Au ^ (uint8_t)i));
    }

    // Cleanup
    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);
    return 0;
}

CTEST(test_ghs_tag_persists_across_xfer)
{
    epa_ghs_t *ghs = epa_ghs_create(1024, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 123, 64, &h) == EPA_GHS_OK);

    ASSERT_TRUE(epa_ghs_set_tag(ghs, h, EPA_GHS_TAG('K','V','N','D')) == EPA_GHS_OK);

    uint32_t tag = 0;
    ASSERT_TRUE(epa_ghs_get_tag(ghs, h, &tag) == EPA_GHS_OK);
    ASSERT_TRUE(tag == EPA_GHS_TAG('K','V','N','D'));

    // ownership xfer should not alter tag
    ASSERT_TRUE(epa_ghs_transfer(ghs, h, 0) == EPA_GHS_OK);

    tag = 0;
    ASSERT_TRUE(epa_ghs_get_tag(ghs, h, &tag) == EPA_GHS_OK);
    ASSERT_TRUE(tag == EPA_GHS_TAG('K','V','N','D'));

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);
    return 0;
}


void ctest_register_test_ghs(void) {
    const char *F = "test_ghs.c";
    REG(F, test_ghs_alloc_free);
    REG(F, test_ghs_xfer_ownership);
    REG(F, test_ghs_double_free);
    REG(F, test_ghs_handle_roundtrip_ring);
    REG(F, test_ghs_full_and_thrash_10m);
    REG(F, test_ghs_transfer_preserves_payload);
    REG(F, test_ghs_tag_persists_across_xfer);
}

