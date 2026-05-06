// tools/epa_asm_repl.c
//
// A tiny interactive CLI that lets you paste EPAASM, then run `compile`.
// It compiles using your existing assembler (epa_asm_compile_file()) by
// writing the current buffer to a temporary .epaasm file.
//
// Build (from repo root; adjust include paths if needed):
//   gcc -O2 -Wall -Wextra -std=c11 \
//     -Iinclude -Isrc \
//     -o build/tools/epa_asm_repl \
//     tools/epa_asm_repl.c src/epa_asm_compiler.c -ldl
//
// Usage:
//   ./build/tools/epa_asm_repl
//
// Commands (type at beginning of a line):
//   compile [out.epl|out.epa]   - compile current buffer; optionally write binary blob to file
//   dump-blob                   - compile buffer (if needed) and dump last compiled blob (hex+ascii)
//   clear                       - clear current buffer
//   show                        - print current buffer with line numbers
//   load <file.epaasm>          - load file into buffer (replaces current buffer)
//   save <file.epaasm>          - save buffer to file
//   tests                       - list unit tests
//   test <n|name>               - select a unit test (by number or filename)
//   run-test [n|name]           - run selected test (or run provided one) via unit runner
//   run-local [ticks]           - run current buffer IN-PROCESS using libepa_kernel.so,
//                                keeping a persistent kernel instance across commands
//   ingress <wid> <data>        - queue ingress payload to wid (text or hex:...)
//   quit / exit                 - leave
//
// Everything else is treated as EPAASM text and appended to the buffer.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#include <dirent.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#include "../src/epa_asm_compiler.h" // provides epa_asm_compile_file + EPA_MAX_ERR

// For local state inspection (dump-worker)
#include "../src/epa_kernel.h"          // EpaKernel + worker array
#include "../src/vm/epa_worker_state.h" // EpaWorkerState

typedef struct {
  char test_dir[512];
  char kernel_so[512];
  char unit_runner[512];
  char run_ticks[64];
} EpaDevConfig;

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} StrBuf;

static EpaDevConfig g_cfg;

// Keep the most recently compiled blob (so REPL can dump it).
static uint8_t *g_last_blob = NULL;
static size_t   g_last_blob_len = 0;

static void epadev_defaults(EpaDevConfig *cfg) {
  strcpy(cfg->test_dir, "../../unittests/tests");
  strcpy(cfg->kernel_so, "../libepa_kernel.so");
  strcpy(cfg->unit_runner, "../unittests/epa_unit_runner");
  strcpy(cfg->run_ticks, "5000000");
}

static void trim(char *s) {
  char *p = s;
  while (isspace((unsigned char)*p)) p++;
  memmove(s, p, strlen(p) + 1);

  size_t n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static int load_dev_config(const char *path, EpaDevConfig *cfg) {
  FILE *f = fopen(path, "r");
  if (!f) return 0;

  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    trim(line);
    if (!line[0] || line[0] == '#') continue;

    char *eq = strchr(line, '=');
    if (!eq) continue;

    *eq++ = 0;
    trim(line);
    trim(eq);

    if (!strcmp(line, "EPA_TEST_DIR")) {
      strncpy(cfg->test_dir, eq, sizeof(cfg->test_dir) - 1);
      cfg->test_dir[sizeof(cfg->test_dir) - 1] = 0;
    } else if (!strcmp(line, "EPA_KERNEL_SO")) {
      strncpy(cfg->kernel_so, eq, sizeof(cfg->kernel_so) - 1);
      cfg->kernel_so[sizeof(cfg->kernel_so) - 1] = 0;
    } else if (!strcmp(line, "EPA_UNIT_RUNNER")) {
      strncpy(cfg->unit_runner, eq, sizeof(cfg->unit_runner) - 1);
      cfg->unit_runner[sizeof(cfg->unit_runner) - 1] = 0;
    } else if (!strcmp(line, "EPA_RUN_TICKS")) {
      strncpy(cfg->run_ticks, eq, sizeof(cfg->run_ticks) - 1);
      cfg->run_ticks[sizeof(cfg->run_ticks) - 1] = 0;
    }
  }

  fclose(f);
  return 1;
}

static void sb_free(StrBuf *sb) {
  free(sb->buf);
  sb->buf = NULL;
  sb->len = sb->cap = 0;
}

static void sb_clear(StrBuf *sb) {
  sb->len = 0;
  if (sb->buf) sb->buf[0] = '\0';
}

static int sb_reserve(StrBuf *sb, size_t add) {
  if (sb->len + add + 1 <= sb->cap) return 1;
  size_t ncap = sb->cap ? sb->cap : 4096;
  while (ncap < sb->len + add + 1) ncap *= 2;
  char *nb = (char*)realloc(sb->buf, ncap);
  if (!nb) return 0;
  sb->buf = nb;
  sb->cap = ncap;
  return 1;
}

static int sb_append(StrBuf *sb, const char *s) {
  size_t n = strlen(s);
  if (!sb_reserve(sb, n)) return 0;
  memcpy(sb->buf + sb->len, s, n);
  sb->len += n;
  sb->buf[sb->len] = '\0';
  return 1;
}

static int sb_append_line(StrBuf *sb, const char *line) {
  if (!sb_append(sb, line)) return 0;
  if (!sb_append(sb, "\n")) return 0;
  return 1;
}

