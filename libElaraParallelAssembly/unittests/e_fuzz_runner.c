// e_fuzz_runner.c
// Handles both .e and .epaasm test programs.
// .e files are compiled in-process via the E pipeline.
// Random ingress is pumped before each run via e_fuzz_pump.
//
// Usage:
//   ./e_fuzz_runner <libkernel.so> <tests_dir> [max_ticks] [n_msgs] [seed]

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include "e_fuzz_pump.h"
#include "e_compiler.h"
#include "epa_asm_compiler.h"

/* ------------------------------------------------------------------ */
/* Mirror kernel API types (kept minimal, same pattern as unit runner) */
/* ------------------------------------------------------------------ */
#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

typedef struct { uint32_t block_type; uint32_t block_id; uint32_t rel_pc; } EpaEip;
typedef struct EpaKernel EpaKernel;

typedef enum {
    EPA_KDBG_BREAK  = 1,
    EPA_KDBG_TRAP   = 2,
    EPA_KDBG_EXCEPT = 3,
    EPA_KDBG_SIGNAL = 4,
} EpaKernelDbgKind;

typedef EpaKernel* (*fp_create)(char err[EPA_MAX_ERR]);
typedef void       (*fp_destroy)(EpaKernel *k);
typedef int        (*fp_load_blob)(EpaKernel *k, const uint8_t *blob, size_t blob_len, char err[EPA_MAX_ERR]);
typedef int        (*fp_run)(EpaKernel *k, uint32_t max_ticks, int debug, char err[EPA_MAX_ERR]);
typedef int        (*fp_ingress_push)(EpaKernel *k, uint32_t wid, const void *data, uint32_t len);
typedef uint32_t   (*fp_worker_count)(const EpaKernel *k);
typedef void       (*fp_set_dbg_cb)(EpaKernel *k,
                       void (*cb)(void *, EpaKernelDbgKind, uint8_t, uint32_t, const EpaEip *, const char *),
                       void *user);

/* ------------------------------------------------------------------ */
/* Globals resolved once from the .so                                  */
/* ------------------------------------------------------------------ */
static fp_create        g_create;
static fp_destroy       g_destroy;
static fp_load_blob     g_load_blob;
static fp_run           g_run;
static fp_ingress_push  g_ingress_push;
static fp_worker_count  g_worker_count;
static fp_set_dbg_cb    g_set_dbg_cb;

/* ------------------------------------------------------------------ */
/* Debug event capture                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    int trap_count;
    int except_count;
    char last_err[EPA_MAX_ERR];
} FuzzEvents;

static void fuzz_dbg_cb(void *user, EpaKernelDbgKind kind,
                        uint8_t wid, uint32_t code,
                        const EpaEip *at, const char *msg)
{
    FuzzEvents *ev = (FuzzEvents *)user;
    (void)wid; (void)code; (void)at;
    if (kind == EPA_KDBG_TRAP)   ev->trap_count++;
    if (kind == EPA_KDBG_EXCEPT) ev->except_count++;
    if (msg && msg[0])
        snprintf(ev->last_err, EPA_MAX_ERR, "%s", msg);
}

/* ------------------------------------------------------------------ */
/* Ingress push adapter for e_fuzz_pump                                */
/* ------------------------------------------------------------------ */
typedef struct { EpaKernel *k; } PushCtx;

