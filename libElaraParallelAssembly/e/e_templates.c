#define _POSIX_C_SOURCE 200809L
#include "e_templates.h"

#include "e_ast.h"

#include <stdlib.h>
#include <string.h>
#include <stddef.h>

typedef struct {
  ETemplateGraph *graph;
  uint32_t next_id;
  uint32_t break_target;
  uint32_t continue_target;
} GraphBuild;

typedef struct {
  uint32_t entry_id;
  uint32_t exit_id;
} Region;

static int graph_push(ETemplateGraph *graph, ETemplateBlock block) {
  ETemplateBlock *next;
  next = (ETemplateBlock*)realloc(graph->blocks, sizeof(ETemplateBlock) * (graph->block_count + 1u));
  if (!next) return 0;
  graph->blocks = next;
  graph->blocks[graph->block_count++] = block;
  return 1;
}

static uint32_t add_block(GraphBuild *gb, ETemplateKind kind, const char *debug_name, uint8_t consumes_predicate) {
  ETemplateBlock block;
  memset(&block, 0, sizeof(block));
  block.block_id = gb->next_id++;
  block.kind = kind;
  block.debug_name = debug_name;
  block.consumes_predicate = consumes_predicate;
  if (!graph_push(gb->graph, block)) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  return block.block_id;
}

static ETemplateBlock *find_block(ETemplateGraph *graph, uint32_t id) {
  size_t i;
  for (i = 0; i < graph->block_count; i++) {
    if (graph->blocks[i].block_id == id) return &graph->blocks[i];
  }
  return NULL;
}

static void set_primary(ETemplateGraph *graph, uint32_t id, ETemplateEdgeKind kind, uint32_t target) {
  ETemplateBlock *b = find_block(graph, id);
  if (!b) return;
  b->primary_edge.kind = kind;
  b->primary_edge.target_block_id = target;
}

static void set_secondary(ETemplateGraph *graph, uint32_t id, ETemplateEdgeKind kind, uint32_t target) {
  ETemplateBlock *b = find_block(graph, id);
  if (!b) return;
  b->secondary_edge.kind = kind;
  b->secondary_edge.target_block_id = target;
}

static Region build_stmt(GraphBuild *gb, const EStmt *stmt);

static Region build_simple(GraphBuild *gb, ETemplateKind kind, const char *name, int terminal) {
  Region r;
  r.entry_id = add_block(gb, kind, name, 0);
  r.exit_id = r.entry_id;
  if (terminal) {
    set_primary(gb->graph, r.entry_id, E_EDGE_TERMINAL, 0u);
  }
  return r;
}

static Region build_block(GraphBuild *gb, const EStmtList *list) {
  size_t i;
  Region region;
  uint32_t prev_exit = 0u;
  memset(&region, 0, sizeof(region));

  if (list->count == 0) {
    return build_simple(gb, E_TPL_SEQ, "empty-block", 0);
  }

  for (i = 0; i < list->count; i++) {
    Region child = build_stmt(gb, list->items[i]);
    if (i == 0) region.entry_id = child.entry_id;
    if (prev_exit != 0u) {
      ETemplateBlock *prev = find_block(gb->graph, prev_exit);
      if (prev && prev->primary_edge.kind == E_EDGE_NONE) {
        set_primary(gb->graph, prev_exit, E_EDGE_FALLTHROUGH, child.entry_id);
      }
    }
    prev_exit = child.exit_id;
    region.exit_id = child.exit_id;
  }

  return region;
}