static char *ltrim(char *s) {
  while (*s && isspace((unsigned char)*s)) s++;
  return s;
}

static void rtrim_inplace(char *s) {
  size_t n = strlen(s);
  while (n > 0 && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}

static int starts_with_word(const char *s, const char *w) {
  size_t n = strlen(w);
  if (strncmp(s, w, n) != 0) return 0;
  if (s[n] == '\0') return 1;
  return isspace((unsigned char)s[n]) != 0;
}

static void print_help(void) {
  printf(
    "EPAASM REPL\n"
    "Paste EPAASM lines. Commands:\n"
    "  compile [out.epa]      Compile buffer. If out path provided, write binary blob.\n"
    "  dump-blob              Compile buffer (if needed) and dump last compiled blob (hex+ascii).\n"
    "  clear                  Clear buffer.\n"
    "  show                   Show buffer with line numbers.\n"
    "  load <file>            Load EPAASM file into buffer (replaces buffer).\n"
    "  save <file>            Save buffer to file.\n"
    "  tests                  List unit tests\n"
    "  test <n|name>          Select a unit test (by number or filename)\n"
    "  run-test [n|name]      Run selected test (or run provided one) via unit runner\n"
    "  run                    Run current buffer via unit runner on a temp directory\n"
    "  run-local [ticks]      Run current buffer in-process (persistent kernel state)\n"
    "  ingress <wid> <data>   Queue ingress payload to wid (text or hex:...)\n"
	"  tp-init <n>            Start a test thread pool with n threads (threads park/wait)\n"
	"  tp-stat                Show thread pool status (threads + waiting count)\n"
	"  tp-stop                Stop/join the test thread pool\n"
    "  quit / exit            Exit.\n"
  );
}

static void cmd_show(const StrBuf *sb) {
  if (!sb->buf || sb->len == 0) {
    printf("(buffer empty)\n");
    return;
  }
  int line = 1;
  const char *p = sb->buf;
  const char *start = p;
  while (*p) {
    if (*p == '\n') {
      printf("%4d | %.*s\n", line, (int)(p - start), start);
      line++;
      p++;
      start = p;
    } else {
      p++;
    }
  }
  if (start != p) {
    printf("%4d | %s\n", line, start);
  }
}

static int write_file_text(const char *path, const StrBuf *sb) {
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  if (sb->len) fwrite(sb->buf, 1, sb->len, f);
  fclose(f);
  return 1;
}

static int load_file_text(const char *path, StrBuf *sb) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  sb_clear(sb);

  char tmp[4096];
  while (1) {
    size_t n = fread(tmp, 1, sizeof(tmp), f);
    if (n == 0) break;
    if (!sb_reserve(sb, n)) { fclose(f); return 0; }
    memcpy(sb->buf + sb->len, tmp, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
  }
  fclose(f);
  return 1;
}

static int write_file_bin(const char *path, const uint8_t *blob, size_t blob_len) {
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  if (blob_len) fwrite(blob, 1, blob_len, f);
  fclose(f);
  return 1;
}

static void dump_blob_pretty(const uint8_t *b, size_t len) {
  printf("\n-- BLOB DUMP (len=%zu bytes) ----------------------------\n", len);

  for (size_t i = 0; i < len; i += 16) {
    printf("%08zx: ", i);

    // hex
    for (size_t j = 0; j < 16; j++) {
      if (i + j < len) printf("%02X ", b[i + j]);
      else printf("   ");
    }

    printf(" | ");

    // ascii
    for (size_t j = 0; j < 16 && i + j < len; j++) {
      uint8_t c = b[i + j];
      putchar((c >= 32 && c < 127) ? (int)c : '.');
    }

    putchar('\n');
  }

  printf("---------------------------------------------------------\n");
}

// Compile current buffer to a blob, store in g_last_blob/g_last_blob_len.
// If compile fails, returns 0 and does not modify the existing last blob.
static int compile_buffer_to_last_blob(const StrBuf *sb, char err[EPA_MAX_ERR]) {
  if (err) err[0] = 0;
  if (!sb->buf || sb->len == 0) {
    snprintf(err, EPA_MAX_ERR, "buffer is empty");
    return 0;
  }

  // Write buffer to a temp .epaasm file
  char tmpl[] = "/tmp/epaasm_repl_XXXXXX.epaasm"; // mkstemps handles suffix
  int fd = mkstemps(tmpl, (int)strlen(".epaasm"));
  if (fd < 0) {
    snprintf(err, EPA_MAX_ERR, "mkstemps failed: %s", strerror(errno));
    return 0;
  }

  FILE *tf = fdopen(fd, "wb");
  if (!tf) {
    close(fd);
    unlink(tmpl);
    snprintf(err, EPA_MAX_ERR, "fdopen failed: %s", strerror(errno));
    return 0;
  }

  fwrite(sb->buf, 1, sb->len, tf);
  fclose(tf); // closes fd too

  size_t blob_len = 0;
  uint8_t *blob = epa_asm_compile_file(tmpl, &blob_len, err);

  unlink(tmpl);

  if (!blob) {
    if (!err[0]) snprintf(err, EPA_MAX_ERR, "compile failed");
    return 0;
  }

  // Replace last blob
  free(g_last_blob);
  g_last_blob = blob;
  g_last_blob_len = blob_len;
  return 1;
}

static int compile_buffer(const StrBuf *sb, const char *out_path_opt) {
  char err[EPA_MAX_ERR];
  if (!compile_buffer_to_last_blob(sb, err)) {
    fprintf(stderr, "compile: FAILED: %s\n", err[0] ? err : "(unknown error)");
    return 0;
  }

  printf("compile: OK (%zu bytes)\n", g_last_blob_len);

  if (out_path_opt && out_path_opt[0]) {
    if (!write_file_bin(out_path_opt, g_last_blob, g_last_blob_len)) {
      fprintf(stderr, "compile: could not write '%s': %s\n", out_path_opt, strerror(errno));
      return 0;
    }
    printf("wrote: %s\n", out_path_opt);
  }

  return 1;
}

static int cmd_dump_blob(const StrBuf *sb) {
  if (!g_last_blob || g_last_blob_len == 0) {
    char err[EPA_MAX_ERR];
    if (!compile_buffer_to_last_blob(sb, err)) {
      fprintf(stderr, "dump-blob: compile FAILED: %s\n", err[0] ? err : "(unknown error)");
      return 0;
    }
    printf("dump-blob: compiled (%zu bytes)\n", g_last_blob_len);
  }
  dump_blob_pretty(g_last_blob, g_last_blob_len);
  return 1;
}

static int run_buffer(const StrBuf *sb) {
  if (!sb->buf || sb->len == 0) {
    fprintf(stderr, "run: buffer is empty\n");
    return 0;
  }

  // temp dir
  char dtmp[] = "/tmp/epaasm_run_dir_XXXXXX";
  if (!mkdtemp(dtmp)) {
    fprintf(stderr, "run: mkdtemp failed: %s\n", strerror(errno));
    return 0;
  }

  // write buffer as a test file
  char tpath[512];
  snprintf(tpath, sizeof(tpath), "%s/repl.epaasm", dtmp);

  FILE *f = fopen(tpath, "wb");
  if (!f) {
    fprintf(stderr, "run: could not write temp file: %s\n", strerror(errno));
    rmdir(dtmp);
    return 0;
  }
  if (sb->len) fwrite(sb->buf, 1, sb->len, f);
  fwrite("\n", 1, 1, f);
  fclose(f);

  // run unit runner on that directory (it will run the single test)
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "%s %s %s %s",
          g_cfg.unit_runner, g_cfg.kernel_so, dtmp, g_cfg.run_ticks);

  printf("run: %s\n", cmd);
  int rc = system(cmd);

  unlink(tpath);
  rmdir(dtmp);

  if (rc != 0) {
    fprintf(stderr, "run: execution failed (code %d)\n", rc);
    return 0;
  }

  return 1;
}

