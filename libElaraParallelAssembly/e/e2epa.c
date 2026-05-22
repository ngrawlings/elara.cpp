#define _POSIX_C_SOURCE 200809L
#include "e_lexer.h"
#include "e_parser.h"
#include "e_semantic.h"
#include "e_emit_epa.h"
#include "e_preprocess.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
  fprintf(stderr, "Usage: %s <input.e> <output.epaasm> [output.epamap]\n", argv0);
}

int main(int argc, char **argv) {
  char err[256];
  char *src;
  ETokenVec toks;
  EProgram prog;
  ESemanticModel model;
  ELineMap line_map;
  FILE *out;
  FILE *map_out = NULL;

  if (argc < 3 || argc > 4) {
    usage(argv[0]);
    return 2;
  }

  memset(&line_map, 0, sizeof(line_map));

  src = e_load_translation_unit_with_map(argv[1], &line_map, err);
  if (!src) {
    fprintf(stderr, "%s\n", err);
    return 1;
  }

  if (!e_lex_source(src, &toks, err)) {
    fprintf(stderr, "lex: %s\n", err);
    free(src);
    e_line_map_free(&line_map);
    return 1;
  }

  if (!e_parse_program(&toks, &prog, err)) {
    fprintf(stderr, "parse: %s\n", err);
    e_token_vec_free(&toks);
    free(src);
    e_line_map_free(&line_map);
    return 1;
  }

  if (!e_build_semantic_model(&prog, &model, err)) {
    fprintf(stderr, "semantic: %s\n", err);
    e_program_free(&prog);
    e_token_vec_free(&toks);
    free(src);
    e_line_map_free(&line_map);
    return 1;
  }

  out = fopen(argv[2], "w+b");
  if (!out) {
    fprintf(stderr, "open failed: %s\n", argv[2]);
    e_semantic_model_free(&model);
    e_program_free(&prog);
    e_token_vec_free(&toks);
    free(src);
    e_line_map_free(&line_map);
    return 1;
  }

  if (argc == 4) {
    map_out = fopen(argv[3], "wb");
    if (!map_out) {
      fprintf(stderr, "open map failed: %s\n", argv[3]);
      fclose(out);
      e_semantic_model_free(&model);
      e_program_free(&prog);
      e_token_vec_free(&toks);
      free(src);
      e_line_map_free(&line_map);
      return 1;
    }
  }

  if (!e_emit_epa_asm(out, map_out, &prog, &model, &line_map, argv[1], err)) {
    fprintf(stderr, "emit: %s\n", err);
    fclose(out);
    if (map_out) fclose(map_out);
    e_semantic_model_free(&model);
    e_program_free(&prog);
    e_token_vec_free(&toks);
    free(src);
    e_line_map_free(&line_map);
    return 1;
  }

  fclose(out);
  if (map_out) fclose(map_out);
  e_semantic_model_free(&model);
  e_program_free(&prog);
  e_token_vec_free(&toks);
  free(src);
  e_line_map_free(&line_map);
  return 0;
}
