// epa_unit_runner.c
// Build: see unittests/Makefile (E compiler sources are linked in)
//
// Usage:
//   ./epa_unit_runner /path/to/libepa_kernel.so /path/to/tests_dir [max_ticks]
//
// Accepts both .epaasm and .e files. .e files are compiled in-process.
// A test PASSES only if epa_kernel_run() returns 1 and no TRAP/EXCEPT fired.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <unistd.h>

#include "e_compiler.h"
#include "epa_asm_compiler.h"

#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

// --- Mirror minimal public-facing structs from epa_kernel_so.h / VM side ---
// epa_instruct_common.c uses: at->block_type, at->block_id, at->rel_pc
typedef struct {
  uint32_t block_type;
  uint32_t block_id;
  uint32_t rel_pc;
} EpaEip;

// Forward-declare opaque kernel handle type
typedef struct EpaKernel EpaKernel;
typedef struct EpaAtBatch EpaAtBatch;
typedef struct epa_ghs_t epa_ghs_t;
typedef uint64_t epa_ghs_handle_t;

#define EPA_TEST_AT_SUB_ID 0x7A11u
#define EPA_TEST_MATH_AT_SUB_ID 0x7A12u

typedef enum {
  EPA_KDBG_BREAK  = 1,
  EPA_KDBG_TRAP   = 2,
  EPA_KDBG_EXCEPT = 3,
  EPA_KDBG_SIGNAL = 4,
} EpaKernelDbgKind;

typedef int (*EpaAtExecEntry)(
  EpaAtBatch *b,
  uint32_t vtid,
  int32_t tid,
  epa_ghs_t *ghs,
  epa_ghs_handle_t h
);

// Unified debug callback signature used by kernel (single callback)
typedef void (*EpaKernelDbgCallback)(
  void *user,
  EpaKernelDbgKind kind,
  uint8_t wid,
  uint32_t code,
  const EpaEip *at,
  const char *msg
);

// --- dlsym function pointer types (from epa_kernel_so.h) ---
typedef EpaKernel* (*fp_epa_kernel_create)(char err[EPA_MAX_ERR]);
typedef void      (*fp_epa_kernel_destroy)(EpaKernel *k);
typedef int       (*fp_epa_kernel_load_blob)(EpaKernel *k, const uint8_t *blob, size_t blob_len, char err[EPA_MAX_ERR]);
typedef int       (*fp_epa_kernel_run)(EpaKernel *k, uint32_t max_ticks, int debug, char err[EPA_MAX_ERR]);
typedef int       (*fp_epa_ghs_get_ptr)(epa_ghs_t *ghs, epa_ghs_handle_t h, void **out_ptr);

// NEW: use set_debug_callback instead of set_debug_hooks
typedef void      (*fp_epa_kernel_set_debug_callback)(EpaKernel *k, EpaKernelDbgCallback cb, void *cb_user);
typedef int       (*fp_epa_at_router_register)(uint32_t sub_id, EpaAtExecEntry fn);

static _Atomic uint32_t g_test_at_hits = 0;
static _Atomic uint32_t g_test_math_at_hits = 0;
static fp_epa_ghs_get_ptr g_epa_ghs_get_ptr = NULL;

static int unit_test_at_exec(EpaAtBatch *b,
                             uint32_t vtid,
                             int32_t tid,
                             epa_ghs_t *ghs,
                             epa_ghs_handle_t h) {
  (void)b;
  (void)vtid;
  (void)tid;
  (void)ghs;
  (void)h;
  atomic_fetch_add(&g_test_at_hits, 1u);
  return 1;
}

static uint32_t math_partial_for_vtid(uint32_t vtid) {
  // Sum_{i=1..500} ((vtid+1)*i^2 + 3*i + 7)
  const uint32_t sum_i = 125250u;
  const uint32_t sum_i2 = 41791750u;
  return (vtid + 1u) * sum_i2 + 3u * sum_i + 7u * 500u;
}

static int unit_test_math_at_exec(EpaAtBatch *b,
                                  uint32_t vtid,
                                  int32_t tid,
                                  epa_ghs_t *ghs,
                                  epa_ghs_handle_t h) {
  (void)b;
  (void)tid;
  if (!ghs || !g_epa_ghs_get_ptr) return 0;

  void *base = NULL;
  if (g_epa_ghs_get_ptr(ghs, h, &base) != 0 || !base) return 0;
  ((uint32_t*)base)[vtid] = math_partial_for_vtid(vtid);
  atomic_fetch_add(&g_test_math_at_hits, 1u);
  return 1;
}

// --- Event capture ---
typedef struct {
  int trap_count;
  int except_count;

  uint8_t  last_trap_wid;
  uint32_t last_trap_code;
  EpaEip   last_trap_at;

  uint8_t  last_except_wid;
  uint32_t last_except_code;
  EpaEip   last_except_at;

  // If callback provides supplemental text via err, keep last seen
  char last_hook_err[EPA_MAX_ERR];
} TestEvents;