// --------------------------
// Test listing support
// --------------------------
typedef struct {
  char  **names;     // filenames only (e.g. "foo.epaasm")
  size_t  count;
  size_t  cap;
} TestList;

static void tl_free(TestList *tl) {
  if (!tl) return;
  for (size_t i = 0; i < tl->count; i++) free(tl->names[i]);
  free(tl->names);
  tl->names = NULL;
  tl->count = tl->cap = 0;
}

static int tl_push(TestList *tl, const char *name) {
  if (tl->count + 1 > tl->cap) {
    size_t ncap = tl->cap ? tl->cap * 2 : 64;
    char **nn = (char**)realloc(tl->names, ncap * sizeof(char*));
    if (!nn) return 0;
    tl->names = nn;
    tl->cap = ncap;
  }
  tl->names[tl->count] = strdup(name);
  if (!tl->names[tl->count]) return 0;
  tl->count++;
  return 1;
}

static int ends_with(const char *s, const char *suffix) {
  size_t ns = strlen(s), nf = strlen(suffix);
  if (nf > ns) return 0;
  return memcmp(s + (ns - nf), suffix, nf) == 0;
}

static int cmp_cstr(const void *a, const void *b) {
  const char *aa = *(const char* const*)a;
  const char *bb = *(const char* const*)b;
  return strcmp(aa, bb);
}

static int load_test_list(const char *dir, TestList *out) {
  memset(out, 0, sizeof(*out));
  DIR *d = opendir(dir);
  if (!d) return 0;

  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.') continue;
    if (!ends_with(e->d_name, ".epaasm")) continue;
    if (!tl_push(out, e->d_name)) {
      closedir(d);
      tl_free(out);
      return 0;
    }
  }
  closedir(d);

  if (out->count) qsort(out->names, out->count, sizeof(char*), cmp_cstr);
  return 1;
}

static void cmd_tests(const char *dir) {
  TestList tl;
  if (!load_test_list(dir, &tl)) {
    fprintf(stderr, "tests: could not open '%s': %s\n", dir, strerror(errno));
    return;
  }

  if (tl.count == 0) {
    printf("(no tests found in %s)\n", dir);
    tl_free(&tl);
    return;
  }

  printf("Unit tests in %s:\n", dir);
  for (size_t i = 0; i < tl.count; i++) {
    printf("  %3zu) %s\n", i + 1, tl.names[i]);
  }
  tl_free(&tl);
}

