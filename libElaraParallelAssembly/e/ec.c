#define _POSIX_C_SOURCE 200809L
#include "e_lexer.h"
#include "e_parser.h"
#include "e_semantic.h"
#include "e_templates.h"

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
  fprintf(stderr, "Usage: %s <input.e>\n", argv0);
}

static void dump_templates(FILE *out, const EProgram *prog) {
  size_t i;
  for (i = 0; i < prog->count; i++) {
    ETemplateGraph graph;
    char err[256];
    const ETopDecl *top = &prog->items[i];
    const EStmt *body = NULL;
    const char *name = NULL;

    switch (top->kind) {
      case E_TOP_KERNEL:
        body = top->as.kernel.body;
        name = "kernel";
        break;
      case E_TOP_WORKER:
        body = top->as.worker.body;
        name = top->as.worker.name;
        break;
      case E_TOP_FUNCTION:
        body = top->as.func.body;
        name = top->as.func.name;
        break;
      case E_TOP_TYPE:
        body = top->as.tdecl.body;
        name = top->as.tdecl.name;
        break;
      case E_TOP_STRUCT:
      case E_TOP_DECLARE:
        break;
    }

    if (!body) continue;
    if (!e_template_graph_build_from_stmt(body, &graph, err)) {
      fprintf(out, "template-graph %s error: %s\n", name ? name : "(anon)", err);
      continue;
    }
    fprintf(out, "template-owner %s\n", name ? name : "(anon)");
    e_template_graph_dump(out, &graph);
    e_template_graph_free(&graph);
  }
}

int main(int argc, char **argv) {
  char err[256];
  char *src;
  ETokenVec toks;
  EProgram prog;
  ESemanticModel model;

  if (argc != 2) {
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

  e_program_dump(stdout, &prog);
  e_semantic_model_dump(stdout, &model);
  dump_templates(stdout, &prog);

  e_semantic_model_free(&model);
  e_program_free(&prog);
  e_token_vec_free(&toks);
  free(src);
  return 0;
}
