#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t epa_rgm_handle_t;

static inline uint32_t epa_rgm_handle_index(epa_rgm_handle_t h) { return (uint32_t)(h & 0xffffffffu); }
static inline uint32_t epa_rgm_handle_gen(epa_rgm_handle_t h) { return (uint32_t)(h >> 32); }
static inline epa_rgm_handle_t epa_rgm_make_handle(uint32_t idx, uint32_t gen) {
  return ((uint64_t)gen << 32) | (uint64_t)idx;
}

typedef enum {
  EPA_RGM_OK = 0,
  EPA_RGM_ERR_NULL = -1,
  EPA_RGM_ERR_OOM = -2,
  EPA_RGM_ERR_FULL = -3,
  EPA_RGM_ERR_BAD_HANDLE = -4,
  EPA_RGM_ERR_BAD_SIZE = -5,
  EPA_RGM_ERR_DUPLICATE = -6,
  EPA_RGM_ERR_BOUNDS = -7,
  EPA_RGM_ERR_NOT_FOUND = -8
} epa_rgm_err_t;

typedef struct {
  uint64_t name_uid;
  uint32_t size_bytes;
  uint32_t generation;
} epa_rgm_meta_t;

typedef struct {
  uint32_t in_use;
  uint32_t generation;
  uint64_t name_uid;
  uint32_t size_bytes;
  uint8_t *bytes;
} epa_rgm_entry_t;

typedef struct {
  uint32_t max_entries;
  uint32_t live_count;
  epa_rgm_entry_t *entries;
} epa_rgm_t;

epa_rgm_t *epa_rgm_create(uint32_t max_entries);
epa_rgm_t *epa_rgm_global(void);
void epa_rgm_destroy(epa_rgm_t *rgm);
void epa_rgm_reset(epa_rgm_t *rgm);

epa_rgm_err_t epa_rgm_publish_copy(epa_rgm_t *rgm,
                                   uint64_t name_uid,
                                   const void *bytes,
                                   uint32_t size_bytes,
                                   epa_rgm_handle_t *out_handle);

epa_rgm_err_t epa_rgm_get_by_name(epa_rgm_t *rgm,
                                  uint64_t name_uid,
                                  epa_rgm_handle_t *out_handle);

epa_rgm_err_t epa_rgm_get_meta(epa_rgm_t *rgm,
                               epa_rgm_handle_t h,
                               epa_rgm_meta_t *out_meta);

epa_rgm_err_t epa_rgm_read_bytes(epa_rgm_t *rgm,
                                 epa_rgm_handle_t h,
                                 uint32_t offset,
                                 void *dst,
                                 uint32_t len);

epa_rgm_err_t epa_rgm_validate(epa_rgm_t *rgm, epa_rgm_handle_t h);

#ifdef __cplusplus
}
#endif