// Resolve "n" (1-based) or filename to full path.
// Returns malloc'd string path on success, NULL on failure.
static char *resolve_test_path(const char *dir, const char *spec) {
  if (!spec || !*spec) return NULL;

  // If numeric -> index in sorted list
  int all_digits = 1;
  for (const char *p = spec; *p; p++) {
    if (!isdigit((unsigned char)*p)) { all_digits = 0; break; }
  }

  if (all_digits) {
    long idx = strtol(spec, NULL, 10);
    if (idx <= 0) return NULL;

    TestList tl;
    if (!load_test_list(dir, &tl)) return NULL;
    if ((size_t)idx > tl.count) {
      tl_free(&tl);
      return NULL;
    }

    const char *name = tl.names[idx - 1];
    char *full = (char*)malloc(strlen(dir) + 1 + strlen(name) + 1);
    if (full) sprintf(full, "%s/%s", dir, name);
    tl_free(&tl);
    return full;
  }

  // Otherwise treat as filename. Add .epaasm if missing.
  char tmp[512];
  if (ends_with(spec, ".epaasm")) {
    snprintf(tmp, sizeof(tmp), "%s", spec);
  } else {
    snprintf(tmp, sizeof(tmp), "%s.epaasm", spec);
  }

  char *full = (char*)malloc(strlen(dir) + 1 + strlen(tmp) + 1);
  if (!full) return NULL;
  sprintf(full, "%s/%s", dir, tmp);

  // Ensure file exists
  struct stat st;
  if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
    free(full);
    return NULL;
  }
  return full;
}

static int copy_file(const char *src, const char *dst) {
  FILE *fs = fopen(src, "rb");
  if (!fs) return 0;
  FILE *fd = fopen(dst, "wb");
  if (!fd) { fclose(fs); return 0; }

  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fs)) > 0) {
    if (fwrite(buf, 1, n, fd) != n) { fclose(fs); fclose(fd); return 0; }
  }

  fclose(fs);
  fclose(fd);
  return 1;
}

// Run a single .epaasm test file by copying it into a temp directory and invoking the unit runner.
static int run_test_file(const char *test_path) {
  if (!test_path || !*test_path) {
    fprintf(stderr, "run-test: no test selected\n");
    return 0;
  }

  // temp dir
  char dtmp[] = "/tmp/epa_ut_XXXXXX";
  if (!mkdtemp(dtmp)) {
    fprintf(stderr, "run-test: mkdtemp failed: %s\n", strerror(errno));
    return 0;
  }

  // copy test into temp dir
  char dst[512];
  snprintf(dst, sizeof(dst), "%s/test.epaasm", dtmp);
  if (!copy_file(test_path, dst)) {
    fprintf(stderr, "run-test: copy failed: %s\n", strerror(errno));
    unlink(dst);
    rmdir(dtmp);
    return 0;
  }

  // execute runner on that directory
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "%s %s %s %s",
          g_cfg.unit_runner, g_cfg.kernel_so, dtmp, g_cfg.run_ticks);

  printf("run-test: %s\n", cmd);
  int rc = system(cmd);

  // cleanup
  unlink(dst);
  rmdir(dtmp);

  if (rc != 0) {
    fprintf(stderr, "run-test: FAILED (code %d)\n", rc);
    return 0;
  }

  printf("run-test: OK\n");
  return 1;
}

// --------------------------
// Local (in-process) kernel runner (persistent state)
// --------------------------

// Forward-declare opaque EpaKernel type without pulling in full kernel headers.
// We only pass pointers across dlsym boundaries.

typedef struct {
  void *so;

  EpaKernel* (*p_create)(char err[EPA_MAX_ERR]);
  void       (*p_destroy)(EpaKernel *k);
  int        (*p_load_asm)(EpaKernel *k, const char *asm_path, char err[EPA_MAX_ERR]);
  int        (*p_run)(EpaKernel *k, uint32_t max_ticks, int debug, char err[EPA_MAX_ERR]);

  // optional lifecycle hook (should exist in your kernel_so)
  int        (*p_ingress_push)(EpaKernel *k, uint32_t wid, const void *data, uint32_t len);

  EpaKernel *k;
} LocalKernel;

static LocalKernel g_lk;

// --------------------------
// Simple REPL test thread-pool (for validating "N threads waiting")
// --------------------------

typedef struct {
  pthread_mutex_t mu;
  pthread_cond_t  cv;

  pthread_t  *threads;
  uint32_t    n_threads;

  _Atomic uint32_t waiting;
  int stop;
  int inited;
} ReplThreadPool;

static ReplThreadPool g_tp;

static void tp_destroy(void);

static void* tp_thread_main(void *arg) {
  (void)arg;

  pthread_mutex_lock(&g_tp.mu);
  for (;;) {
    if (g_tp.stop) break;

    // mark ourselves as waiting BEFORE we park
    atomic_fetch_add(&g_tp.waiting, 1);

    // wait until woken (or stop)
    pthread_cond_wait(&g_tp.cv, &g_tp.mu);

    // woke up
    atomic_fetch_sub(&g_tp.waiting, 1);

    if (g_tp.stop) break;

    // In this test pool, threads just re-park immediately.
    // Later, this is where we'd pick up batch/work descriptors.
  }
  pthread_mutex_unlock(&g_tp.mu);
  return NULL;
}