static Region build_if(GraphBuild *gb, const EStmt *stmt) {
  Region out;
  Region then_region;
  Region else_region;
  uint32_t eval_id;
  uint32_t head_id;
  uint32_t then_id;
  uint32_t else_id = 0u;
  uint32_t join_id;

  eval_id = add_block(gb, E_TPL_EXPR_EVAL, "if-cond", 0);
  head_id = add_block(gb, E_TPL_IF_HEAD, "if-head", 1);
  then_region = build_stmt(gb, stmt->as.if_stmt.then_branch);
  then_id = add_block(gb, E_TPL_IF_THEN, "if-then", 0);
  join_id = add_block(gb, E_TPL_IF_JOIN, "if-join", 0);

  set_primary(gb->graph, eval_id, E_EDGE_FALLTHROUGH, head_id);
  set_primary(gb->graph, head_id, E_EDGE_TRUE, then_id);
  set_primary(gb->graph, then_id, E_EDGE_FALLTHROUGH, then_region.entry_id);

  if (stmt->as.if_stmt.else_branch) {
    else_region = build_stmt(gb, stmt->as.if_stmt.else_branch);
    else_id = add_block(gb, E_TPL_IF_ELSE, "if-else", 0);
    set_secondary(gb->graph, head_id, E_EDGE_FALSE, else_id);
    set_primary(gb->graph, else_id, E_EDGE_FALLTHROUGH, else_region.entry_id);
    if (find_block(gb->graph, else_region.exit_id)->primary_edge.kind == E_EDGE_NONE) {
      set_primary(gb->graph, else_region.exit_id, E_EDGE_JUMP, join_id);
    }
  } else {
    set_secondary(gb->graph, head_id, E_EDGE_FALSE, join_id);
  }

  if (find_block(gb->graph, then_region.exit_id)->primary_edge.kind == E_EDGE_NONE) {
    set_primary(gb->graph, then_region.exit_id, E_EDGE_JUMP, join_id);
  }

  out.entry_id = eval_id;
  out.exit_id = join_id;
  return out;
}

static Region build_switch(GraphBuild *gb, const EStmt *stmt) {
  Region out;
  uint32_t eval_id;
  uint32_t head_id;
  uint32_t join_id;
  uint32_t prev_head = 0u;
  size_t i;
  uint32_t saved_break_target;
  uint32_t saved_continue_target;

  eval_id = add_block(gb, E_TPL_EXPR_EVAL, "switch-target", 0);
  head_id = add_block(gb, E_TPL_SWITCH_HEAD, "switch-head", 1);
  join_id = add_block(gb, E_TPL_SWITCH_JOIN, "switch-join", 0);
  saved_break_target = gb->break_target;
  saved_continue_target = gb->continue_target;
  gb->break_target = join_id;

  set_primary(gb->graph, eval_id, E_EDGE_FALLTHROUGH, head_id);
  prev_head = head_id;

  for (i = 0; i < stmt->as.switch_stmt.case_count; i++) {
    const ESwitchCase *scase = &stmt->as.switch_stmt.cases[i];
    Region case_body = build_block(gb, &scase->body);
    uint32_t case_id = add_block(gb,
                                 scase->is_default ? E_TPL_SWITCH_DEFAULT : E_TPL_SWITCH_CASE,
                                 scase->is_default ? "switch-default" : "switch-case",
                                 0);

    set_primary(gb->graph, prev_head,
                scase->is_default ? E_EDGE_FALSE : E_EDGE_TRUE,
                case_id);
    set_primary(gb->graph, case_id, E_EDGE_FALLTHROUGH, case_body.entry_id);
    if (find_block(gb->graph, case_body.exit_id)->primary_edge.kind == E_EDGE_NONE) {
      set_primary(gb->graph, case_body.exit_id, E_EDGE_JUMP, join_id);
    }

    if (!scase->is_default) {
      uint32_t next_head = add_block(gb, E_TPL_SWITCH_HEAD, "switch-next-case", 1);
      set_secondary(gb->graph, prev_head, E_EDGE_FALSE, next_head);
      prev_head = next_head;
    }
  }

  if (find_block(gb->graph, prev_head)->primary_edge.kind == E_EDGE_NONE) {
    set_primary(gb->graph, prev_head, E_EDGE_FALSE, join_id);
  }

  gb->break_target = saved_break_target;
  gb->continue_target = saved_continue_target;

  out.entry_id = eval_id;
  out.exit_id = join_id;
  return out;
}