static void unit_dbg_cb(void *user,
                        EpaKernelDbgKind kind,
                        uint8_t wid,
                        uint32_t code,
                        const EpaEip *at,
                        const char *msg) {
  TestEvents *ev = (TestEvents*)user;

  if (kind == EPA_KDBG_TRAP) {
    ev->trap_count++;
    ev->last_trap_wid = wid;
    ev->last_trap_code = code;
    if (at) ev->last_trap_at = *at;
  } else if (kind == EPA_KDBG_EXCEPT) {
    ev->except_count++;
    ev->last_except_wid = wid;
    ev->last_except_code = code;
    if (at) ev->last_except_at = *at;
  }

  if (msg) {
    strncpy(ev->last_hook_err, msg, EPA_MAX_ERR - 1);
    ev->last_hook_err[EPA_MAX_ERR - 1] = '\0';
  }
}

static int ends_with(const char *s, const char *suffix) {
  size_t ls = strlen(s), lx = strlen(suffix);
  if (lx > ls) return 0;
  return memcmp(s + (ls - lx), suffix, lx) == 0;
}

static int is_regular_file(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) return 0;
  return S_ISREG(st.st_mode);
}

static int cmp_strptr(const void *a, const void *b) {
  const char * const *sa = (const char * const *)a;
  const char * const *sb = (const char * const *)b;
  return strcmp(*sa, *sb);
}

static void* must_dlsym(void *so, const char *name) {
  dlerror();
  void *p = dlsym(so, name);
  const char *e = dlerror();
  if (!p || e) {
    fprintf(stderr, "dlsym failed for %s: %s\n", name, e ? e : "unknown");
    exit(2);
  }
  return p;
}