static int tp_init(uint32_t n) {
  tp_destroy();

  if (n == 0) return 0;

  memset(&g_tp, 0, sizeof(g_tp));
  pthread_mutex_init(&g_tp.mu, NULL);
  pthread_cond_init(&g_tp.cv, NULL);

  g_tp.threads = (pthread_t*)calloc(n, sizeof(pthread_t));
  if (!g_tp.threads) {
    pthread_cond_destroy(&g_tp.cv);
    pthread_mutex_destroy(&g_tp.mu);
    memset(&g_tp, 0, sizeof(g_tp));
    return 0;
  }

  g_tp.n_threads = n;
  g_tp.stop = 0;
  atomic_store(&g_tp.waiting, 0);

  for (uint32_t i = 0; i < n; i++) {
    if (pthread_create(&g_tp.threads[i], NULL, tp_thread_main, NULL) != 0) {
      // stop + join whatever we started
      g_tp.stop = 1;
      pthread_cond_broadcast(&g_tp.cv);
      for (uint32_t j = 0; j < i; j++) pthread_join(g_tp.threads[j], NULL);
      free(g_tp.threads);
      pthread_cond_destroy(&g_tp.cv);
      pthread_mutex_destroy(&g_tp.mu);
      memset(&g_tp, 0, sizeof(g_tp));
      return 0;
    }
  }

  g_tp.inited = 1;
  return 1;
}

static void tp_destroy(void) {
  if (!g_tp.inited) return;

  pthread_mutex_lock(&g_tp.mu);
  g_tp.stop = 1;
  pthread_cond_broadcast(&g_tp.cv);
  pthread_mutex_unlock(&g_tp.mu);

  for (uint32_t i = 0; i < g_tp.n_threads; i++) {
    if (g_tp.threads[i]) pthread_join(g_tp.threads[i], NULL);
  }

  free(g_tp.threads);
  pthread_cond_destroy(&g_tp.cv);
  pthread_mutex_destroy(&g_tp.mu);

  memset(&g_tp, 0, sizeof(g_tp));
}

static uint32_t tp_waiting_count(void) {
  return atomic_load(&g_tp.waiting);
}

// Wait until waiting==n (or timeout_ms). Returns 1 if reached, 0 if timeout.
static int tp_wait_until_waiting(uint32_t n, uint32_t timeout_ms) {
  const uint32_t step_us = 1000; // 1ms
  uint32_t waited = 0;

  while (waited < timeout_ms) {
    if (tp_waiting_count() == n) return 1;
    usleep(step_us);
    waited += 1;
  }
  return (tp_waiting_count() == n);
}

static int lk_open_so(char err[EPA_MAX_ERR]) {
  if (g_lk.so && g_lk.k) return 1; // already open

  g_lk.so = dlopen(g_cfg.kernel_so, RTLD_NOW);
  if (!g_lk.so) {
    snprintf(err, EPA_MAX_ERR, "run-local: dlopen(%s) failed: %s", g_cfg.kernel_so, dlerror());
    return 0;
  }

  *(void**)(&g_lk.p_create)   = dlsym(g_lk.so, "epa_kernel_create");
  *(void**)(&g_lk.p_destroy)  = dlsym(g_lk.so, "epa_kernel_destroy");
  *(void**)(&g_lk.p_load_asm) = dlsym(g_lk.so, "epa_kernel_load_asm");
  *(void**)(&g_lk.p_run)      = dlsym(g_lk.so, "epa_kernel_run");

  // optional
  *(void**)(&g_lk.p_ingress_push) = dlsym(g_lk.so, "epa_kernel_ingress_push");

  if (!g_lk.p_create || !g_lk.p_destroy || !g_lk.p_load_asm || !g_lk.p_run) {
    snprintf(err, EPA_MAX_ERR, "run-local: missing required symbols in %s", g_cfg.kernel_so);
    return 0;
  }

  g_lk.k = g_lk.p_create(err);
  if (!g_lk.k) return 0;

  return 1;
}

static int write_buffer_to_temp_asm(const StrBuf *sb, char out_path[512]) {
  if (!sb->buf || sb->len == 0) return 0;

  char tmpl[] = "/tmp/epaasm_local_XXXXXX.epaasm";
  int fd = mkstemps(tmpl, (int)strlen(".epaasm"));
  if (fd < 0) return 0;

  FILE *tf = fdopen(fd, "wb");
  if (!tf) {
    close(fd);
    unlink(tmpl);
    return 0;
  }

  fwrite(sb->buf, 1, sb->len, tf);
  fwrite("\n", 1, 1, tf);
  fclose(tf);

  strncpy(out_path, tmpl, 511);
  out_path[511] = 0;
  return 1;
}

static int cmd_run_local(const StrBuf *sb, uint32_t ticks) {
  char err[EPA_MAX_ERR];
  if (!lk_open_so(err)) {
    fprintf(stderr, "%s\n", err);
    return 0;
  }

  char asm_path[512];
  if (!write_buffer_to_temp_asm(sb, asm_path)) {
    fprintf(stderr, "run-local: buffer is empty or temp write failed\n");
    return 0;
  }

  if (!g_lk.p_load_asm(g_lk.k, asm_path, err)) {
    fprintf(stderr, "run-local: load_asm FAILED: %s\n", err[0] ? err : "(unknown)");
    unlink(asm_path);
    return 0;
  }

  uint32_t run_ticks = ticks ? ticks : (uint32_t)strtoul(g_cfg.run_ticks, NULL, 10);

  int rc = g_lk.p_run(g_lk.k, run_ticks, 1 /* debug - no error on tick under run */, err);

  unlink(asm_path);

  if (rc == 1) {
    printf("run-local: rc=1\n");
    return 1;
  } else if (rc == 2) {
    printf("run-local: rc=2 (yield)\n");
    return 1;
  } else {
    fprintf(stderr, "run-local: rc=0 (error): %s\n", err[0] ? err : "(unknown)");
    return 0;
  }
}

