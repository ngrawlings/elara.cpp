#define _POSIX_C_SOURCE 200809L
#include "e_lexer.h"
#include "e_parser.h"
#include "e_semantic.h"
#include "e_templates.h"
#include "e_preprocess.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
      case E_TOP_AT_ENTRY:
        body = top->as.at_entry.body;
        name = top->as.at_entry.name;
        break;
      case E_TOP_TYPE:
        body = top->as.tdecl.body;
        name = top->as.tdecl.name;
        break;
      case E_TOP_STRUCT:
      case E_TOP_DECLARE:
      case E_TOP_DYNAMIC:
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

  src = e_load_translation_unit(argv[1], err);
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
