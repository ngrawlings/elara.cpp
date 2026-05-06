// unittests/ctests/test_at_kv_cache_append.c
#include "../ctest.h"

#include <stdint.h>
#include <string.h>

#include "memory/epa_ghs.h"
#include "atomic_tasks/at_kv_cache_append.h"

static void fill_seq(float *dst, size_t n, float base) {
    for (size_t i = 0; i < n; i++) dst[i] = base + (float)i;
}

static int floats_equal(const float *a, const float *b, size_t n) {
    // For this test we use exact equality because we only memcpy (no math).
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static epa_ghs_handle_t make_payload(
    epa_ghs_t *ghs,
    uint32_t n_heads,
    uint32_t head_dim,
    uint32_t capacity,
    uint32_t cur_len,
    uint32_t append_len,
    float k_new_base,
    float v_new_base,
    float k_cache_init,
    float v_cache_init
) {
    const size_t vec_per_tok = (size_t)n_heads * (size_t)head_dim;

    const size_t k_new_count   = (size_t)append_len * vec_per_tok;
    const size_t v_new_count   = (size_t)append_len * vec_per_tok;
    const size_t k_cache_count = (size_t)capacity   * vec_per_tok;
    const size_t v_cache_count = (size_t)capacity   * vec_per_tok;

    const size_t bytes =
        sizeof(AtKvAppendHdr_v1) +
        (k_new_count + v_new_count + k_cache_count + v_cache_count) * sizeof(float);

    epa_ghs_handle_t h = 0;
    ASSERT_TRUE(epa_ghs_alloc(ghs, EPA_GHS_T_BYTES, 0, (uint32_t)bytes, &h) == EPA_GHS_OK);

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    ASSERT_TRUE(base != NULL);

    memset(base, 0, bytes);

    uint8_t *p = (uint8_t *)base;

    AtKvAppendHdr_v1 *hdr = (AtKvAppendHdr_v1 *)p;
    hdr->flags      = AT_KV_F_NONE;
    hdr->n_heads    = n_heads;
    hdr->head_dim   = head_dim;
    hdr->capacity   = capacity;
    hdr->cur_len    = cur_len;
    hdr->append_len = append_len;

    p += sizeof(*hdr);

    float *K_new   = (float *)p; p += k_new_count   * sizeof(float);
    float *V_new   = (float *)p; p += v_new_count   * sizeof(float);
    float *K_cache = (float *)p; p += k_cache_count * sizeof(float);
    float *V_cache = (float *)p; /*p += v_cache_count * sizeof(float);*/

    if (append_len) {
        fill_seq(K_new, k_new_count, k_new_base);
        fill_seq(V_new, v_new_count, v_new_base);
    }

    // initialize cache to known values so we can verify only the expected slice changes
    for (size_t i = 0; i < k_cache_count; i++) K_cache[i] = k_cache_init;
    for (size_t i = 0; i < v_cache_count; i++) V_cache[i] = v_cache_init;

    return h;
}

CTEST(test_at_kv_cache_append_one)
{
    epa_ghs_t *ghs = epa_ghs_create(4096, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_heads = 2;
    const uint32_t head_dim = 4;
    const uint32_t capacity = 8;
    const uint32_t cur_len = 3;
    const uint32_t append_len = 1;

    const size_t vec_per_tok = (size_t)n_heads * (size_t)head_dim;
    const size_t slice_count = (size_t)append_len * vec_per_tok;

    epa_ghs_handle_t h = make_payload(
        ghs, n_heads, head_dim, capacity, cur_len, append_len,
        /*k_new_base*/ 100.0f,
        /*v_new_base*/ 200.0f,
        /*k_cache_init*/ -1.0f,
        /*v_cache_init*/ -2.0f
    );

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);

    uint8_t *p = (uint8_t *)base;
    AtKvAppendHdr_v1 *hdr = (AtKvAppendHdr_v1 *)p;
    p += sizeof(*hdr);

    float *K_new   = (float *)p; p += slice_count * sizeof(float);
    float *V_new   = (float *)p; p += slice_count * sizeof(float);
    float *K_cache = (float *)p; p += ((size_t)capacity * vec_per_tok) * sizeof(float);
    float *V_cache = (float *)p;

    int rc = at_kv_cache_append_run(ghs, h);
    ASSERT_TRUE(rc == AT_KV_OK);

    // cur_len should advance by 1
    ASSERT_TRUE(hdr->cur_len == (cur_len + append_len));

    // expected destination region
    const size_t dst_off = (size_t)cur_len * vec_per_tok;
    ASSERT_TRUE(floats_equal(&K_cache[dst_off], K_new, slice_count));
    ASSERT_TRUE(floats_equal(&V_cache[dst_off], V_new, slice_count));

    // untouched regions should remain init
    // check one element before and after (if in range)
    if (dst_off > 0) {
        ASSERT_TRUE(K_cache[dst_off - 1] == -1.0f);
        ASSERT_TRUE(V_cache[dst_off - 1] == -2.0f);
    }
    if (dst_off + slice_count < (size_t)capacity * vec_per_tok) {
        ASSERT_TRUE(K_cache[dst_off + slice_count] == -1.0f);
        ASSERT_TRUE(V_cache[dst_off + slice_count] == -2.0f);
    }

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_kv_cache_append_multi)
{
    epa_ghs_t *ghs = epa_ghs_create(8192, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_heads = 1;
    const uint32_t head_dim = 8;
    const uint32_t capacity = 16;
    const uint32_t cur_len = 5;
    const uint32_t append_len = 4;

    const size_t vec_per_tok = (size_t)n_heads * (size_t)head_dim;
    const size_t slice_count = (size_t)append_len * vec_per_tok;

    epa_ghs_handle_t h = make_payload(
        ghs, n_heads, head_dim, capacity, cur_len, append_len,
        /*k_new_base*/ 10.0f,
        /*v_new_base*/ 20.0f,
        /*k_cache_init*/ 0.0f,
        /*v_cache_init*/ 0.0f
    );

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);

    uint8_t *p = (uint8_t *)base;
    AtKvAppendHdr_v1 *hdr = (AtKvAppendHdr_v1 *)p;
    p += sizeof(*hdr);

    float *K_new   = (float *)p; p += slice_count * sizeof(float);
    float *V_new   = (float *)p; p += slice_count * sizeof(float);
    float *K_cache = (float *)p; p += ((size_t)capacity * vec_per_tok) * sizeof(float);
    float *V_cache = (float *)p;

    int rc = at_kv_cache_append_run(ghs, h);
    ASSERT_TRUE(rc == AT_KV_OK);

    ASSERT_TRUE(hdr->cur_len == (cur_len + append_len));

    const size_t dst_off = (size_t)cur_len * vec_per_tok;
    ASSERT_TRUE(floats_equal(&K_cache[dst_off], K_new, slice_count));
    ASSERT_TRUE(floats_equal(&V_cache[dst_off], V_new, slice_count));

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_kv_cache_append_capacity_full_fails)
{
    epa_ghs_t *ghs = epa_ghs_create(4096, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_heads = 1;
    const uint32_t head_dim = 4;
    const uint32_t capacity = 4;
    const uint32_t cur_len = 4;     // full
    const uint32_t append_len = 1;  // would overflow

    epa_ghs_handle_t h = make_payload(
        ghs, n_heads, head_dim, capacity, cur_len, append_len,
        1.0f, 2.0f, 0.0f, 0.0f
    );

    int rc = at_kv_cache_append_run(ghs, h);
    ASSERT_TRUE(rc == AT_KV_ERR);

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_kv_cache_append_overflow_fails)
{
    epa_ghs_t *ghs = epa_ghs_create(4096, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_heads = 2;
    const uint32_t head_dim = 2;
    const uint32_t capacity = 8;
    const uint32_t cur_len = 7;
    const uint32_t append_len = 2; // 7+2 > 8

    epa_ghs_handle_t h = make_payload(
        ghs, n_heads, head_dim, capacity, cur_len, append_len,
        3.0f, 4.0f, -9.0f, -8.0f
    );

    int rc = at_kv_cache_append_run(ghs, h);
    ASSERT_TRUE(rc == AT_KV_ERR);

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

CTEST(test_at_kv_cache_append_zero_len_noop)
{
    epa_ghs_t *ghs = epa_ghs_create(4096, NULL, NULL, NULL);
    ASSERT_TRUE(ghs != NULL);

    const uint32_t n_heads = 1;
    const uint32_t head_dim = 4;
    const uint32_t capacity = 8;
    const uint32_t cur_len = 3;
    const uint32_t append_len = 0; // no-op

    // Still valid payload; K_new/V_new sections are size 0
    epa_ghs_handle_t h = make_payload(
        ghs, n_heads, head_dim, capacity, cur_len, append_len,
        0.0f, 0.0f, 7.0f, 9.0f
    );

    void *base = NULL;
    ASSERT_TRUE(epa_ghs_get_ptr(ghs, h, &base) == EPA_GHS_OK);
    AtKvAppendHdr_v1 *hdr = (AtKvAppendHdr_v1 *)base;

    int rc = at_kv_cache_append_run(ghs, h);
    ASSERT_TRUE(rc == AT_KV_OK);
    ASSERT_TRUE(hdr->cur_len == cur_len);

    ASSERT_TRUE(epa_ghs_free(ghs, h) == EPA_GHS_OK);
    epa_ghs_destroy(ghs);

    return 0;
}

void ctest_register_test_at_kv_cache(void) {
    const char *F = "test_at_kv_cache.c";
    REG(F, test_at_kv_cache_append_one);
    REG(F, test_at_kv_cache_append_multi);
    REG(F, test_at_kv_cache_append_capacity_full_fails);
    REG(F, test_at_kv_cache_append_overflow_fails);
    REG(F, test_at_kv_cache_append_zero_len_noop);
}