static int cmd_step_local() {
	char err[EPA_MAX_ERR];
	g_lk.p_run(g_lk.k, 1, 1 /* debug - no error on tick under run */, err);
}

static int hexval(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// Usage:
//   ingress <wid> <text...>
//   ingress <wid> hex:<aabbcc...>
static int cmd_ingress(uint32_t wid, const char *payload) {
  char err[EPA_MAX_ERR];
  if (!lk_open_so(err)) {
    fprintf(stderr, "%s\n", err);
    return 0;
  }
  if (!g_lk.p_ingress_push) {
    fprintf(stderr, "ingress: epa_kernel_ingress_push not found in %s\n", g_cfg.kernel_so);
    return 0;
  }
  if (!payload || !*payload) {
    fprintf(stderr, "ingress: missing payload\n");
    return 0;
  }

  if (!strncmp(payload, "hex:", 4)) {
    const char *h = payload + 4;
    size_t n = strlen(h);
    if ((n & 1) != 0) {
      fprintf(stderr, "ingress: hex payload must have even length\n");
      return 0;
    }
    size_t bytes_len = n / 2;
    uint8_t *buf = (uint8_t*)malloc(bytes_len);
    if (!buf) { fprintf(stderr, "ingress: OOM\n"); return 0; }

    for (size_t i = 0; i < bytes_len; i++) {
      int hi = hexval((unsigned char)h[i*2+0]);
      int lo = hexval((unsigned char)h[i*2+1]);
      if (hi < 0 || lo < 0) {
        free(buf);
        fprintf(stderr, "ingress: invalid hex\n");
        return 0;
      }
      buf[i] = (uint8_t)((hi << 4) | lo);
    }

    int ok = g_lk.p_ingress_push(g_lk.k, wid, buf, (uint32_t)bytes_len);
    free(buf);

    if (ok) {
      fprintf(stderr, "ingress: push failed (wid=%u)\n", wid);
      return 0;
    }
    printf("ingress: queued %zu bytes to wid=%u\n", bytes_len, wid);
    return 1;
  }

  uint32_t len = (uint32_t)strlen(payload);
  int ok = g_lk.p_ingress_push(g_lk.k, wid, payload, len);
  if (ok) {
    fprintf(stderr, "ingress: push failed (wid=%u)\n", wid);
    return 0;
  }
  printf("ingress: queued %u bytes to wid=%u\n", len, wid);
  return 1;
}

// --------------------------
// Local worker inspection
// --------------------------

static void dump_hex_ascii_limited(const uint8_t *b, size_t len, size_t max_bytes) {
  if (!b || len == 0) { printf("(empty)\n"); return; }
  if (max_bytes && len > max_bytes) len = max_bytes;
  for (size_t i = 0; i < len; i += 16) {
    printf("%08zx: ", i);
    for (size_t j = 0; j < 16; j++) {
      if (i + j < len) printf("%02X ", b[i + j]);
      else printf("   ");
    }
    printf(" | ");
    for (size_t j = 0; j < 16 && i + j < len; j++) {
      uint8_t c = b[i + j];
      putchar((c >= 32 && c < 127) ? (int)c : '.');
    }
    putchar('\n');
  }
  if (max_bytes && max_bytes < len) {
    printf("(truncated)\n");
  }
}

static void dump_ring_u32(const char *name, const IdRing *r, size_t max_items) {
  if (!r) {
    printf("%s: (null)\n", name);
    return;
  }
  printf("%s: cap=%u head=%u tail=%u count=%u\n",
         name, (unsigned)r->cap, (unsigned)r->head, (unsigned)r->tail, (unsigned)r->count);
  if (!r->buf || r->cap == 0 || r->count == 0) return;
  uint32_t n = r->count;
  if (max_items && n > max_items) n = (uint32_t)max_items;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t idx = (uint32_t)((r->head + i) % r->cap);
    printf("  [%u] = 0x%08X (%u)\n", (unsigned)i, (unsigned)r->buf[idx], (unsigned)r->buf[idx]);
  }
  if (max_items && r->count > max_items) {
    printf("  ... (%u more)\n", (unsigned)(r->count - (uint32_t)max_items));
  }
}

static void dump_stack(const EpaStack *st, size_t max_items) {
  if (!st) { printf("  stack: (null)\n"); return; }
  printf("  stack: sp=%zu cap=%zu\n", st->sp, st->cap);
  if (!st->words || st->sp == 0) return;
  size_t start = 0;
  if (max_items && st->sp > max_items) start = st->sp - max_items;
  for (size_t i = start; i < st->sp; i++) {
    printf("    [%zu] 0x%08X (%u)\n", i, (unsigned)st->words[i], (unsigned)st->words[i]);
  }
  if (start) printf("    ... (showing last %zu items)\n", st->sp - start);
}

