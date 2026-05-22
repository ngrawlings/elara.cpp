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
