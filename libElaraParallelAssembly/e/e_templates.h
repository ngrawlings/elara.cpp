#pragma once

#include "e_ast.h"

#include <stdint.h>
#include <stdio.h>

typedef enum {
  E_TPL_INVALID = 0,
  E_TPL_EXPR_EVAL,
  E_TPL_IF_HEAD,
  E_TPL_IF_THEN,
  E_TPL_IF_ELSE,
  E_TPL_IF_JOIN,
  E_TPL_SWITCH_HEAD,
  E_TPL_SWITCH_CASE,
  E_TPL_SWITCH_DEFAULT,
  E_TPL_SWITCH_JOIN,
  E_TPL_RETURN,
  E_TPL_BREAK,
  E_TPL_SEQ,
} ETemplateKind;

typedef enum {
  E_EDGE_NONE = 0,
  E_EDGE_FALLTHROUGH,
  E_EDGE_TRUE,
  E_EDGE_FALSE,
  E_EDGE_JUMP,
  E_EDGE_TERMINAL,
} ETemplateEdgeKind;

typedef struct {
  uint32_t target_block_id;
  ETemplateEdgeKind kind;
} ETemplateEdge;

typedef struct {
  uint32_t block_id;
  ETemplateKind kind;
  const char *debug_name;

  /*
    Condition/result contract for the first control-flow templates:
    - The block that feeds E_TPL_IF_HEAD must leave a canonical boolean result.
    - For now that means: 0 = false, non-zero = true.
    - The eventual EPA lowering may keep this value on the stack or move it into
      a fixed scratch register, but the semantic contract stays the same.
    - Variable materialization is intentionally deferred; this template layer
      only describes control-flow shape and edge meanings.
  */
  uint8_t consumes_predicate;

  ETemplateEdge primary_edge;
  ETemplateEdge secondary_edge;
} ETemplateBlock;

typedef struct {
  ETemplateBlock *blocks;
  size_t block_count;
} ETemplateGraph;

const char *e_template_kind_name(ETemplateKind kind);
const char *e_template_edge_kind_name(ETemplateEdgeKind kind);
int e_template_graph_build_from_stmt(const EStmt *stmt, ETemplateGraph *out_graph, char err[256]);
void e_template_graph_free(ETemplateGraph *graph);
void e_template_graph_dump(FILE *out, const ETemplateGraph *graph);