static int cmd_dump_worker(uint32_t wid) {
  char err[EPA_MAX_ERR];
  if (!lk_open_so(err)) {
    fprintf(stderr, "%s\n", err);
    return 0;
  }
  if (!g_lk.k) {
    fprintf(stderr, "dump-worker: no local kernel (run-local first)\n");
    return 0;
  }

  // g_lk.k is allocated by the shared library, but the layout is shared via headers.
  EpaKernel *k = g_lk.k;
  if (wid >= EPA_MAX_WORKERS) {
    fprintf(stderr, "dump-worker: wid out of range (0..%u)\n", (unsigned)(EPA_MAX_WORKERS - 1));
    return 0;
  }

  const EpaWorkerState *w = &k->impl.workers[wid];
  printf("-- WORKER %u --\n", (unsigned)wid);
  printf("  inited=%u halted=%u blocked=%u faulted=%u waiting_for_data=%u\n",
         (unsigned)w->inited, (unsigned)w->halted, (unsigned)w->blocked,
         (unsigned)w->faulted, (unsigned)w->waiting_for_data);

  printf("EIP: type=%u id=%u rel_pc=%u\n", (unsigned)(unsigned)w->vm.eip.block_type, (unsigned)(unsigned)w->vm.eip.block_id, (unsigned)w->vm.eip.rel_pc);

  // VM-level details
  printf("  vm: regs = [0x%08X 0x%08X 0x%08X 0x%08X], ip=%u\n",
         (unsigned)w->vm.csc[0], (unsigned)w->vm.csc[1], (unsigned)w->vm.csc[2], (unsigned)w->vm.csc[3],
         (unsigned)w->vm.eip.rel_pc);
  printf("  vm: locals[0..7] = ");
  for (int i = 0; i < 8; i++) printf("0x%08X ", (unsigned)w->vm.locals[i]);
  printf("\n");
  printf("  vm: lbytes_top=%u\n", (unsigned)w->vm.lbytes_top);
  dump_stack(&w->vm.stack, 32);
  dump_ring_u32("  inq", &w->inq, 64);
  dump_ring_u32("  outq", &w->outq, 64);

  // Signal mailbox (bytes)
  printf("  signal_mb_cap=%u\n", (unsigned)k->prog.signal_mailbox_size[w->id]);
  if (w->signal_mailbox && k->prog.signal_mailbox_size[w->id]) {
    dump_hex_ascii_limited(w->signal_mailbox, (size_t)k->prog.signal_mailbox_size[w->id], (size_t)k->prog.signal_mailbox_size[w->id]);
  } else {
    printf("  signal_mb: (null)\n");
  }

  return 1;
}