static Region build_while(GraphBuild *gb, const EStmt *stmt) {
  Region out;
  Region body_region;
  uint32_t eval_id;
  uint32_t head_id;
  uint32_t join_id;
  uint32_t saved_break_target;
  uint32_t saved_continue_target;

  eval_id = add_block(gb, E_TPL_EXPR_EVAL, "while-cond", 0);
  head_id = add_block(gb, E_TPL_WHILE_HEAD, "while-head", 1);
  join_id = add_block(gb, E_TPL_LOOP_JOIN, "while-join", 0);
  saved_break_target = gb->break_target;
  saved_continue_target = gb->continue_target;
  gb->break_target = join_id;
  gb->continue_target = eval_id;
  body_region = build_stmt(gb, stmt->as.while_stmt.body);

  set_primary(gb->graph, eval_id, E_EDGE_FALLTHROUGH, head_id);
  set_primary(gb->graph, head_id, E_EDGE_TRUE, body_region.entry_id);
  set_secondary(gb->graph, head_id, E_EDGE_FALSE, join_id);
  if (find_block(gb->graph, body_region.exit_id)->primary_edge.kind == E_EDGE_NONE) {
    set_primary(gb->graph, body_region.exit_id, E_EDGE_JUMP, eval_id);
  }

  gb->break_target = saved_break_target;
  gb->continue_target = saved_continue_target;
  out.entry_id = eval_id;
  out.exit_id = join_id;
  return out;
}

static Region build_for(GraphBuild *gb, const EStmt *stmt) {
  Region out;
  Region init_region;
  Region body_region;
  uint32_t head_id;
  uint32_t step_id;
  uint32_t join_id;
  uint32_t saved_break_target;
  uint32_t saved_continue_target;

  init_region = build_stmt(gb, stmt->as.for_stmt.init);
  head_id = add_block(gb, E_TPL_FOR_HEAD, "for-head", stmt->as.for_stmt.cond ? 1 : 0);
  step_id = add_block(gb, E_TPL_FOR_STEP, "for-step", 0);
  join_id = add_block(gb, E_TPL_LOOP_JOIN, "for-join", 0);
  saved_break_target = gb->break_target;
  saved_continue_target = gb->continue_target;
  gb->break_target = join_id;
  gb->continue_target = step_id;
  body_region = build_stmt(gb, stmt->as.for_stmt.body);

  if (find_block(gb->graph, init_region.exit_id)->primary_edge.kind == E_EDGE_NONE) {
    set_primary(gb->graph, init_region.exit_id, E_EDGE_FALLTHROUGH, head_id);
  }
  if (stmt->as.for_stmt.cond) {
    set_primary(gb->graph, head_id, E_EDGE_TRUE, body_region.entry_id);
    set_secondary(gb->graph, head_id, E_EDGE_FALSE, join_id);
  } else {
    set_primary(gb->graph, head_id, E_EDGE_FALLTHROUGH, body_region.entry_id);
  }
  if (find_block(gb->graph, body_region.exit_id)->primary_edge.kind == E_EDGE_NONE) {
    set_primary(gb->graph, body_region.exit_id, E_EDGE_JUMP, step_id);
  }
  set_primary(gb->graph, step_id, E_EDGE_JUMP, head_id);

  gb->break_target = saved_break_target;
  gb->continue_target = saved_continue_target;
  out.entry_id = init_region.entry_id;
  out.exit_id = join_id;
  return out;
}

static Region build_stmt(GraphBuild *gb, const EStmt *stmt) {
  if (!stmt) return build_simple(gb, E_TPL_SEQ, "null-stmt", 0);
  switch (stmt->kind) {
    case E_STMT_DECL: return build_simple(gb, E_TPL_SEQ, "decl", 0);
    case E_STMT_EXPR: return build_simple(gb, E_TPL_SEQ, "expr", 0);
    case E_STMT_RETURN: return build_simple(gb, E_TPL_RETURN, "return", 1);
    case E_STMT_BLOCK: return build_block(gb, &stmt->as.block);
    case E_STMT_IF: return build_if(gb, stmt);
    case E_STMT_WHILE: return build_while(gb, stmt);
    case E_STMT_FOR: return build_for(gb, stmt);
    case E_STMT_SWITCH: return build_switch(gb, stmt);
    case E_STMT_BREAK: {
      Region r = build_simple(gb, E_TPL_BREAK, "break", 0);
      if (gb->break_target != 0u) {
        set_primary(gb->graph, r.entry_id, E_EDGE_JUMP, gb->break_target);
      } else {
        set_primary(gb->graph, r.entry_id, E_EDGE_TERMINAL, 0u);
      }
      return r;
    }
    case E_STMT_CONTINUE: {
      Region r = build_simple(gb, E_TPL_CONTINUE, "continue", 0);
      if (gb->continue_target != 0u) {
        set_primary(gb->graph, r.entry_id, E_EDGE_JUMP, gb->continue_target);
      } else {
        set_primary(gb->graph, r.entry_id, E_EDGE_TERMINAL, 0u);
      }
      return r;
    }
    case E_STMT_NEXT: return build_simple(gb, E_TPL_SEQ, "next", 0);
    case E_STMT_RAW_EPA: return build_simple(gb, E_TPL_SEQ, "raw-epa", 0);
    case E_STMT_FOREACH: return build_while(gb, stmt);
    case E_STMT_DYNAMIC: return build_simple(gb, E_TPL_SEQ, "dynamic-decl", 0);
    case E_STMT_STATIC_BLOCK: return build_block(gb, &stmt->as.static_block);
  }
  return build_simple(gb, E_TPL_INVALID, "invalid", 1);
}

