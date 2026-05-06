// epa_ghs.c
#include "epa_ghs.h"
#include <stdlib.h>
#include <string.h>

#define EPA_GHS_RETURN_IF_ERR(x) if (x!=EPA_GHS_OK) return x;

static void* ghs_default_alloc(size_t bytes, void* user) {
  (void)user;
  return malloc(bytes);
}
static void ghs_default_free(void* p, void* user) {
  (void)user;
  free(p);
}

epa_ghs_t* epa_ghs_create(uint32_t max_entries,
                         epa_ghs_alloc_fn a, epa_ghs_free_fn f, void* user) {
  if (max_entries == 0) return NULL;

  epa_ghs_t* g = (epa_ghs_t*)malloc(sizeof(epa_ghs_t));
  if (!g) return NULL;
  memset(g, 0, sizeof(*g));

  g->max_entries = max_entries;
  g->alloc_fn = a ? a : ghs_default_alloc;
  g->free_fn  = f ? f : ghs_default_free;
  g->user = user;

  g->entries = (epa_ghs_entry_t*)calloc(max_entries, sizeof(epa_ghs_entry_t));
  if (!g->entries) {
    free(g);
    return NULL;
  }

  // generation starts at 1 so "0" can be reserved if you want
  for (uint32_t i = 0; i < max_entries; i++) {
    g->entries[i].generation = 1u;
  }
  return g;
}

void epa_ghs_destroy(epa_ghs_t* ghs) {
  if (!ghs) return;
  epa_ghs_reset(ghs);
  free(ghs->entries);
  free(ghs);
}

void epa_ghs_reset(epa_ghs_t* ghs) {
  if (!ghs) return;
  for (uint32_t i = 0; i < ghs->max_entries; i++) {
    epa_ghs_entry_t* e = &ghs->entries[i];
    if (e->flags & EPA_GHS_F_IN_USE) {
      if (e->ptr) ghs->free_fn(e->ptr, ghs->user);
      e->ptr = NULL;
      e->flags = 0;
      e->type = EPA_GHS_T_NONE;
      e->owner = 0;
      e->size_bytes = 0;
      e->capacity = 0;
      e->tag = 0;
      // invalidate old handles
      e->generation++;
    }
  }
  ghs->live_count = 0;
}

static epa_ghs_err_t ghs_find_free_slot(epa_ghs_t* ghs, uint32_t* out_idx) {
  if (!ghs || !out_idx) return EPA_GHS_ERR_NULL;
  for (uint32_t i = 0; i < ghs->max_entries; i++) {
    if ((ghs->entries[i].flags & EPA_GHS_F_IN_USE) == 0) {
      *out_idx = i;
      return EPA_GHS_OK;
    }
  }
  return EPA_GHS_ERR_FULL;
}

epa_ghs_err_t epa_ghs_validate(epa_ghs_t* ghs, epa_ghs_handle_t h) {
  if (!ghs) return EPA_GHS_ERR_NULL;
  uint32_t idx = epa_ghs_handle_index(h);
  uint32_t gen = epa_ghs_handle_gen(h);
  if (idx >= ghs->max_entries) return EPA_GHS_ERR_BAD_HANDLE;

  epa_ghs_entry_t* e = &ghs->entries[idx];
  if ((e->flags & EPA_GHS_F_IN_USE) == 0) return EPA_GHS_ERR_BAD_HANDLE;
  if (e->generation != gen) return EPA_GHS_ERR_BAD_HANDLE;
  return EPA_GHS_OK;
}

epa_ghs_err_t epa_ghs_alloc(epa_ghs_t* ghs,
                           epa_ghs_type_t type,
                           uint32_t owner,
                           uint32_t size_bytes,
                           epa_ghs_handle_t* out_handle) {
  if (!ghs || !out_handle) return EPA_GHS_ERR_NULL;
  if (size_bytes == 0) return EPA_GHS_ERR_BAD_SIZE;

  uint32_t idx = 0;
  epa_ghs_err_t err = ghs_find_free_slot(ghs, &idx);
  if (err != EPA_GHS_OK) return err;

  void* p = ghs->alloc_fn((size_t)size_bytes, ghs->user);
  if (!p) return EPA_GHS_ERR_OOM;

  epa_ghs_entry_t* e = &ghs->entries[idx];
  e->flags = EPA_GHS_F_IN_USE;
  e->type = type;
  e->owner = owner;
  e->size_bytes = size_bytes;
  e->capacity = size_bytes;
  e->ptr = p;

  // Important: handle uses current generation
  *out_handle = ((uint64_t)e->generation << 32) | (uint64_t)idx;

  ghs->live_count++;
  return EPA_GHS_OK;
}