int main(void) {
  StrBuf sb = (StrBuf){0};
  char *selected_test = NULL;
  print_help();

  char *line = NULL;
  size_t cap = 0;

  epadev_defaults(&g_cfg);

  if (!load_dev_config("/etc/elara/dev.conf", &g_cfg)) {
    fprintf(stderr,
      "warning: could not load /etc/elara/dev.conf, using defaults\n");
  }

  printf("Config:\n");
  printf("  TEST_DIR    = %s\n", g_cfg.test_dir);
  printf("  KERNEL_SO   = %s\n", g_cfg.kernel_so);
  printf("  UNIT_RUNNER = %s\n", g_cfg.unit_runner);
  printf("  RUN_TICKS   = %s\n", g_cfg.run_ticks);

  while (1) {
    printf("epaasm> ");
    fflush(stdout);

    ssize_t n = getline(&line, &cap, stdin);
    if (n < 0) {
      printf("\n");
      break;
    }

    // Strip newline
    while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';

    char *t = ltrim(line);
    rtrim_inplace(t);

    if (*t == '\0') {
      // allow blank lines in buffer (useful for readability)
      if (!sb_append_line(&sb, "")) { fprintf(stderr, "OOM\n"); break; }
      continue;
    }

    // Commands only if they are the first token on the line.
    if (starts_with_word(t, "quit") || starts_with_word(t, "exit")) {
      break;
    }
    if (starts_with_word(t, "help")) {
      print_help();
      continue;
    }
    if (starts_with_word(t, "clear")) {
      sb_clear(&sb);
      printf("(cleared)\n");
      continue;
    }
    if (starts_with_word(t, "show")) {
      cmd_show(&sb);
      continue;
    }
    if (starts_with_word(t, "dump-blob")) {
      (void)cmd_dump_blob(&sb);
      continue;
    }
    if (starts_with_word(t, "dump-worker")) {
          char *p = t + 11;
          p = ltrim(p);
          if (*p == '\0') { fprintf(stderr, "dump-worker: missing wid\n"); continue; }
          char *end = NULL;
          unsigned long wid_ul = strtoul(p, &end, 10);
          if (end == p) { fprintf(stderr, "dump-worker: bad wid\n"); continue; }
          (void)cmd_dump_worker((uint32_t)wid_ul);
          continue;
        }
    if (starts_with_word(t, "save")) {
      char *p = t + 4;
      p = ltrim(p);
      if (*p == '\0') {
        fprintf(stderr, "save: missing path\n");
        continue;
      }
      if (!write_file_text(p, &sb)) {
        fprintf(stderr, "save: could not write '%s': %s\n", p, strerror(errno));
        continue;
      }
      printf("saved: %s\n", p);
      continue;
    }
    if (starts_with_word(t, "load")) {
      char *p = t + 4;
      p = ltrim(p);
      if (*p == '\0') {
        fprintf(stderr, "load: missing path\n");
        continue;
      }
      if (!load_file_text(p, &sb)) {
        fprintf(stderr, "load: could not read '%s': %s\n", p, strerror(errno));
        continue;
      }
      printf("loaded: %s (%zu bytes)\n", p, sb.len);
      continue;
    }
    if (starts_with_word(t, "compile")) {
      char *p = t + 7;
      p = ltrim(p);
      const char *out_path = (*p) ? p : NULL;
      (void)compile_buffer(&sb, out_path);
      continue;
    }
    if (starts_with_word(t, "run-local")) {
      char *p = t + 9;
      p = ltrim(p);
      uint32_t ticks = 0;
      if (*p) ticks = (uint32_t)strtoul(p, NULL, 10);
      (void)cmd_run_local(&sb, ticks);
      continue;
    }
    if (starts_with_word(t, "run") || starts_with_word(t, "r")) {
      (void)cmd_run_local(&sb, 1);
      continue;
    }
    if (starts_with_word(t, "step") || starts_with_word(t, "s")) {
      (void)cmd_step_local();
      continue;
    }
    if (starts_with_word(t, "ingress") || starts_with_word(t, "in")) {
      char *p = t + (starts_with_word(t, "ingress ") ? 7 : 2);
      p = ltrim(p);
      if (*p == '\0') { fprintf(stderr, "ingress: missing wid\n"); continue; }
      char *end = NULL;
      unsigned long wid_ul = strtoul(p, &end, 10);
      if (end == p) { fprintf(stderr, "ingress: bad wid\n"); continue; }
      p = ltrim(end);
      if (*p == '\0') { fprintf(stderr, "ingress: missing payload\n"); continue; }
      (void)cmd_ingress((uint32_t)wid_ul, p);
      continue;
    }
    if (starts_with_word(t, "run")) {
      (void)run_buffer(&sb);
      continue;
    }

    if (starts_with_word(t, "tests")) {
      cmd_tests(g_cfg.test_dir);
      continue;
    }

    if (starts_with_word(t, "test")) {
      char *p = t + 4;
      p = ltrim(p);
      if (*p == '\0') {
        fprintf(stderr, "test: missing <n|name>\n");
        continue;
      }

      char *path = resolve_test_path(g_cfg.test_dir, p);
      if (!path) {
        fprintf(stderr, "test: not found: %s\n", p);
        continue;
      }

      free(selected_test);
      selected_test = path;
      printf("selected: %s\n", selected_test);

      // Optional convenience: auto-load selected test into buffer
      if (!load_file_text(selected_test, &sb)) {
        fprintf(stderr, "test: could not load '%s': %s\n", selected_test, strerror(errno));
      } else {
        printf("loaded into buffer (%zu bytes)\n", sb.len);
      }

      continue;
    }

    if (starts_with_word(t, "run-test")) {
      char *p = t + 8;
      p = ltrim(p);

      char *path = NULL;
      if (*p) {
        path = resolve_test_path(g_cfg.test_dir, p);
        if (!path) {
          fprintf(stderr, "run-test: not found: %s\n", p);
          continue;
        }
      }

      const char *to_run = path ? path : selected_test;
      (void)run_test_file(to_run);

      free(path);
      continue;
    }

    if (starts_with_word(t, "tp-init")) {
      char *p = t + 7;
      p = ltrim(p);
      if (*p == '\0') { fprintf(stderr, "tp-init: missing <n>\n"); continue; }
      uint32_t nthreads = (uint32_t)strtoul(p, NULL, 10);
      if (nthreads == 0) { fprintf(stderr, "tp-init: n must be > 0\n"); continue; }

      if (!tp_init(nthreads)) {
        fprintf(stderr, "tp-init: failed\n");
        continue;
      }

      // Give threads a moment to reach their parked state
      int ok = tp_wait_until_waiting(nthreads, 1000 /*ms*/);

      printf("tp-init: started %u threads\n", (unsigned)nthreads);
      printf("tp-init: waiting=%u (%s)\n",
             (unsigned)tp_waiting_count(),
             ok ? "OK" : "NOT READY YET");

      continue;
    }

    if (starts_with_word(t, "tp-stat")) {
      if (!g_tp.inited) {
        printf("tp-stat: (not initialised)\n");
      } else {
        printf("tp-stat: threads=%u waiting=%u stop=%d\n",
               (unsigned)g_tp.n_threads,
               (unsigned)tp_waiting_count(),
               g_tp.stop);
      }
      continue;
    }

    if (starts_with_word(t, "tp-stop")) {
      tp_destroy();
      printf("tp-stop: done\n");
      continue;
    }

    // Otherwise: treat as EPAASM line and append to the buffer.
    if (!sb_append_line(&sb, t)) {
      fprintf(stderr, "OOM\n");
      break;
    }
  }

  free(selected_test);
  free(line);
  sb_free(&sb);
  free(g_last_blob);
  g_last_blob = NULL;
  g_last_blob_len = 0;
  return 0;
}

