// epa_ghs.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------
// Handle format: 64-bit
//   hi32: generation
//   lo32: index
// ---------------------------
typedef uint64_t epa_ghs_handle_t;

static inline uint32_t epa_ghs_handle_index(epa_ghs_handle_t h) { return (uint32_t)(h & 0xFFFFFFFFu); }
static inline uint32_t epa_ghs_handle_gen  (epa_ghs_handle_t h) { return (uint32_t)(h >> 32); }
static inline epa_ghs_handle_t epa_ghs_make_handle(uint32_t lo_idx, uint32_t hi_gen) {
    return ((epa_ghs_handle_t)hi_gen << 32) | (epa_ghs_handle_t)lo_idx;
}

// Simple object types (extend as needed).
typedef enum epa_ghs_type_e {
  EPA_GHS_T_NONE   = 0,
  EPA_GHS_T_BYTES  = 1,
  EPA_GHS_T_I32    = 2,
  EPA_GHS_T_F32    = 3,
  EPA_GHS_T_PIXELS = 4,
  EPA_GHS_T_STRING = 5,
} epa_ghs_type_t;

typedef enum epa_ghs_err_e {
  EPA_GHS_OK = 0,
  EPA_GHS_ERR_NULL = -1,
  EPA_GHS_ERR_OOM = -2,
  EPA_GHS_ERR_FULL = -3,
  EPA_GHS_ERR_BAD_HANDLE = -4,
  EPA_GHS_ERR_BAD_SIZE = -5,
  EPA_GHS_EINVAL = -6,
  EPA_GHS_EBOUNDS = -7
} epa_ghs_err_t;

// Metadata exposed to VM/opcodes (security is NOT enforced here).
typedef struct epa_ghs_meta_t {
  epa_ghs_type_t type;
  uint32_t owner;       // slot id, e.g. 0 = kernel
  uint32_t flags;       // reserved
  uint32_t size_bytes;  // payload size
  uint32_t capacity;    // allocated capacity
  uint32_t generation;  // current gen in table
} epa_ghs_meta_t;

// Entry flags (internal-ish but sometimes useful for debugging).
// Entry flags (internal-ish but sometimes useful for debugging).
enum {
  EPA_GHS_F_IN_USE = 1u << 0,

  // Payload may be read by any worker regardless of owner.
  // Writes / resizes / frees are still owner-controlled at the opcode/kernel layer.
  EPA_GHS_F_PUBLIC_READ = 1u << 1,

  // Entry is part of the kernel-managed global cache.
  // Semantics (enforced by kernel, not by GHS):
  //  - Any worker may read at any time (PUBLIC_READ implied).
  //  - Only the kernel may update, and only at a global quiescent point
  //    (e.g. all workers in WAIT_SYNC). If not quiescent, the kernel
  //    should queue the update and apply it later.
  EPA_GHS_F_GLOBAL_CACHE = 1u << 2,
};

// Mask of all public flags (excluding IN_USE).
#define EPA_GHS_F_PUBLIC_MASK (EPA_GHS_F_PUBLIC_READ | EPA_GHS_F_GLOBAL_CACHE)

// Allocator hooks (optional). If NULL, module uses malloc/free in .c
typedef void* (*epa_ghs_alloc_fn)(size_t bytes, void* user);
typedef void  (*epa_ghs_free_fn)(void* ptr, void* user);

// Global Handle Space
typedef struct epa_ghs_entry_t {
  uint32_t flags;       // EPA_GHS_F_IN_USE
  uint32_t generation;  // bumps on free
  epa_ghs_type_t type;
  uint32_t owner;
  uint32_t size_bytes;
  uint32_t capacity;
  uint32_t tag;   // NEW: user-defined identity tag
  void*    ptr;
} epa_ghs_entry_t;

typedef struct epa_ghs_t {
  uint32_t max_entries;
  uint32_t live_count;
  epa_ghs_entry_t* entries;

  epa_ghs_alloc_fn alloc_fn;
  epa_ghs_free_fn  free_fn;
  void* user;
} epa_ghs_t;

