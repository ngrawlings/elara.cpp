// src/atomic_tasks/at_kv_cache_append.c
#include "at_kv_cache_append.h"

#include <stddef.h>
#include <string.h>

int at_kv_cache_append_run(epa_ghs_t *ghs, epa_ghs_handle_t h) {
    if (!ghs) return AT_KV_ERR;

    void *base = NULL;
    epa_ghs_meta_t meta;

    if (epa_ghs_get_meta(ghs, h, &meta) != EPA_GHS_OK) return AT_KV_ERR;
    if (epa_ghs_get_ptr(ghs, h, &base) != EPA_GHS_OK)  return AT_KV_ERR;
    if (!base) return AT_KV_ERR;

    if (meta.size_bytes < (uint32_t)sizeof(AtKvAppendHdr_v1)) return AT_KV_ERR;

    uint8_t *p = (uint8_t *)base;
    AtKvAppendHdr_v1 *hdr = (AtKvAppendHdr_v1 *)p;

    const uint32_t n_heads    = hdr->n_heads;
    const uint32_t head_dim   = hdr->head_dim;
    const uint32_t capacity   = hdr->capacity;
    const uint32_t cur_len    = hdr->cur_len;
    const uint32_t append_len = hdr->append_len;

    if (n_heads == 0 || head_dim == 0 || capacity == 0) return AT_KV_ERR;
    if (append_len == 0) return AT_KV_OK; // nothing to do
    if (cur_len > capacity) return AT_KV_ERR;
    if (append_len > (capacity - cur_len)) return AT_KV_ERR;

    const size_t vec_per_tok = (size_t)n_heads * (size_t)head_dim;

    const size_t k_new_count    = (size_t)append_len * vec_per_tok;
    const size_t v_new_count    = (size_t)append_len * vec_per_tok;
    const size_t k_cache_count  = (size_t)capacity   * vec_per_tok;
    const size_t v_cache_count  = (size_t)capacity   * vec_per_tok;

    const size_t bytes_needed =
        sizeof(AtKvAppendHdr_v1) +
        (k_new_count + v_new_count + k_cache_count + v_cache_count) * sizeof(float);

    if ((size_t)meta.size_bytes < bytes_needed) return AT_KV_ERR;

    // Layout: [Hdr][K_new][V_new][K_cache][V_cache]
    p += sizeof(AtKvAppendHdr_v1);
    float *K_new   = (float *)p; p += k_new_count   * sizeof(float);
    float *V_new   = (float *)p; p += v_new_count   * sizeof(float);
    float *K_cache = (float *)p; p += k_cache_count * sizeof(float);
    float *V_cache = (float *)p; /*p += v_cache_count * sizeof(float);*/

    // Destination offset in cache
    const size_t dst_off = (size_t)cur_len * vec_per_tok;

    // Copy new slices into cache
    memcpy(&K_cache[dst_off], K_new, k_new_count * sizeof(float));
    memcpy(&V_cache[dst_off], V_new, v_new_count * sizeof(float));

    // Update header
    hdr->cur_len = cur_len + append_len;

    return AT_KV_OK;
}