static int push_adapter(void *ctx, uint32_t wid, const void *data, uint32_t len)
{
    PushCtx *p = (PushCtx *)ctx;
    return g_ingress_push(p->k, wid, data, len);
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static int ends_with(const char *s, const char *suf)
{
    size_t ls = strlen(s), lx = strlen(suf);
    return lx <= ls && memcmp(s + ls - lx, suf, lx) == 0;
}

static int is_regular_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int cmp_strptr(const void *a, const void *b)
{
    return strcmp(*(const char * const *)a, *(const char * const *)b);
}

static void *must_dlsym(void *so, const char *name)
{
    dlerror();
    void *p = dlsym(so, name);
    const char *e = dlerror();
    if (!p || e) { fprintf(stderr, "dlsym(%s): %s\n", name, e ? e : "null"); exit(2); }
    return p;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
            "Usage: %s <libkernel.so> <tests_dir> [max_ticks] [n_msgs] [seed]\n",
            argv[0]);
        return 2;
    }

    const char *so_path  = argv[1];
    const char *dir_path = argv[2];
    uint32_t max_ticks = 2u * 1000u * 1000u;
    uint32_t n_msgs    = 32u;
    uint32_t seed      = 0xfeed1234u;

    if (argc >= 4) max_ticks = (uint32_t)strtoull(argv[3], NULL, 10);
    if (argc >= 5) n_msgs    = (uint32_t)strtoull(argv[4], NULL, 10);
    if (argc >= 6) seed      = (uint32_t)strtoull(argv[5], NULL, 10);

    void *so = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!so) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

    g_create        = (fp_create)        must_dlsym(so, "epa_kernel_create");
    g_destroy       = (fp_destroy)       must_dlsym(so, "epa_kernel_destroy");
    g_load_blob     = (fp_load_blob)     must_dlsym(so, "epa_kernel_load_blob");
    g_run           = (fp_run)           must_dlsym(so, "epa_kernel_run");
    g_ingress_push  = (fp_ingress_push)  must_dlsym(so, "epa_kernel_ingress_push");
    g_worker_count  = (fp_worker_count)  must_dlsym(so, "epa_kernel_worker_count");
    g_set_dbg_cb    = (fp_set_dbg_cb)    must_dlsym(so, "epa_kernel_set_debug_callback");

    /* ---- collect .epaasm and .e files ---- */
    DIR *d = opendir(dir_path);
    if (!d) { fprintf(stderr, "opendir(%s): %s\n", dir_path, strerror(errno)); dlclose(so); return 2; }

    size_t cap = 64, n = 0;
    char **files = (char **)calloc(cap, sizeof(char *));
    if (!files) { fprintf(stderr, "OOM\n"); closedir(d); dlclose(so); return 2; }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (!ends_with(de->d_name, ".epaasm") && !ends_with(de->d_name, ".e")) continue;
        size_t need = strlen(dir_path) + 1 + strlen(de->d_name) + 1;
        char *full = (char *)malloc(need);
        if (!full) { fprintf(stderr, "OOM\n"); return 2; }
        snprintf(full, need, "%s/%s", dir_path, de->d_name);
        if (!is_regular_file(full)) { free(full); continue; }
        if (n == cap) {
            cap *= 2;
            char **nf = (char **)realloc(files, cap * sizeof(char *));
            if (!nf) { fprintf(stderr, "OOM\n"); return 2; }
            files = nf;
        }
        files[n++] = full;
    }
    closedir(d);

    if (n == 0) {
        fprintf(stderr, "No .e or .epaasm files in %s\n", dir_path);
        free(files); dlclose(so); return 1;
    }
    qsort(files, n, sizeof(char *), cmp_strptr);

    int pass = 0, fail = 0;

    for (size_t i = 0; i < n; i++) {
        const char *path = files[i];
        char err[EPA_MAX_ERR];
        int  is_e = ends_with(path, ".e");

        err[0] = '\0';

        /* compile to binary blob fully in-memory */
        char    *epaasm   = NULL;
        uint8_t *blob     = NULL;
        size_t   blob_len = 0;

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

        EpaKernel *k = g_create(err);
        if (!k) {
            fprintf(stderr, "[FAIL] %s\n  kernel_create: %s\n", path, err);
            free(blob);
            fail++;
            continue;
        }

        FuzzEvents ev;
        memset(&ev, 0, sizeof(ev));
        g_set_dbg_cb(k, fuzz_dbg_cb, &ev);

        if (!g_load_blob(k, blob, blob_len, err)) {
            fprintf(stderr, "[FAIL] %s\n  load_blob: %s\n", path, err);
            g_destroy(k);
            free(blob);
            fail++;
            continue;
        }
        free(blob); blob = NULL;

        /* For .e files distribute ingress across real workers (wid 1..N-1).
         * For .epaasm files keep legacy wid=0 targeting. */
        PushCtx pctx = { k };
        uint32_t n_workers = g_worker_count(k);
        if (is_e && n_workers > 1u) {
            uint32_t per = n_msgs / (n_workers - 1u);
            uint32_t rem = n_msgs % (n_workers - 1u);
            for (uint32_t w = 1u; w < n_workers; w++) {
                uint32_t msgs = per + (w == 1u ? rem : 0u);
                e_fuzz_pump(push_adapter, &pctx, w, seed ^ w, msgs);
            }
        } else {
            e_fuzz_pump(push_adapter, &pctx, 0u, seed, n_msgs);
        }

        int run_ret = g_run(k, max_ticks, 0, err);
        g_destroy(k);

        int test_ok = (run_ret == 1) && (ev.trap_count == 0) && (ev.except_count == 0);
        if (test_ok) {
            printf("[PASS] %s\n", path);
            pass++;
        } else {
            fprintf(stderr, "[FAIL] %s\n", path);
            if (!test_ok)
                fprintf(stderr, "  run=%d traps=%d excepts=%d err=%s hook=%s\n",
                    run_ret, ev.trap_count, ev.except_count,
                    err[0] ? err : "(none)",
                    ev.last_err[0] ? ev.last_err : "(none)");
            fail++;
        }
    }

    for (size_t i = 0; i < n; i++) free(files[i]);
    free(files);
    dlclose(so);

    printf("\nSummary: %d passed, %d failed (total %zu)  seed=0x%08x n_msgs=%u\n",
           pass, fail, n, (unsigned)seed, (unsigned)n_msgs);
    return (fail == 0) ? 0 : 1;
}
