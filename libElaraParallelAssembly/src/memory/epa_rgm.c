#include "memory/epa_rgm.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static pthread_mutex_t g_rgm_mu = PTHREAD_MUTEX_INITIALIZER;
static epa_rgm_t *g_rgm = NULL;

static int rgm_entry_is_published(const epa_rgm_entry_t *e) {
  return __atomic_load_n(&e->in_use, __ATOMIC_ACQUIRE) != 0u;
}

static void rgm_entry_publish(epa_rgm_entry_t *e) {
  __atomic_store_n(&e->in_use, 1u, __ATOMIC_RELEASE);
}

static void rgm_entry_unpublish(epa_rgm_entry_t *e) {
  __atomic_store_n(&e->in_use, 0u, __ATOMIC_RELEASE);
}

epa_rgm_t *epa_rgm_create(uint32_t max_entries) {
  epa_rgm_t *rgm;
  if (max_entries == 0u) return NULL;
  rgm = (epa_rgm_t*)calloc(1, sizeof(*rgm));
  if (!rgm) return NULL;
  rgm->entries = (epa_rgm_entry_t*)calloc(max_entries, sizeof(epa_rgm_entry_t));
  if (!rgm->entries) {
    free(rgm);
    return NULL;
  }
  rgm->max_entries = max_entries;
  for (uint32_t i = 0; i < max_entries; i++) {
    rgm->entries[i].generation = 1u;
  }
  return rgm;
}

epa_rgm_t *epa_rgm_global(void) {
  epa_rgm_t *rgm;
  pthread_mutex_lock(&g_rgm_mu);
  if (!g_rgm) {
    g_rgm = epa_rgm_create(65536u);
  }
  rgm = g_rgm;
  pthread_mutex_unlock(&g_rgm_mu);
  return rgm;
}

void epa_rgm_reset(epa_rgm_t *rgm) {
  if (!rgm) return;
  for (uint32_t i = 0; i < rgm->max_entries; i++) {
    epa_rgm_entry_t *e = &rgm->entries[i];
    if (!rgm_entry_is_published(e)) continue;
    free(e->bytes);
    e->bytes = NULL;
    rgm_entry_unpublish(e);
    e->name_uid = 0u;
    e->size_bytes = 0u;
    e->generation++;
  }
  rgm->live_count = 0u;
}

void epa_rgm_destroy(epa_rgm_t *rgm) {
  if (!rgm) return;
  epa_rgm_reset(rgm);
  free(rgm->entries);
  free(rgm);
}

static epa_rgm_err_t rgm_find_by_name(epa_rgm_t *rgm, uint64_t name_uid, uint32_t *out_idx) {
  if (!rgm || !out_idx) return EPA_RGM_ERR_NULL;
  for (uint32_t i = 0; i < rgm->max_entries; i++) {
    epa_rgm_entry_t *e = &rgm->entries[i];
    if (rgm_entry_is_published(e) && e->name_uid == name_uid) {
      *out_idx = i;
      return EPA_RGM_OK;
    }
  }
  return EPA_RGM_ERR_NOT_FOUND;
}

static epa_rgm_err_t rgm_find_free(epa_rgm_t *rgm, uint32_t *out_idx) {
  if (!rgm || !out_idx) return EPA_RGM_ERR_NULL;
  for (uint32_t i = 0; i < rgm->max_entries; i++) {
    if (!rgm_entry_is_published(&rgm->entries[i])) {
      *out_idx = i;
      return EPA_RGM_OK;
    }
  }
  return EPA_RGM_ERR_FULL;
}

epa_rgm_err_t epa_rgm_validate(epa_rgm_t *rgm, epa_rgm_handle_t h) {
  uint32_t idx;
  uint32_t gen;
  epa_rgm_entry_t *e;
  if (!rgm) return EPA_RGM_ERR_NULL;
  idx = epa_rgm_handle_index(h);
  gen = epa_rgm_handle_gen(h);
  if (idx >= rgm->max_entries) return EPA_RGM_ERR_BAD_HANDLE;
  e = &rgm->entries[idx];
  if (!rgm_entry_is_published(e) || e->generation != gen) return EPA_RGM_ERR_BAD_HANDLE;
  return EPA_RGM_OK;
}