int epa_ghs_alloc_tagged(epa_ghs_t *ghs, epa_ghs_type_t type, uint32_t owner,
                        uint32_t bytes, uint32_t tag, epa_ghs_handle_t *out_h)
{
    int rc = epa_ghs_alloc(ghs, type, owner, bytes, out_h);
    if (rc != EPA_GHS_OK) return rc;
    return epa_ghs_set_tag(ghs, *out_h, tag);
}

epa_ghs_err_t epa_ghs_free(epa_ghs_t* ghs, epa_ghs_handle_t h) {
  if (!ghs) return EPA_GHS_ERR_NULL;
  epa_ghs_err_t v = epa_ghs_validate(ghs, h);
  if (v != EPA_GHS_OK) return v;

  uint32_t idx = epa_ghs_handle_index(h);
  epa_ghs_entry_t* e = &ghs->entries[idx];

  if (e->ptr) {
    ghs->free_fn(e->ptr, ghs->user);
    e->ptr = NULL;
  }
  e->flags = 0;
  e->type = EPA_GHS_T_NONE;
  e->owner = 0;
  e->size_bytes = 0;
  e->capacity = 0;

  // Invalidate old handles
  e->generation++;

  if (ghs->live_count > 0) ghs->live_count--;
  return EPA_GHS_OK;
}

epa_ghs_err_t epa_ghs_transfer(epa_ghs_t* ghs, epa_ghs_handle_t h, uint32_t new_owner) {
  if (!ghs) return EPA_GHS_ERR_NULL;
  epa_ghs_err_t v = epa_ghs_validate(ghs, h);
  if (v != EPA_GHS_OK) return v;

  uint32_t idx = epa_ghs_handle_index(h);
  epa_ghs_entry_t* e = &ghs->entries[idx];
  e->owner = new_owner;
  return EPA_GHS_OK;
}

epa_ghs_err_t epa_ghs_resize(epa_ghs_t* ghs, epa_ghs_handle_t h, uint32_t new_size_bytes) {
  if (!ghs) return EPA_GHS_ERR_NULL;
  if (new_size_bytes == 0) return EPA_GHS_ERR_BAD_SIZE;

  epa_ghs_err_t v = epa_ghs_validate(ghs, h);
  if (v != EPA_GHS_OK) return v;

  uint32_t idx = epa_ghs_handle_index(h);
  epa_ghs_entry_t* e = &ghs->entries[idx];

  if (new_size_bytes <= e->capacity) {
    e->size_bytes = new_size_bytes;
    return EPA_GHS_OK;
  }

  // grow
  void* np = ghs->alloc_fn((size_t)new_size_bytes, ghs->user);
  if (!np) return EPA_GHS_ERR_OOM;

  // copy old content
  if (e->ptr && e->size_bytes) {
    memcpy(np, e->ptr, (size_t)e->size_bytes);
  }

  // free old
  if (e->ptr) ghs->free_fn(e->ptr, ghs->user);

  e->ptr = np;
  e->capacity = new_size_bytes;
  e->size_bytes = new_size_bytes;
  return EPA_GHS_OK;
}

epa_ghs_err_t epa_ghs_get_meta(epa_ghs_t* ghs, epa_ghs_handle_t h, epa_ghs_meta_t* out_meta) {
  if (!ghs || !out_meta) return EPA_GHS_ERR_NULL;
  epa_ghs_err_t v = epa_ghs_validate(ghs, h);
  if (v != EPA_GHS_OK) return v;

  uint32_t idx = epa_ghs_handle_index(h);
  epa_ghs_entry_t* e = &ghs->entries[idx];

  out_meta->type = e->type;
  out_meta->owner = e->owner;
  out_meta->flags = e->flags;
  out_meta->size_bytes = e->size_bytes;
  out_meta->capacity = e->capacity;
  out_meta->generation = e->generation;
  return EPA_GHS_OK;
}

epa_ghs_err_t epa_ghs_get_ptr(epa_ghs_t* ghs, epa_ghs_handle_t h, void** out_ptr) {
  if (!ghs || !out_ptr) return EPA_GHS_ERR_NULL;
  epa_ghs_err_t v = epa_ghs_validate(ghs, h);
  if (v != EPA_GHS_OK) return v;

  uint32_t idx = epa_ghs_handle_index(h);
  epa_ghs_entry_t* e = &ghs->entries[idx];
  *out_ptr = e->ptr;
  return EPA_GHS_OK;
}