// Create/destroy
epa_ghs_t* epa_ghs_create(uint32_t max_entries,
                         epa_ghs_alloc_fn a, epa_ghs_free_fn f, void* user);
void       epa_ghs_destroy(epa_ghs_t* ghs);

// Reset table (frees all live objects)
void epa_ghs_reset(epa_ghs_t* ghs);

// Core ops (NO SECURITY ENFORCEMENT)
// - alloc returns handle with (idx, gen)
// - free invalidates handle by incrementing generation
epa_ghs_err_t epa_ghs_alloc(epa_ghs_t* ghs,
                           epa_ghs_type_t type,
                           uint32_t owner,
                           uint32_t size_bytes,
                           epa_ghs_handle_t* out_handle);

epa_ghs_err_t epa_ghs_alloc_tagged(epa_ghs_t *ghs, epa_ghs_type_t type, uint32_t owner,
                        uint32_t bytes, uint32_t tag, epa_ghs_handle_t *out_h);

epa_ghs_err_t epa_ghs_free(epa_ghs_t* ghs, epa_ghs_handle_t h);

// Transfer ownership (does not check "current owner" – opcode layer should)
epa_ghs_err_t epa_ghs_transfer(epa_ghs_t* ghs, epa_ghs_handle_t h, uint32_t new_owner);

// Resize (optional). Keeps same handle/index/gen.
// Does not enforce owner security.
epa_ghs_err_t epa_ghs_resize(epa_ghs_t* ghs, epa_ghs_handle_t h, uint32_t new_size_bytes);

// Accessors
epa_ghs_err_t epa_ghs_get_meta(epa_ghs_t* ghs, epa_ghs_handle_t h, epa_ghs_meta_t* out_meta);

// Returns pointer to payload (host mode). In GPU mode this would become an address/offset.
// No security checks.
epa_ghs_err_t epa_ghs_get_ptr(epa_ghs_t* ghs, epa_ghs_handle_t h, void** out_ptr);

// Copy bytes out of a handle payload into a caller buffer.
// Validates handle + bounds-checks (offset + len) <= size_bytes.
epa_ghs_err_t epa_ghs_read_bytes(epa_ghs_t* ghs,
                                epa_ghs_handle_t h,
                                uint32_t offset,
                                void* dst,
                                uint32_t len);

epa_ghs_err_t epa_ghs_write_bytes(epa_ghs_t* ghs,
                                 epa_ghs_handle_t h,
                                 uint32_t offset,
                                 const void* src,
                                 uint32_t len);

// Validation helper (checks handle index and generation + in_use)
epa_ghs_err_t epa_ghs_validate(epa_ghs_t* ghs, epa_ghs_handle_t h);

// Debug/stats
uint32_t epa_ghs_capacity(const epa_ghs_t* ghs);
uint32_t epa_ghs_live_count(const epa_ghs_t* ghs);


epa_ghs_err_t epa_ghs_get_flags(epa_ghs_t* ghs, epa_ghs_handle_t h, uint8_t* flags);
// Update entry flags.
//
// Notes:
// - GHS does not enforce security/ownership. Flags are metadata used by the
//   kernel/opcode layer.
// - Setting GLOBAL_CACHE will implicitly set PUBLIC_READ.
// - Clearing PUBLIC_READ will also clear GLOBAL_CACHE.
epa_ghs_err_t epa_ghs_update_flags(epa_ghs_t* ghs,
                                  epa_ghs_handle_t h,
                                  uint32_t set_bits,
                                  uint32_t clear_bits);

int epa_ghs_set_tag(epa_ghs_t *ghs, epa_ghs_handle_t h, uint32_t tag);
int epa_ghs_get_tag(epa_ghs_t *ghs, epa_ghs_handle_t h, uint32_t *out_tag);


#ifdef __cplusplus
}
#endif
