#define _POSIX_C_SOURCE 200809L
#include "e_compiler.h"
#include "e_preprocess.h"
#include "e_lexer.h"
#include "e_parser.h"
#include "e_semantic.h"
#include "e_emit_epa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char      *src;
  ETokenVec  toks;
  EProgram   prog;
  ESemanticModel model;
  int        ok;
} FileCompileState;

char *e_compile_src_to_epaasm(const char *src, char err[256])
{
    ETokenVec      toks;
    EProgram       prog;
    ESemanticModel model;
    char          *out_buf = NULL;
    size_t         out_len = 0;
    FILE          *out_fp  = NULL;

    memset(&toks,  0, sizeof(toks));
    memset(&prog,  0, sizeof(prog));
    memset(&model, 0, sizeof(model));

    if (!e_lex_source(src, &toks, err))             goto done;
    if (!e_parse_program(&toks, &prog, err))         goto done;
    if (!e_build_semantic_model(&prog, &model, err)) goto done;

    out_fp = open_memstream(&out_buf, &out_len);
    if (!out_fp) {
        if (err) snprintf(err, 256, "e_compile_src_to_epaasm: open_memstream failed");
        goto done;
    }
    if (!e_emit_epa_asm(out_fp, NULL, &prog, &model, NULL, NULL, err)) {
        fclose(out_fp); out_fp = NULL;
        free(out_buf);  out_buf = NULL;
        goto done;
    }
    fclose(out_fp); out_fp = NULL;   /* flushes and null-terminates out_buf */

done:
    e_semantic_model_free(&model);
    e_program_free(&prog);
    e_token_vec_free(&toks);
    return out_buf;
}

char *e_compile_file_to_epaasm(const char *path, char err[256])
{
    char *src    = e_load_translation_unit(path, err);
    if (!src) return NULL;
    char *result = e_compile_src_to_epaasm(src, err);
    free(src);
    return result;
}

int e_compile_files_to_epaasm(const char **paths, size_t count,
                               char **out_asm,
                               char (*err_per_file)[256],
                               char err[256])
{
    size_t i;
    FileCompileState *states = NULL;
    ECrossKernelIndex cross_kernel;
    const ESemanticModel **model_ptrs = NULL;
    int result = 1;

    if (count == 0) return 1;

    memset(&cross_kernel, 0, sizeof(cross_kernel));

    states = (FileCompileState*)calloc(count, sizeof(*states));
    if (!states) {
        if (err) snprintf(err, 256, "e_compile_files_to_epaasm: OOM");
        return 0;
    }
    model_ptrs = (const ESemanticModel**)calloc(count, sizeof(*model_ptrs));
    if (!model_ptrs) {
        if (err) snprintf(err, 256, "e_compile_files_to_epaasm: OOM");
        free(states);
        return 0;
    }

    /* Phase 1: lex + parse + build semantic model for each file. */
    for (i = 0; i < count; i++) {
        FileCompileState *s = &states[i];
        char ferr[256] = {0};
        s->src = e_load_translation_unit(paths[i], ferr);
        if (!s->src) {
            if (err_per_file) snprintf((*err_per_file) + i * 256, 256, "%s", ferr);
            continue;
        }
        if (!e_lex_source(s->src, &s->toks, ferr) ||
            !e_parse_program(&s->toks, &s->prog, ferr) ||
            !e_build_semantic_model(&s->prog, &s->model, ferr)) {
            if (err_per_file) snprintf((*err_per_file) + i * 256, 256, "%s", ferr);
            continue;
        }
        s->ok = 1;
        model_ptrs[i] = &s->model;
    }

    /* Phase 2: build cross-kernel index from all successful first-pass models. */
    {
        char cidx_err[256] = {0};
        if (!e_build_cross_kernel_index(model_ptrs, count, &cross_kernel, cidx_err)) {
            if (err) snprintf(err, 256, "%s", cidx_err);
            result = 0;
            goto done;
        }
    }

    /* Phase 3: inject cross-kernel index into each model and emit. */
    for (i = 0; i < count; i++) {
        FileCompileState *s = &states[i];
        char ferr[256] = {0};
        char *out_buf = NULL;
        size_t out_len = 0;
        FILE *out_fp = NULL;

        out_asm[i] = NULL;
        if (!s->ok) continue;

        /* Copy cross-kernel index into this model (build a fresh copy). */
        {
            const ESemanticModel *mptr = &s->model;
            e_cross_kernel_index_free(&s->model.cross_kernel);
            if (!e_build_cross_kernel_index(model_ptrs, count, &s->model.cross_kernel, ferr)) {
                if (err_per_file) snprintf((*err_per_file) + i * 256, 256, "%s", ferr);
                (void)mptr;
                continue;
            }
            if (!e_validate_cross_kernel_references(&s->prog, &s->model, ferr)) {
                if (err_per_file) snprintf((*err_per_file) + i * 256, 256, "%s", ferr);
                (void)mptr;
                continue;
            }
        }

        out_fp = open_memstream(&out_buf, &out_len);
        if (!out_fp) {
            if (err_per_file) snprintf((*err_per_file) + i * 256, 256, "open_memstream failed");
            continue;
        }
        if (!e_emit_epa_asm(out_fp, NULL, &s->prog, &s->model, NULL, NULL, ferr)) {
            fclose(out_fp);
            free(out_buf);
            if (err_per_file) snprintf((*err_per_file) + i * 256, 256, "%s", ferr);
            continue;
        }
        fclose(out_fp);
        out_asm[i] = out_buf;
    }

done:
    e_cross_kernel_index_free(&cross_kernel);
    free(model_ptrs);
    for (i = 0; i < count; i++) {
        FileCompileState *s = &states[i];
        e_semantic_model_free(&s->model);
        e_program_free(&s->prog);
        e_token_vec_free(&s->toks);
        free(s->src);
    }
    free(states);
    return result;
}