epa_ghs_err_t epa_ghs_read_bytes(epa_ghs_t* ghs,
                                epa_ghs_handle_t h,
                                uint32_t offset,
                                void* dst,
                                uint32_t len) {
  if (!dst && len) return EPA_GHS_EINVAL;

  // Validate + get slot
  EPA_GHS_RETURN_IF_ERR(epa_ghs_validate(ghs, h));
  uint32_t idx = epa_ghs_handle_index(h);
  epa_ghs_entry_t* s = &ghs->entries[idx];

  if (offset > s->size_bytes) return EPA_GHS_EBOUNDS;
  if (len > (s->size_bytes - offset)) return EPA_GHS_EBOUNDS;

  if (len) memcpy(dst, (uint8_t*)s->ptr + offset, (size_t)len);
  return EPA_GHS_OK;
}

epa_ghs_err_t epa_ghs_write_bytes(epa_ghs_t* ghs,
                                 epa_ghs_handle_t h,
                                 uint32_t offset,
                                 const void* src,
                                 uint32_t len) {
  if (!src && len) return EPA_GHS_EINVAL;

  // Validate + get slot
  EPA_GHS_RETURN_IF_ERR(epa_ghs_validate(ghs, h));
  epa_ghs_entry_t* s = &ghs->entries[h];

  if (offset > s->size_bytes) return EPA_GHS_EBOUNDS;
  if (len > (s->size_bytes - offset)) return EPA_GHS_EBOUNDS;

  if (len) memcpy((uint8_t*)s->ptr + offset, src, (size_t)len);
  return EPA_GHS_OK;
}

uint32_t epa_ghs_capacity(const epa_ghs_t* ghs) {
  return ghs ? ghs->max_entries : 0;
}

uint32_t epa_ghs_live_count(const epa_ghs_t* ghs) {
  return ghs ? ghs->live_count : 0;
}

epa_ghs_err_t epa_ghs_update_flags(epa_ghs_t* ghs,
                                  epa_ghs_handle_t h,
                                  uint32_t set_bits,
                                  uint32_t clear_bits) {
  if (!ghs) return EPA_GHS_ERR_NULL;
  epa_ghs_err_t v = epa_ghs_validate(ghs, h);
  if (v != EPA_GHS_OK) return v;

  // Only allow modification of publicly defined flags.
  set_bits &= EPA_GHS_F_PUBLIC_MASK;
  clear_bits &= EPA_GHS_F_PUBLIC_MASK;

  // GLOBAL_CACHE implies PUBLIC_READ.
  if (set_bits & EPA_GHS_F_GLOBAL_CACHE) set_bits |= EPA_GHS_F_PUBLIC_READ;

  // If clearing PUBLIC_READ, also clear GLOBAL_CACHE.
  if (clear_bits & EPA_GHS_F_PUBLIC_READ) clear_bits |= EPA_GHS_F_GLOBAL_CACHE;

  uint32_t idx = epa_ghs_handle_index(h);
  epa_ghs_entry_t* e = &ghs->entries[idx];

  // Preserve IN_USE and any internal bits.
  uint32_t f = e->flags;
  f &= ~clear_bits;
  f |= set_bits;
  e->flags = f;
  return EPA_GHS_OK;
}

epa_ghs_err_t epa_ghs_get_flags(epa_ghs_t* ghs, epa_ghs_handle_t h, uint8_t* flags) {
  if (!ghs) return EPA_GHS_ERR_NULL;
  epa_ghs_err_t v = epa_ghs_validate(ghs, h);
  if (v != EPA_GHS_OK) return v;

  uint32_t idx = epa_ghs_handle_index(h);
  epa_ghs_entry_t* e = &ghs->entries[idx];

  *flags = e->flags;
  return EPA_GHS_OK;
}

int epa_ghs_set_tag(epa_ghs_t *ghs, epa_ghs_handle_t h, uint32_t tag) {
    if (!ghs) return EPA_GHS_ERR_BAD_HANDLE;

    uint32_t idx = epa_ghs_handle_index(h);
    epa_ghs_entry_t* e = &ghs->entries[idx];

    e->tag = tag;
    return EPA_GHS_OK;
}

int epa_ghs_get_tag(epa_ghs_t *ghs, epa_ghs_handle_t h, uint32_t *out_tag) {
    if (!ghs) return EPA_GHS_ERR_BAD_HANDLE;
    if (!out_tag) return EPA_GHS_ERR_NULL;

    uint32_t idx = epa_ghs_handle_index(h);
    epa_ghs_entry_t* e = &ghs->entries[idx];

    *out_tag = e->tag;
    return EPA_GHS_OK;
}

