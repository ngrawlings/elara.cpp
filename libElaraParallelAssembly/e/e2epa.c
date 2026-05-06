#define _POSIX_C_SOURCE 200809L
#include "e_lexer.h"
#include "e_parser.h"
#include "e_semantic.h"
#include "e_emit_epa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, char err[256]) {
  FILE *f;
  long n;
  size_t got;
  char *buf;

  if (err) err[0] = 0;
  f = fopen(path, "rb");
  if (!f) {
    if (err) snprintf(err, 256, "open failed: %s", path);
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    if (err) snprintf(err, 256, "seek failed: %s", path);
    return NULL;
  }
  n = ftell(f);
  if (n < 0) {
    fclose(f);
    if (err) snprintf(err, 256, "ftell failed: %s", path);
    return NULL;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    if (err) snprintf(err, 256, "seek reset failed: %s", path);
    return NULL;
  }
  buf = (char*)malloc((size_t)n + 1u);
  if (!buf) {
    fclose(f);
    if (err) snprintf(err, 256, "OOM");
    return NULL;
  }
  got = fread(buf, 1, (size_t)n, f);
  fclose(f);
  if (got != (size_t)n) {
    free(buf);
    if (err) snprintf(err, 256, "read failed: %s", path);
    return NULL;
  }
  buf[n] = 0;
  return buf;
}

static void usage(const char *argv0) {
  fprintf(stderr, "Usage: %s <input.e> <output.epaasm>\n", argv0);
}

int main(int argc, char **argv) {
  char err[256];
  char *src;
  ETokenVec toks;
  EProgram prog;
  ESemanticModel model;
  FILE *out;

  if (argc != 3) {
    usage(argv[0]);
    return 2;
  }

  src = read_file(argv[1], err);
  if (!src) {
    fprintf(stderr, "%s\n", err);
    return 1;
  }

  if (!e_lex_source(src, &toks, err)) {
    fprintf(stderr, "lex: %s\n", err);
    free(src);
    return 1;
  }

  if (!e_parse_program(&toks, &prog, err)) {
    fprintf(stderr, "parse: %s\n", err);
    e_token_vec_free(&toks);
    free(src);
    return 1;
  }

  if (!e_build_semantic_model(&prog, &model, err)) {
    fprintf(stderr, "semantic: %s\n", err);
    e_program_free(&prog);
    e_token_vec_free(&toks);
    free(src);
    return 1;
  }

  out = fopen(argv[2], "wb");
  if (!out) {
    fprintf(stderr, "open failed: %s\n", argv[2]);
    e_semantic_model_free(&model);
    e_program_free(&prog);
    e_token_vec_free(&toks);
    free(src);
    return 1;
  }

  if (!e_emit_epa_asm(out, &prog, &model, err)) {
    fprintf(stderr, "emit: %s\n", err);
    fclose(out);
    e_semantic_model_free(&model);
    e_program_free(&prog);
    e_token_vec_free(&toks);
    free(src);
    return 1;
  }

  fclose(out);
  e_semantic_model_free(&model);
  e_program_free(&prog);
  e_token_vec_free(&toks);
  free(src);
  return 0;
}