const char *e_template_kind_name(ETemplateKind kind) {
  switch (kind) {
    case E_TPL_INVALID: return "invalid";
    case E_TPL_EXPR_EVAL: return "expr-eval";
    case E_TPL_IF_HEAD: return "if-head";
    case E_TPL_IF_THEN: return "if-then";
    case E_TPL_IF_ELSE: return "if-else";
    case E_TPL_IF_JOIN: return "if-join";
    case E_TPL_WHILE_HEAD: return "while-head";
    case E_TPL_FOR_HEAD: return "for-head";
    case E_TPL_FOR_STEP: return "for-step";
    case E_TPL_LOOP_JOIN: return "loop-join";
    case E_TPL_SWITCH_HEAD: return "switch-head";
    case E_TPL_SWITCH_CASE: return "switch-case";
    case E_TPL_SWITCH_DEFAULT: return "switch-default";
    case E_TPL_SWITCH_JOIN: return "switch-join";
    case E_TPL_RETURN: return "return";
    case E_TPL_BREAK: return "break";
    case E_TPL_CONTINUE: return "continue";
    case E_TPL_SEQ: return "seq";
  }
  return "unknown";
}

const char *e_template_edge_kind_name(ETemplateEdgeKind kind) {
  switch (kind) {
    case E_EDGE_NONE: return "none";
    case E_EDGE_FALLTHROUGH: return "fallthrough";
    case E_EDGE_TRUE: return "true";
    case E_EDGE_FALSE: return "false";
    case E_EDGE_JUMP: return "jump";
    case E_EDGE_TERMINAL: return "terminal";
  }
  return "unknown";
}

int e_template_graph_build_from_stmt(const EStmt *stmt, ETemplateGraph *out_graph, char err[256]) {
  GraphBuild gb;
  Region r;
  (void)err;
  memset(out_graph, 0, sizeof(*out_graph));
  memset(&gb, 0, sizeof(gb));
  gb.graph = out_graph;
  gb.next_id = 1u;
  r = build_stmt(&gb, stmt);
  (void)r;
  return 1;
}

void e_template_graph_free(ETemplateGraph *graph) {
  if (!graph) return;
  free(graph->blocks);
  graph->blocks = NULL;
  graph->block_count = 0;
}

void e_template_graph_dump(FILE *out, const ETemplateGraph *graph) {
  size_t i;

  if (!graph || !out) return;
  fputs("template-graph\n", out);
  if (graph->block_count == 0) {
    fputs("  none\n", out);
    return;
  }

  for (i = 0; i < graph->block_count; i++) {
    const ETemplateBlock *b = &graph->blocks[i];
    fprintf(out, "  block %u kind=%s", b->block_id, e_template_kind_name(b->kind));
    if (b->debug_name && *b->debug_name) {
      fprintf(out, " name=%s", b->debug_name);
    }
    if (b->consumes_predicate) {
      fputs(" consumes_predicate=1", out);
    }
    fputc('\n', out);

    fprintf(out, "    edge primary=%s", e_template_edge_kind_name(b->primary_edge.kind));
    if (b->primary_edge.kind != E_EDGE_NONE && b->primary_edge.kind != E_EDGE_TERMINAL) {
      fprintf(out, " -> %u", b->primary_edge.target_block_id);
    }
    fputc('\n', out);

    fprintf(out, "    edge secondary=%s", e_template_edge_kind_name(b->secondary_edge.kind));
    if (b->secondary_edge.kind != E_EDGE_NONE && b->secondary_edge.kind != E_EDGE_TERMINAL) {
      fprintf(out, " -> %u", b->secondary_edge.target_block_id);
    }
    fputc('\n', out);
  }
}
