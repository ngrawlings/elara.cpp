// unittests/ctest_registry.c
#include "ctest.h"

#include <string.h>
#include <stdio.h>

static ctest_desc *g_head = NULL;
static ctest_desc *g_tail = NULL;

// ---- shared state slot ----
static void *g_state_ptr = NULL;
static ctest_state_dtor g_state_dtor = NULL;

void ctest_state_set(void *p, ctest_state_dtor dtor)
{
    // If replacing existing state, clean it up first.
    if (g_state_ptr && g_state_dtor) {
        g_state_dtor(g_state_ptr);
    }
    g_state_ptr  = p;
    g_state_dtor = dtor;
}

void *ctest_state_get(void)
{
    return g_state_ptr;
}

void ctest_state_clear(void)
{
    ctest_state_set(NULL, NULL);
}

void ctest_register(const char *file, const char *name, ctest_fn fn)
{
    static ctest_desc pool[4096];   // simple static pool; bump if needed
    static unsigned used = 0;

    if (!file) file = "(unknown)";
    if (!name) name = "(unnamed)";
    if (!fn)   return;

    if (used >= (unsigned)(sizeof(pool) / sizeof(pool[0]))) {
        fprintf(stderr, "ctest_register: registry overflow\n");
        return;
    }

    ctest_desc *d = &pool[used++];
    d->file = file;
    d->name = name;
    d->fn   = fn;
    d->next = NULL;

    if (!g_head) {
        g_head = g_tail = d;
    } else {
        g_tail->next = d;
        g_tail = d;
    }
}

int ctest_run_all(void)
{
    int fails = 0;

    const char *cur_file = NULL;

    // Collect failures (static, no malloc)
    typedef struct {
        const char *file;
        const char *name;
    } FailRec;

    static FailRec fail_list[1024];
    unsigned fail_count = 0;

    for (ctest_desc *t = g_head; t; t = t->next) {
        if (!cur_file || strcmp(cur_file, t->file) != 0) {
            cur_file = t->file;
            fprintf(stdout, "\n== %s ==\n", cur_file);
        }

        int rc = t->fn();
        if (rc) {
            fprintf(stderr, "[FAIL] %s\n", t->name);
            fails++;

            if (fail_count < (unsigned)(sizeof(fail_list) / sizeof(fail_list[0]))) {
                fail_list[fail_count].file = t->file;
                fail_list[fail_count].name = t->name;
                fail_count++;
            } else {
                static int warned = 0;
                if (!warned) {
                    warned = 1;
                    fprintf(stderr, "[WARN] failure summary overflow; truncating list\n");
                }
            }
        } else {
            fprintf(stdout, "[PASS] %s\n", t->name);
        }
    }

    // ---- Summary block at the bottom ----
    if (fails) {
        fprintf(stderr, "\n==== FAILURES (%d) ====\n", fails);
        for (unsigned i = 0; i < fail_count; i++) {
            fprintf(stderr, "  %s :: %s\n", fail_list[i].file, fail_list[i].name);
        }
        if (fail_count != (unsigned)fails) {
            fprintf(stderr, "  ... (%d more failures not listed)\n",
                    (int)((unsigned)fails - fail_count));
        }
    } else {
        fprintf(stdout, "\n==== ALL TESTS PASSED ====\n");
    }

    // Always clean up shared state at the end.
    ctest_state_clear();

    return fails;
}