static void print_event_summary(const TestEvents *ev) {
  if (ev->trap_count > 0) {
    fprintf(stderr,
      "  TRAP: count=%d last(wid=%u code=%u at type=%u id=%u pc=%u)\n",
      ev->trap_count,
      (unsigned)ev->last_trap_wid,
      (unsigned)ev->last_trap_code,
      (unsigned)ev->last_trap_at.block_type,
      (unsigned)ev->last_trap_at.block_id,
      (unsigned)ev->last_trap_at.rel_pc
    );
  }
  if (ev->except_count > 0) {
    fprintf(stderr,
      "  EXCEPT: count=%d last(wid=%u code=%u at type=%u id=%u pc=%u)\n",
      ev->except_count,
      (unsigned)ev->last_except_wid,
      (unsigned)ev->last_except_code,
      (unsigned)ev->last_except_at.block_type,
      (unsigned)ev->last_except_at.block_id,
      (unsigned)ev->last_except_at.rel_pc
    );
  }
  if (ev->last_hook_err[0]) {
    fprintf(stderr, "  last_hook_err: %s\n", ev->last_hook_err);
  }
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr,
      "Usage: %s /path/to/libepa_kernel.so /path/to/tests_dir [max_ticks]\n",
      argv[0]
    );
    return 2;
  }

  const char *so_path = argv[1];
  const char *dir_path = argv[2];
  uint32_t max_ticks = 5u * 1000u * 1000u; // default
  if (argc >= 4) {
    unsigned long long v = strtoull(argv[3], NULL, 10);
    if (v == 0 || v > 0xFFFFFFFFull) {
      fprintf(stderr, "Invalid max_ticks: %s\n", argv[3]);
      return 2;
    }
    max_ticks = (uint32_t)v;
  }

  void *so = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
  if (!so) {
    fprintf(stderr, "dlopen failed: %s\n", dlerror());
    return 2;
  }

  fp_epa_kernel_create  epa_kernel_create  = (fp_epa_kernel_create)must_dlsym(so, "epa_kernel_create");
  fp_epa_kernel_destroy epa_kernel_destroy = (fp_epa_kernel_destroy)must_dlsym(so, "epa_kernel_destroy");
  fp_epa_kernel_load_blob epa_kernel_load_blob = (fp_epa_kernel_load_blob)must_dlsym(so, "epa_kernel_load_blob");
  fp_epa_kernel_run     epa_kernel_run     = (fp_epa_kernel_run)must_dlsym(so, "epa_kernel_run");

  fp_epa_kernel_set_debug_callback epa_kernel_set_debug_callback =
    (fp_epa_kernel_set_debug_callback)must_dlsym(so, "epa_kernel_set_debug_callback");
  fp_epa_at_router_register epa_at_router_register =
    (fp_epa_at_router_register)must_dlsym(so, "epa_at_router_register");
  g_epa_ghs_get_ptr = (fp_epa_ghs_get_ptr)must_dlsym(so, "epa_ghs_get_ptr");

  if (!epa_at_router_register(EPA_TEST_AT_SUB_ID, unit_test_at_exec)) {
    fprintf(stderr, "epa_at_router_register failed for sub_id=%u\n", (unsigned)EPA_TEST_AT_SUB_ID);
    dlclose(so);
    return 2;
  }
  if (!epa_at_router_register(EPA_TEST_MATH_AT_SUB_ID, unit_test_math_at_exec)) {
    fprintf(stderr, "epa_at_router_register failed for sub_id=%u\n", (unsigned)EPA_TEST_MATH_AT_SUB_ID);
    dlclose(so);
    return 2;
  }

  DIR *d = opendir(dir_path);
  if (!d) {
    fprintf(stderr, "opendir(%s) failed: %s\n", dir_path, strerror(errno));
    dlclose(so);
    return 2;
  }

  // Collect *.epaasm and *.e
  size_t cap = 64, n = 0;
  char **files = (char**)calloc(cap, sizeof(char*));
  if (!files) {
    fprintf(stderr, "OOM\n");
    closedir(d);
    dlclose(so);
    return 2;
  }

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.') continue;
    if (!ends_with(de->d_name, ".epaasm") && !ends_with(de->d_name, ".e")) continue;

    // Build full path
    size_t need = strlen(dir_path) + 1 + strlen(de->d_name) + 1;
    char *full = (char*)malloc(need);
    if (!full) { fprintf(stderr, "OOM\n"); return 2; }
    snprintf(full, need, "%s/%s", dir_path, de->d_name);

    if (!is_regular_file(full)) {
      free(full);
      continue;
    }

    if (n == cap) {
      cap *= 2;
      char **nf = (char**)realloc(files, cap * sizeof(char*));
      if (!nf) { fprintf(stderr, "OOM\n"); return 2; }
      files = nf;
    }
    files[n++] = full;
  }
  closedir(d);

  if (n == 0) {
    fprintf(stderr, "No .epaasm or .e files found in %s\n", dir_path);
    free(files);
    dlclose(so);
    return 1;
  }

  qsort(files, n, sizeof(char*), cmp_strptr);

  int pass = 0, fail = 0;

  for (size_t i = 0; i < n; i++) {
    const char *path = files[i];
    int is_e = ends_with(path, ".e");

    int expects_test_at = ends_with(path, "at_entry.epaasm");
    int expects_math_at = ends_with(path, "at_math_lifecycle.epaasm");
    if (expects_test_at) atomic_store(&g_test_at_hits, 0u);
    if (expects_math_at) atomic_store(&g_test_math_at_hits, 0u);

    char err[EPA_MAX_ERR];
    err[0] = '\0';

    /* Compile to binary blob fully in-memory */
    char     *epaasm = NULL;
    uint8_t  *blob   = NULL;
    size_t    blob_len = 0;

    if (is_e) {
      epaasm = e_compile_file_to_epaasm(path, err);
      if (!epaasm) {
        fprintf(stderr, "[FAIL] %s\n  e_compile: %s\n", path, err);
        fail++;
        continue;
      }
      blob = epa_asm_compile_src(epaasm, &blob_len, err);
      free(epaasm); epaasm = NULL;
    } else {
      blob = epa_asm_compile_file(path, &blob_len, err);
    }

    if (!blob) {
      fprintf(stderr, "[FAIL] %s\n  asm_compile: %s\n", path, err);
      fail++;
      continue;
    }

    EpaKernel *k = epa_kernel_create(err);
    if (!k) {
      fprintf(stderr, "[FAIL] %s\n  kernel_create: %s\n", path, err);
      free(blob);
      fail++;
      continue;
    }

    TestEvents ev;
    memset(&ev, 0, sizeof(ev));

    epa_kernel_set_debug_callback(k, unit_dbg_cb, &ev);

    if (!epa_kernel_load_blob(k, blob, blob_len, err)) {
      fprintf(stderr, "[FAIL] %s\n  load_blob: %s\n", path, err);
      print_event_summary(&ev);
      epa_kernel_destroy(k);
      free(blob);
      fail++;
      continue;
    }
    free(blob); blob = NULL;

    int ok = epa_kernel_run(k, max_ticks, 0, err);

    // Test policy:
    // - Any TRAP/EXCEPT is a failure.
    // - run() must return 1 for clean halt.
    int test_ok = (ok == 1) && (ev.trap_count == 0) && (ev.except_count == 0);
    if (expects_test_at && atomic_load(&g_test_at_hits) != 3u) {
      test_ok = 0;
      snprintf(err, EPA_MAX_ERR, "expected 3 AT hits, got %u", (unsigned)atomic_load(&g_test_at_hits));
    }
    if (expects_math_at && atomic_load(&g_test_math_at_hits) != 4u) {
      test_ok = 0;
      snprintf(err, EPA_MAX_ERR, "expected 4 math AT hits, got %u", (unsigned)atomic_load(&g_test_math_at_hits));
    }

    if (test_ok) {
      printf("[PASS] %s\n", path);
      pass++;
    } else {
      fprintf(stderr, "[FAIL] %s\n", path);
      fprintf(stderr, "  run_ret=%d err=%s\n", ok, err[0] ? err : "(none)");
      print_event_summary(&ev);
      fail++;
    }

    epa_kernel_destroy(k);
  }

  for (size_t i = 0; i < n; i++) free(files[i]);
  free(files);

  printf("\nSummary: %d passed, %d failed (total %zu)\n", pass, fail, n);

  dlclose(so);

  // Conventional exit code: 0 if all pass, 1 otherwise
  return (fail == 0) ? 0 : 1;
}