epa_rgm_err_t epa_rgm_publish_copy(epa_rgm_t *rgm,
                                   uint64_t name_uid,
                                   const void *bytes,
                                   uint32_t size_bytes,
                                   epa_rgm_handle_t *out_handle) {
  uint32_t idx;
  uint8_t *copy;
  epa_rgm_entry_t *e;
  epa_rgm_err_t found;
  if (!rgm || !bytes || !out_handle) return EPA_RGM_ERR_NULL;
  if (name_uid == 0u || size_bytes == 0u) return EPA_RGM_ERR_BAD_SIZE;

  pthread_mutex_lock(&g_rgm_mu);
  found = rgm_find_by_name(rgm, name_uid, &idx);
  if (found == EPA_RGM_OK) {
    e = &rgm->entries[idx];
    if (e->size_bytes == size_bytes && memcmp(e->bytes, bytes, size_bytes) == 0) {
      *out_handle = epa_rgm_make_handle(idx, e->generation);
      pthread_mutex_unlock(&g_rgm_mu);
      return EPA_RGM_OK;
    }
    pthread_mutex_unlock(&g_rgm_mu);
    return EPA_RGM_ERR_DUPLICATE;
  }
  if (found != EPA_RGM_ERR_NOT_FOUND) {
    pthread_mutex_unlock(&g_rgm_mu);
    return found;
  }

  found = rgm_find_free(rgm, &idx);
  if (found != EPA_RGM_OK) {
    pthread_mutex_unlock(&g_rgm_mu);
    return found;
  }

  copy = (uint8_t*)malloc(size_bytes);
  if (!copy) {
    pthread_mutex_unlock(&g_rgm_mu);
    return EPA_RGM_ERR_OOM;
  }
  memcpy(copy, bytes, size_bytes);

  e = &rgm->entries[idx];
  e->name_uid = name_uid;
  e->size_bytes = size_bytes;
  e->bytes = copy;
  rgm_entry_publish(e);
  *out_handle = epa_rgm_make_handle(idx, e->generation);
  rgm->live_count++;
  pthread_mutex_unlock(&g_rgm_mu);
  return EPA_RGM_OK;
}

epa_rgm_err_t epa_rgm_get_by_name(epa_rgm_t *rgm,
                                  uint64_t name_uid,
                                  epa_rgm_handle_t *out_handle) {
  uint32_t idx;
  epa_rgm_err_t rc;
  if (!rgm || !out_handle) return EPA_RGM_ERR_NULL;
  pthread_mutex_lock(&g_rgm_mu);
  rc = rgm_find_by_name(rgm, name_uid, &idx);
  if (rc != EPA_RGM_OK) {
    pthread_mutex_unlock(&g_rgm_mu);
    return rc;
  }
  *out_handle = epa_rgm_make_handle(idx, rgm->entries[idx].generation);
  pthread_mutex_unlock(&g_rgm_mu);
  return EPA_RGM_OK;
}

epa_rgm_err_t epa_rgm_get_meta(epa_rgm_t *rgm,
                               epa_rgm_handle_t h,
                               epa_rgm_meta_t *out_meta) {
  uint32_t idx;
  epa_rgm_entry_t *e;
  epa_rgm_err_t rc;
  if (!out_meta) return EPA_RGM_ERR_NULL;
  rc = epa_rgm_validate(rgm, h);
  if (rc != EPA_RGM_OK) return rc;
  idx = epa_rgm_handle_index(h);
  e = &rgm->entries[idx];
  out_meta->name_uid = e->name_uid;
  out_meta->size_bytes = e->size_bytes;
  out_meta->generation = e->generation;
  return EPA_RGM_OK;
}

epa_rgm_err_t epa_rgm_read_bytes(epa_rgm_t *rgm,
                                 epa_rgm_handle_t h,
                                 uint32_t offset,
                                 void *dst,
                                 uint32_t len) {
  uint32_t idx;
  epa_rgm_entry_t *e;
  epa_rgm_err_t rc;
  if (!dst && len != 0u) return EPA_RGM_ERR_NULL;
  rc = epa_rgm_validate(rgm, h);
  if (rc != EPA_RGM_OK) return rc;
  idx = epa_rgm_handle_index(h);
  e = &rgm->entries[idx];
  if (offset > e->size_bytes || len > e->size_bytes - offset) return EPA_RGM_ERR_BOUNDS;
  if (len != 0u) memcpy(dst, e->bytes + offset, len);
  return EPA_RGM_OK;
}
