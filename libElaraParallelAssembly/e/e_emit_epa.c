#define _POSIX_C_SOURCE 200809L
#include "e_emit_epa.h"
#include "../src/epa_program_desc.h"
#include "../src/epa_asm_compiler.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Functions use 4-byte (u32) local slot indices; workers use 1-byte (u8). */
#define EMIT_LOAD_L(out, ctx, slot) \
    fprintf((out), "%s %u\n", (ctx)->current_function ? "LOAD_LW" : "LOAD_L", (slot))
#define EMIT_STORE_L(out, ctx, slot) \
    fprintf((out), "%s %u\n", (ctx)->current_function ? "STORE_LW" : "STORE_L", (slot))

typedef struct {
  char *literal;
  unsigned int id;
} EmitStringConst;

typedef struct EmitCtx EmitCtx;

typedef int (*EmitBuiltinFn)(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);

typedef struct {
  const char *name;
  EmitBuiltinFn fn;
} EmitBuiltinEntry;

struct EmitCtx {
  unsigned int next_label_id;
  unsigned int next_worker_id;
  unsigned int next_func_id;
  unsigned int next_string_id;
  const EProgram *prog;
  const ESemanticModel *model;
  const EFunction *current_function;
  const EWorker *current_worker;
  const EFunctionFrame *current_frame;
  EmitStringConst *strings;
  size_t string_count;
  /* Iterator variable → pool_id bindings, registered when dynamic_iterator() is emitted */
  struct { const char *iter_name; unsigned int pool_id; } iter_bindings[16];
  unsigned int iter_binding_count;
  /* Current type layout when inside a type body (for GHS field ident resolution) */
  const ETypeLayout *current_type_layout;
  /* Pre-built name → func_id map for user-defined function CALL emission */
  struct { const char *name; unsigned int func_id; } func_id_map[64];
  unsigned int func_id_map_count;
  /* Source line map: translates flat preprocessed line → (file, original-line) */
  const ELineMap *line_map;
  const char *main_file;
};

static void emit_indent(FILE *out, int depth) {
  int i;
  for (i = 0; i < depth; i++) fputs("  ", out);
}

static const EFunctionFrame *find_frame(const ESemanticModel *model, const char *name) {
  size_t i;
  for (i = 0; i < model->frame_count; i++) {
    if (strcmp(model->frames[i].owner_name, name) == 0) return &model->frames[i];
  }
  return NULL;
}

static const EFunctionFrame *active_frame_for_ctx(const EmitCtx *ctx) {
  if (ctx->current_frame) return ctx->current_frame;
  if (!ctx->current_worker && !ctx->current_function) {
    return find_frame(ctx->model, "kernel");
  }
  return NULL;
}

static const ELocalBinding *find_local_binding(const EFunctionFrame *frame, const EStmt *decl_stmt) {
  size_t i;
  if (!frame) return NULL;
  for (i = 0; i < frame->local_count; i++) {
    if (frame->locals[i].decl_stmt == decl_stmt) return &frame->locals[i];
  }
  return NULL;
}

static const ELocalBinding *find_local_binding_by_name(const EFunctionFrame *frame, const char *name) {
  size_t i;
  if (!frame) return NULL;
  for (i = 0; i < frame->local_count; i++) {
    if (strcmp(frame->locals[i].name, name) == 0) return &frame->locals[i];
  }
  return NULL;
}

static const EFunctionParamCheck *find_param_check_by_name(const EmitCtx *ctx, const char *name) {
  size_t i;
  if (!ctx->current_function) return NULL;
  for (i = 0; i < ctx->model->check_count; i++) {
    const EFunctionParamCheck *check = &ctx->model->checks[i];
    if (check->function != ctx->current_function) continue;
    if (strcmp(ctx->current_function->params[check->param_index].name, name) == 0) return check;
  }
  return NULL;
}

static const EValidatorBinding *find_validator_binding(const ESemanticModel *model, const char *name) {
  size_t i;
  for (i = 0; i < model->validator_count; i++) {
    if (strcmp(model->validators[i].type_name, name) == 0) return &model->validators[i];
  }
  return NULL;
}

static int scalar_vm_local_slot_for_name(const EmitCtx *ctx, const char *name, unsigned int *out_slot) {
  const ELocalBinding *local = find_local_binding_by_name(active_frame_for_ctx(ctx), name);
  const EFunctionParamCheck *param = find_param_check_by_name(ctx, name);
  if (local && local->vm_local_words == 1u) {
    *out_slot = local->vm_local_slot;
    return 1;
  }
  if (param && param->vm_local_words == 1u) {
    *out_slot = param->vm_local_slot;
    return 1;
  }
  return 0;
}

static int type_ref_vm_local_slot_for_name(const EmitCtx *ctx, const char *name, unsigned int *out_slot) {
  const ELocalBinding *local = find_local_binding_by_name(active_frame_for_ctx(ctx), name);
  const EFunctionParamCheck *param = find_param_check_by_name(ctx, name);
  if (local && local->vm_local_words == 2u) {
    *out_slot = local->vm_local_slot;
    return 1;
  }
  if (param && param->vm_local_words == 2u) {
    *out_slot = param->vm_local_slot;
    return 1;
  }
  return 0;
}

static const ETypeLayout *find_type_layout(const ESemanticModel *model, const char *name) {
  size_t i;
  for (i = 0; i < model->type_layout_count; i++) {
    if (strcmp(model->type_layouts[i].type_name, name) == 0) return &model->type_layouts[i];
  }
  return NULL;
}

static const EDynamicPool *find_dynamic_pool(const ESemanticModel *model, const char *name) {
  size_t i;
  for (i = 0; i < model->dynamic_pool_count; i++) {
    if (strcmp(model->dynamic_pools[i].name, name) == 0) return &model->dynamic_pools[i];
  }
  return NULL;
}

static const EGhsField *find_field_layout(const ESemanticModel *model, const char *type_name, const char *field) {
  const ETypeLayout *layout = find_type_layout(model, type_name);
  size_t i;
  if (!layout) return NULL;
  for (i = 0; i < layout->field_count; i++) {
    if (strcmp(layout->fields[i].name, field) == 0) return &layout->fields[i];
  }
  return NULL;
}

static char *xstrdup_local_emit(const char *s) {
  size_t n = strlen(s);
  char *p = (char*)malloc(n + 1u);
  if (!p) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  memcpy(p, s, n + 1u);
  return p;
}

static unsigned int intern_string_const(EmitCtx *ctx, const char *literal) {
  size_t i;
  for (i = 0; i < ctx->string_count; i++) {
    if (strcmp(ctx->strings[i].literal, literal) == 0) return ctx->strings[i].id;
  }
  ctx->strings = (EmitStringConst*)realloc(ctx->strings, sizeof(EmitStringConst) * (ctx->string_count + 1u));
  if (!ctx->strings) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  ctx->strings[ctx->string_count].literal = xstrdup_local_emit(literal);
  ctx->strings[ctx->string_count].id = ctx->next_string_id++;
  ctx->string_count++;
  return ctx->strings[ctx->string_count - 1u].id;
}

static void collect_strings_in_expr(EmitCtx *ctx, const EExpr *expr);

static void collect_strings_in_stmt(EmitCtx *ctx, const EStmt *stmt) {
  size_t i;
  if (!stmt) return;
  switch (stmt->kind) {
    case E_STMT_DECL:
      collect_strings_in_expr(ctx, stmt->as.decl.init);
      break;
    case E_STMT_RETURN:
      collect_strings_in_expr(ctx, stmt->as.ret.value);
      break;
    case E_STMT_EXPR:
      collect_strings_in_expr(ctx, stmt->as.expr);
      break;
    case E_STMT_BLOCK:
      for (i = 0; i < stmt->as.block.count; i++) collect_strings_in_stmt(ctx, stmt->as.block.items[i]);
      break;
    case E_STMT_IF:
      collect_strings_in_expr(ctx, stmt->as.if_stmt.cond);
      collect_strings_in_stmt(ctx, stmt->as.if_stmt.then_branch);
      collect_strings_in_stmt(ctx, stmt->as.if_stmt.else_branch);
      break;
    case E_STMT_WHILE:
      collect_strings_in_expr(ctx, stmt->as.while_stmt.cond);
      collect_strings_in_stmt(ctx, stmt->as.while_stmt.body);
      break;
    case E_STMT_FOR:
      collect_strings_in_stmt(ctx, stmt->as.for_stmt.init);
      collect_strings_in_expr(ctx, stmt->as.for_stmt.cond);
      collect_strings_in_expr(ctx, stmt->as.for_stmt.step);
      collect_strings_in_stmt(ctx, stmt->as.for_stmt.body);
      break;
    case E_STMT_SWITCH:
      collect_strings_in_expr(ctx, stmt->as.switch_stmt.target);
      for (i = 0; i < stmt->as.switch_stmt.case_count; i++) {
        size_t j;
        for (j = 0; j < stmt->as.switch_stmt.cases[i].body.count; j++) {
          collect_strings_in_stmt(ctx, stmt->as.switch_stmt.cases[i].body.items[j]);
        }
      }
      break;
    case E_STMT_FOREACH:
      collect_strings_in_stmt(ctx, stmt->as.foreach_stmt.body);
      break;
    case E_STMT_STATIC_BLOCK:
      for (i = 0; i < stmt->as.static_block.count; i++) collect_strings_in_stmt(ctx, stmt->as.static_block.items[i]);
      break;
    case E_STMT_BREAK:
    case E_STMT_CONTINUE:
    case E_STMT_NEXT:
    case E_STMT_RAW_EPA:
    case E_STMT_DYNAMIC:
      break;
  }
}

static void collect_strings_in_expr(EmitCtx *ctx, const EExpr *expr) {
  size_t i;
  if (!expr) return;
  switch (expr->kind) {
    case E_EXPR_STRING:
      intern_string_const(ctx, expr->as.string_lit);
      break;
    case E_EXPR_BINARY:
      collect_strings_in_expr(ctx, expr->as.binary.lhs);
      collect_strings_in_expr(ctx, expr->as.binary.rhs);
      break;
    case E_EXPR_ASSIGN:
      collect_strings_in_expr(ctx, expr->as.assign.lhs);
      collect_strings_in_expr(ctx, expr->as.assign.rhs);
      break;
    case E_EXPR_CALL:
      for (i = 0; i < expr->as.call.arg_count; i++) collect_strings_in_expr(ctx, expr->as.call.args[i]);
      break;
    case E_EXPR_FIELD:
      collect_strings_in_expr(ctx, expr->as.field.base);
      break;
    case E_EXPR_INDEX:
      collect_strings_in_expr(ctx, expr->as.index.base);
      collect_strings_in_expr(ctx, expr->as.index.index);
      break;
    case E_EXPR_IDENT:
    case E_EXPR_INT:
      break;
  }
}

static void collect_program_strings(EmitCtx *ctx, const EProgram *prog) {
  size_t i;
  for (i = 0; i < prog->count; i++) {
    const ETopDecl *top = &prog->items[i];
    switch (top->kind) {
      case E_TOP_KERNEL: collect_strings_in_stmt(ctx, top->as.kernel.body); break;
      case E_TOP_WORKER: collect_strings_in_stmt(ctx, top->as.worker.body); break;
      case E_TOP_FUNCTION: collect_strings_in_stmt(ctx, top->as.func.body); break;
      case E_TOP_TYPE: collect_strings_in_stmt(ctx, top->as.tdecl.body); break;
      case E_TOP_STRUCT:
      case E_TOP_DECLARE:
      case E_TOP_DYNAMIC:
        break;
    }
  }
}

static void emit_expr(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_log_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_kernel_wait_signal_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_signal_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_host_signal_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_far_signal_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_frame_begin_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_frame_rect_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_frame_line_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_frame_commit_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_kernel_get_ghs_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_request_threads_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_typeof_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_typeid_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_dyn_alloc_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_dyn_free_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_dyn_swap_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_dyn_iterator_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_dyn_next_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_scalar_store(FILE *out, const char *name, EmitCtx *ctx, int depth);
static int emit_worker_field_load(FILE *out, const char *base_name, const char *field_name, EmitCtx *ctx, int depth);
static int emit_target_string_ref_to_regs(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static void emit_zero_fill_static_type(FILE *out, const ELocalBinding *binding, EmitCtx *ctx, int depth);
static unsigned int next_label(EmitCtx *ctx);
static int emit_dynamic_index_load(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_dynamic_index_store(FILE *out, const EExpr *lhs, const EExpr *rhs, EmitCtx *ctx, int depth);
static int emit_call_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);

static void emit_field_path(FILE *out, const EExpr *expr, const ESemanticModel *model) {
  if (!expr) return;
  if (expr->kind == E_EXPR_IDENT) {
    fprintf(out, "%s", expr->as.ident);
    return;
  }
  if (expr->kind == E_EXPR_FIELD) {
    emit_field_path(out, expr->as.field.base, model);
    fprintf(out, ".%s", expr->as.field.field);
    if (expr->as.field.base && expr->as.field.base->kind == E_EXPR_IDENT) {
      const char *base_name = expr->as.field.base->as.ident;
      size_t i;
      for (i = 0; i < model->worker_count; i++) {
        const EWorker *w = model->workers[i];
        if (w->param_count == 1u && strcmp(w->params[0].name, base_name) == 0) {
          const EGhsField *field = find_field_layout(model, w->params[0].type.name, expr->as.field.field);
          if (field) {
            fprintf(out, " [ghs+%zu]", field->ghs_offset);
          }
          return;
        }
      }
      for (i = 0; i < model->function_count; i++) {
        const EFunction *fn = model->functions[i];
        size_t j;
        for (j = 0; j < fn->param_count; j++) {
          if (strcmp(fn->params[j].name, base_name) == 0) {
            const EGhsField *field = find_field_layout(model, fn->params[j].type.name, expr->as.field.field);
            if (field) {
              fprintf(out, " [ghs+%zu]", field->ghs_offset);
            }
            return;
          }
        }
      }
    }
  }
}

static int expr_is_dynamic_index(const EExpr *expr, const ESemanticModel *model, const EDynamicPool **out_pool) {
  const EDynamicPool *pool;
  if (!expr || expr->kind != E_EXPR_INDEX) return 0;
  if (!expr->as.index.base || expr->as.index.base->kind != E_EXPR_IDENT) return 0;
  pool = find_dynamic_pool(model, expr->as.index.base->as.ident);
  if (!pool) return 0;
  if (out_pool) *out_pool = pool;
  return 1;
}

static unsigned int dynamic_pool_id(const EmitCtx *ctx, const EDynamicPool *pool) {
  return (unsigned int)(pool - ctx->model->dynamic_pools);
}

static unsigned int emit_temp_scalar_slot(const EmitCtx *ctx) {
  const EFunctionFrame *frame = active_frame_for_ctx(ctx);
  return frame ? frame->vm_local_count : 0u;
}

static unsigned int emit_temp_ref_slot(const EmitCtx *ctx) {
  const EFunctionFrame *frame = active_frame_for_ctx(ctx);
  return frame ? frame->vm_local_count + 1u : 1u;
}

static int emit_dynamic_index_load(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  const EDynamicPool *pool;
  if (!expr_is_dynamic_index(expr, ctx->model, &pool)) return 0;
  emit_expr(out, expr->as.index.index, ctx, depth);
  emit_indent(out, depth);
  fputs("POP R0\n", out);
  emit_indent(out, depth);
  fprintf(out, "DYN_LOAD %u\n", dynamic_pool_id(ctx, pool));
  emit_indent(out, depth);
  fputs("PUSH R0\n", out);
  emit_indent(out, depth);
  fputs("PUSH R1\n", out);
  return 1;
}

static int emit_dynamic_index_store(FILE *out, const EExpr *lhs, const EExpr *rhs, EmitCtx *ctx, int depth) {
  const EDynamicPool *pool;
  unsigned int id_slot = emit_temp_scalar_slot(ctx);
  unsigned int ref_slot = emit_temp_ref_slot(ctx);
  if (!expr_is_dynamic_index(lhs, ctx->model, &pool)) return 0;
  emit_expr(out, lhs->as.index.index, ctx, depth);
  emit_indent(out, depth);
  fputs("POP R0\n", out);
  emit_indent(out, depth);
  EMIT_STORE_L(out, ctx, id_slot);

  emit_expr(out, rhs, ctx, depth);
  emit_indent(out, depth);
  fputs("POP R1\n", out);
  emit_indent(out, depth);
  EMIT_STORE_L(out, ctx, ref_slot + 1u);
  emit_indent(out, depth);
  fputs("POP R1\n", out);
  emit_indent(out, depth);
  EMIT_STORE_L(out, ctx, ref_slot);

  emit_indent(out, depth);
  EMIT_LOAD_L(out, ctx, id_slot);
  emit_indent(out, depth);
  fputs("POP R0\n", out);
  emit_indent(out, depth);
  EMIT_LOAD_L(out, ctx, ref_slot);
  emit_indent(out, depth);
  fputs("POP R1\n", out);
  emit_indent(out, depth);
  EMIT_LOAD_L(out, ctx, ref_slot + 1u);
  emit_indent(out, depth);
  fputs("POP R2\n", out);
  emit_indent(out, depth);
  fprintf(out, "DYN_STORE %u\n", dynamic_pool_id(ctx, pool));
  emit_indent(out, depth);
  EMIT_LOAD_L(out, ctx, ref_slot);
  emit_indent(out, depth);
  EMIT_LOAD_L(out, ctx, ref_slot + 1u);
  return 1;
}

static int emit_call_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  static const EmitBuiltinEntry kBuiltins[] = {
    {"log", emit_log_builtin},
    {"kernel_wait_signal", emit_kernel_wait_signal_builtin},
    {"signal", emit_signal_builtin},
    {"kernel_signal", emit_signal_builtin},
    {"host_signal", emit_host_signal_builtin},
    {"frame_begin", emit_frame_begin_builtin},
    {"frame_rect", emit_frame_rect_builtin},
    {"frame_line", emit_frame_line_builtin},
    {"frame_commit", emit_frame_commit_builtin},
    {"far_signal", emit_far_signal_builtin},
    {"request_threads", emit_request_threads_builtin},
    {"kernal_get_ghs", emit_kernel_get_ghs_builtin},
    {"kernel_get_ghs", emit_kernel_get_ghs_builtin},
    {"typeof", emit_typeof_builtin},
    {"typeid", emit_typeid_builtin},
    {"dyn_alloc", emit_dyn_alloc_builtin},
    {"dyn_free", emit_dyn_free_builtin},
    {"dyn_swap", emit_dyn_swap_builtin},
    {"dynamic_iterator", emit_dyn_iterator_builtin},
    {"dynamic_next", emit_dyn_next_builtin},
  };
  size_t i;
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  for (i = 0; i < sizeof(kBuiltins) / sizeof(kBuiltins[0]); i++) {
    if (strcmp(expr->as.call.callee, kBuiltins[i].name) == 0) {
      return kBuiltins[i].fn(out, expr, ctx, depth);
    }
  }
  return 0;
}

static void build_func_id_map(EmitCtx *ctx, const EProgram *prog) {
  size_t i;
  unsigned int next_id = 1u;
  ctx->func_id_map_count = 0u;
  for (i = 0; i < prog->count && ctx->func_id_map_count < 64u; i++) {
    const ETopDecl *top = &prog->items[i];
    if (top->kind == E_TOP_TYPE) {
      ctx->func_id_map[ctx->func_id_map_count].name = top->as.tdecl.name;
      ctx->func_id_map[ctx->func_id_map_count].func_id = next_id++;
      ctx->func_id_map_count++;
    } else if (top->kind == E_TOP_FUNCTION) {
      ctx->func_id_map[ctx->func_id_map_count].name = top->as.func.name;
      ctx->func_id_map[ctx->func_id_map_count].func_id = next_id++;
      ctx->func_id_map_count++;
    }
  }
}

static int find_user_func_id(const EmitCtx *ctx, const char *name, unsigned int *out_id) {
  size_t i;
  for (i = 0; i < ctx->func_id_map_count; i++) {
    if (strcmp(ctx->func_id_map[i].name, name) == 0) {
      *out_id = ctx->func_id_map[i].func_id;
      return 1;
    }
  }
  return 0;
}

static int user_func_has_return_value(const EmitCtx *ctx, const char *name) {
  size_t i;
  for (i = 0; i < ctx->prog->count; i++) {
    const ETopDecl *top = &ctx->prog->items[i];
    if (top->kind != E_TOP_FUNCTION) continue;
    if (strcmp(top->as.func.name, name) != 0) continue;
    return top->as.func.return_type.name &&
           top->as.func.return_type.name[0] != '\0' &&
           strcmp(top->as.func.return_type.name, "void") != 0;
  }
  return 0;
}

static void emit_expr(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);

static int emit_user_func_call(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  unsigned int func_id = 0u;
  const EFunctionFrame *callee_frame;
  size_t i;
  if (!find_user_func_id(ctx, expr->as.call.callee, &func_id)) return 0;
  callee_frame = find_frame(ctx->model, expr->as.call.callee);
  for (i = 0; i < expr->as.call.arg_count; i++) {
    size_t j;
    int stored = 0;
    for (j = 0; j < ctx->model->check_count; j++) {
      const EFunctionParamCheck *check = &ctx->model->checks[j];
      if (check->param_index != i) continue;
      if (!callee_frame || check->function != callee_frame->function) continue;
      if (check->vm_local_words == 1u) {
        emit_expr(out, expr->as.call.args[i], ctx, depth);
        emit_indent(out, depth);
        EMIT_STORE_L(out, ctx, check->vm_local_slot);
        stored = 1;
      } else if (check->vm_local_words == 2u) {
        emit_expr(out, expr->as.call.args[i], ctx, depth);
        emit_indent(out, depth);
        fputs("POP R1\n", out);
        emit_indent(out, depth);
        EMIT_STORE_L(out, ctx, check->vm_local_slot + 1u);
        emit_indent(out, depth);
        fputs("POP R0\n", out);
        emit_indent(out, depth);
        EMIT_STORE_L(out, ctx, check->vm_local_slot);
        stored = 1;
      } else {
        emit_indent(out, depth);
        fprintf(out, "; arg %zu type-ref — no scalar slot\n", i);
        stored = 1;
      }
      break;
    }
    if (!stored) {
      emit_expr(out, expr->as.call.args[i], ctx, depth);
      emit_indent(out, depth);
      fprintf(out, "; arg %zu no param check\n", i);
    }
  }
  emit_indent(out, depth);
  fprintf(out, "CALL %u\n", func_id);
  return 1;
}

static void emit_expr(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  size_t i;
  if (!expr) {
    emit_indent(out, depth);
    fputs("; <null-expr>\n", out);
    return;
  }
  switch (expr->kind) {
    case E_EXPR_IDENT:
    {
      unsigned int slot = 0u;
      emit_indent(out, depth);
      if (scalar_vm_local_slot_for_name(ctx, expr->as.ident, &slot)) {
        EMIT_LOAD_L(out, ctx, slot);
      } else if (type_ref_vm_local_slot_for_name(ctx, expr->as.ident, &slot)) {
        EMIT_LOAD_L(out, ctx, slot);
        emit_indent(out, depth);
        EMIT_LOAD_L(out, ctx, slot + 1u);
      } else if (find_dynamic_pool(ctx->model, expr->as.ident)) {
        fprintf(out, "; dynamic pool ident %s\n", expr->as.ident);
      } else if (ctx->current_type_layout) {
        /* Inside a type body: resolve ident as a GHS field of this type (R0/R1 = GHS handle) */
        size_t fi;
        int found_field = 0;
        for (fi = 0; fi < ctx->current_type_layout->field_count; fi++) {
          if (strcmp(ctx->current_type_layout->fields[fi].name, expr->as.ident) == 0) {
            const EGhsField *f = &ctx->current_type_layout->fields[fi];
            if (f->ghs_size == 4u) {
              fprintf(out, "SET_R 2 %zu\n", f->ghs_offset);
              emit_indent(out, depth);
              fputs("GR_MOV4 0\n", out);
              emit_indent(out, depth);
              fputs("PUSH R0\n", out);
            } else {
              fprintf(out, "; type field %s size=%zu (non-4-byte read pending)\n", expr->as.ident, f->ghs_size);
            }
            found_field = 1;
            break;
          }
        }
        if (!found_field) {
          fprintf(out, "; expr ident %s\n", expr->as.ident);
        }
      } else {
        fprintf(out, "; expr ident %s\n", expr->as.ident);
      }
      break;
    }
    case E_EXPR_INT:
      emit_indent(out, depth);
      fprintf(out, "PUSH %lld\n", expr->as.int_lit);
      break;
    case E_EXPR_STRING:
      emit_indent(out, depth);
      fprintf(out, "; expr string %s\n", expr->as.string_lit);
      break;
    case E_EXPR_BINARY:
      emit_expr(out, expr->as.binary.lhs, ctx, depth);
      emit_expr(out, expr->as.binary.rhs, ctx, depth);
      emit_indent(out, depth);
      switch (expr->as.binary.op) {
        case E_BIN_ADD: fputs("ADD_I32\n", out); break;
        case E_BIN_SUB: fputs("SUB_I32\n", out); break;
        case E_BIN_MUL: fputs("MUL_I32\n", out); break;
        case E_BIN_DIV: fputs("DIV_I32\n", out); break;
        case E_BIN_EQ: fputs("EQ_I32\n", out); break;
        case E_BIN_NE: fputs("NE_I32\n", out); break;
        case E_BIN_LT: fputs("LT_I32\n", out); break;
        case E_BIN_LE: fputs("LE_I32\n", out); break;
        case E_BIN_GT: fputs("GT_I32\n", out); break;
        case E_BIN_GE: fputs("GE_I32\n", out); break;
      }
      break;
    case E_EXPR_ASSIGN:
      if (expr_is_dynamic_index(expr->as.assign.lhs, ctx->model, NULL)) {
        emit_dynamic_index_store(out, expr->as.assign.lhs, expr->as.assign.rhs, ctx, depth);
      } else {
        emit_expr(out, expr->as.assign.rhs, ctx, depth);
      }
      if (expr->as.assign.lhs && expr->as.assign.lhs->kind == E_EXPR_IDENT) {
        {
          unsigned int slot = 0u;
          if (scalar_vm_local_slot_for_name(ctx, expr->as.assign.lhs->as.ident, &slot)) {
            emit_scalar_store(out, expr->as.assign.lhs->as.ident, ctx, depth);
            emit_indent(out, depth);
            EMIT_LOAD_L(out, ctx, slot);
          } else if (type_ref_vm_local_slot_for_name(ctx, expr->as.assign.lhs->as.ident, &slot)) {
            emit_indent(out, depth);
            fputs("POP R1\n", out);
            emit_indent(out, depth);
            EMIT_STORE_L(out, ctx, slot + 1u);
            emit_indent(out, depth);
            fputs("POP R0\n", out);
            emit_indent(out, depth);
            EMIT_STORE_L(out, ctx, slot);
            emit_indent(out, depth);
            EMIT_LOAD_L(out, ctx, slot);
            emit_indent(out, depth);
            EMIT_LOAD_L(out, ctx, slot + 1u);
          } else {
            emit_indent(out, depth);
            fprintf(out, "; assign target %s unsupported\n", expr->as.assign.lhs->as.ident);
          }
        }
      } else if (expr_is_dynamic_index(expr->as.assign.lhs, ctx->model, NULL)) {
        /* emit_dynamic_index_store already leaves the assigned ref on stack. */
      } else {
        emit_indent(out, depth);
        fputs("; assign pending lowering\n", out);
      }
      break;
    case E_EXPR_CALL:
      if (emit_call_builtin(out, expr, ctx, depth)) {
        break;
      }
      if (emit_user_func_call(out, expr, ctx, depth)) {
        break;
      }
      emit_indent(out, depth);
      fprintf(out, "; call %s argc=%zu — unknown callee\n", expr->as.call.callee, expr->as.call.arg_count);
      break;
    case E_EXPR_FIELD:
      if (expr->as.field.base && expr->as.field.base->kind == E_EXPR_IDENT &&
          emit_worker_field_load(out, expr->as.field.base->as.ident, expr->as.field.field, ctx, depth)) {
        break;
      }
      emit_indent(out, depth);
      fputs("; field ", out);
      emit_field_path(out, expr, ctx->model);
      fputc('\n', out);
      break;
    case E_EXPR_INDEX:
      if (emit_dynamic_index_load(out, expr, ctx, depth)) break;
      emit_indent(out, depth);
      fputs("; index pending lowering\n", out);
      break;
  }
}

static int emit_scalar_store(FILE *out, const char *name, EmitCtx *ctx, int depth) {
  unsigned int slot = 0u;
  emit_indent(out, depth);
  if (scalar_vm_local_slot_for_name(ctx, name, &slot)) {
    EMIT_STORE_L(out, ctx, slot);
    return 1;
  }
  fprintf(out, "; store %s unsupported\n", name);
  return 0;
}

static int emit_worker_field_load(FILE *out, const char *base_name, const char *field_name, EmitCtx *ctx, int depth) {
  const EGhsField *field;

  /* GHS path: worker's typed ingress parameter backed by the Global Handle Space */
  {
    const EWorker *worker = ctx->current_worker;
    if (worker && worker->param_count == 1u &&
        strcmp(worker->params[0].name, base_name) == 0) {
      field = find_field_layout(ctx->model, worker->params[0].type.name, field_name);
      if (field && field->ghs_size == 4u) {
        emit_indent(out, depth);
        fprintf(out, "SET_R 2 %zu\n", field->ghs_offset);
        emit_indent(out, depth);
        fputs("GR_MOV4 0\n", out);
        emit_indent(out, depth);
        fputs("PUSH R0\n", out);
        return 1;
      }
    }
  }

  /* Local type-ref path: foreach vars, local typed refs backed by the lbytes arena.
     The 2-word binding stores (off, off+1=size); element data lives at lbytes[off]. */
  {
    const ELocalBinding *binding = find_local_binding_by_name(active_frame_for_ctx(ctx), base_name);
    if (binding && binding->vm_local_words == 2u) {
      field = find_field_layout(ctx->model, binding->type_name, field_name);
      if (field && field->ghs_size == 4u) {
        emit_indent(out, depth);
        EMIT_LOAD_L(out, ctx, binding->vm_local_slot);  /* push element base off */
        if (field->ghs_offset > 0u) {
          emit_indent(out, depth);
          fprintf(out, "PUSH %zu\n", field->ghs_offset);
          emit_indent(out, depth);
          fputs("ADD_I32\n", out);
        }
        emit_indent(out, depth);
        fputs("POP R0\n", out);          /* R0 = lbytes read index */
        emit_indent(out, depth);
        fputs("LBR_MOV4 R0 R0\n", out); /* R0 = u32 at lbytes[R0] */
        emit_indent(out, depth);
        fputs("PUSH R0\n", out);
        return 1;
      }
    }
  }

  return 0;
}

static void emit_zero_fill_static_type(FILE *out, const ELocalBinding *binding, EmitCtx *ctx, int depth) {
  unsigned int loop_id = next_label(ctx);
  unsigned int done_id = next_label(ctx);
  emit_indent(out, depth);
  fputs("; zero-fill declared-type static allocation\n", out);
  emit_indent(out, depth);
  fprintf(out, "PUSH R0\n");
  emit_indent(out, depth);
  EMIT_STORE_L(out, ctx, binding->vm_local_slot);
  emit_indent(out, depth);
  fprintf(out, "PUSH R1\n");
  emit_indent(out, depth);
  EMIT_STORE_L(out, ctx, binding->vm_local_slot + 1u);
  emit_indent(out, depth);
  fputs("SET_R 2 0\n", out);
  emit_indent(out, depth);
  fputs("PUSH R0\n", out);
  emit_indent(out, depth);
  fputs("PUSH R1\n", out);
  emit_indent(out, depth);
  fputs("ADD_I32\n", out);
  emit_indent(out, depth);
  fputs("POP R3\n", out);
  emit_indent(out, depth);
  fprintf(out, "E_STATIC_ZERO_%u:\n", loop_id);
  emit_indent(out, depth);
  fputs("PUSH R0\n", out);
  emit_indent(out, depth);
  fputs("PUSH R3\n", out);
  emit_indent(out, depth);
  fputs("LT_I32\n", out);
  emit_indent(out, depth);
  fputs("POP 0\n", out);
  emit_indent(out, depth);
  fprintf(out, "JZ E_STATIC_ZERO_DONE_%u\n", done_id);
  emit_indent(out, depth);
  fputs("RLB_MOV1 2 0\n", out);
  emit_indent(out, depth);
  fputs("PUSH R0\n", out);
  emit_indent(out, depth);
  fputs("PUSH 1\n", out);
  emit_indent(out, depth);
  fputs("ADD_I32\n", out);
  emit_indent(out, depth);
  fputs("POP R0\n", out);
  emit_indent(out, depth);
  fprintf(out, "JMP E_STATIC_ZERO_%u\n", loop_id);
  emit_indent(out, depth);
  fprintf(out, "E_STATIC_ZERO_DONE_%u:\n", done_id);
  emit_indent(out, depth);
  EMIT_LOAD_L(out, ctx, binding->vm_local_slot);
  emit_indent(out, depth);
  fputs("POP R0\n", out);
  emit_indent(out, depth);
  EMIT_LOAD_L(out, ctx, binding->vm_local_slot + 1u);
  emit_indent(out, depth);
  fputs("POP R1\n", out);
}

static unsigned int find_string_const_id(const EmitCtx *ctx, const char *literal) {
  size_t i;
  for (i = 0; i < ctx->string_count; i++) {
    if (strcmp(ctx->strings[i].literal, literal) == 0) return ctx->strings[i].id;
  }
  return 0u;
}

static int emit_log_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  size_t i;
  unsigned int string_id;
  if (expr->as.call.arg_count < 1u) {
    emit_indent(out, depth);
    fputs("; log requires at least a format string\n", out);
    return 1;
  }
  if (expr->as.call.args[0]->kind != E_EXPR_STRING) {
    emit_indent(out, depth);
    fputs("; log requires a string literal as the first argument\n", out);
    return 1;
  }
  string_id = find_string_const_id(ctx, expr->as.call.args[0]->as.string_lit);
  emit_indent(out, depth);
  fprintf(out, "LOAD_CONST %u\n", string_id);
  for (i = 1; i < expr->as.call.arg_count; i++) {
    emit_expr(out, expr->as.call.args[i], ctx, depth);
  }
  emit_indent(out, depth);
  fprintf(out, "FMT %zu\n", expr->as.call.arg_count - 1u);
  emit_indent(out, depth);
  fputs("LOG\n", out);
  return 1;
}

static int emit_kernel_wait_signal_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 0u) {
    emit_indent(out, depth);
    fprintf(out, "; kernel_wait_signal expects 0 args, got %zu\n", expr->as.call.arg_count);
    return 0;
  }
  if (ctx->current_worker || ctx->current_function) {
    emit_indent(out, depth);
    fputs("; kernel_wait_signal only valid in kernel\n", out);
    return 0;
  }
  emit_indent(out, depth);
  fputs("WAIT_ON_SYNC\n", out);
  emit_indent(out, depth);
  fputs("POP R0\n", out);
  emit_indent(out, depth);
  fputs("PUSH R0\n", out);
  return 1;
}

static int emit_signal_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 0u) {
    emit_indent(out, depth);
    fprintf(out, "; %s expects 0 args, got %zu\n", expr->as.call.callee, expr->as.call.arg_count);
    return 0;
  }
  if (!ctx->current_worker) {
    emit_indent(out, depth);
    fprintf(out, "; %s only valid in workers\n", expr->as.call.callee);
    return 0;
  }
  emit_indent(out, depth);
  fputs("SIGNAL\n", out);
  return 1;
}

static int emit_host_signal_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 0u) {
    emit_indent(out, depth);
    fprintf(out, "; host_signal expects 0 args, got %zu\n", expr->as.call.arg_count);
    return 0;
  }
  if (!ctx->current_worker) {
    emit_indent(out, depth);
    fputs("; host_signal only valid in workers\n", out);
    return 0;
  }
  emit_indent(out, depth);
  fputs("HOST_SIGNAL\n", out);
  return 1;
}

static int emit_mailbox_write_expr(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  emit_expr(out, expr, ctx, depth);
  emit_indent(out, depth);
  fputs("POP R0\n", out);
  emit_indent(out, depth);
  fputs("SM_PUT\n", out);
  return 1;
}

static int emit_mailbox_write_i32(FILE *out, int value, int depth) {
  emit_indent(out, depth);
  fprintf(out, "SET_R 0 %d\n", value);
  emit_indent(out, depth);
  fputs("SM_PUT\n", out);
  return 1;
}

static int emit_frame_begin_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 5u) {
    emit_indent(out, depth);
    fprintf(out, "; frame_begin expects 5 args, got %zu\n", expr->as.call.arg_count);
    return 0;
  }
  if (!ctx->current_worker) {
    emit_indent(out, depth);
    fputs("; frame_begin only valid in workers\n", out);
    return 0;
  }
  emit_indent(out, depth);
  fputs("SET_R 3 0\n", out);
  emit_mailbox_write_i32(out, 0x45465231, depth);
  emit_mailbox_write_i32(out, 1, depth);
  emit_mailbox_write_expr(out, expr->as.call.args[0], ctx, depth);
  emit_mailbox_write_expr(out, expr->as.call.args[1], ctx, depth);
  emit_mailbox_write_expr(out, expr->as.call.args[2], ctx, depth);
  emit_mailbox_write_expr(out, expr->as.call.args[3], ctx, depth);
  emit_mailbox_write_expr(out, expr->as.call.args[4], ctx, depth);
  return 1;
}

static int emit_frame_rect_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  size_t i;
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 7u) {
    emit_indent(out, depth);
    fprintf(out, "; frame_rect expects 7 args, got %zu\n", expr->as.call.arg_count);
    return 0;
  }
  if (!ctx->current_worker) {
    emit_indent(out, depth);
    fputs("; frame_rect only valid in workers\n", out);
    return 0;
  }
  emit_mailbox_write_i32(out, 1, depth);
  for (i = 0; i < 7u; i++) emit_mailbox_write_expr(out, expr->as.call.args[i], ctx, depth);
  return 1;
}

static int emit_frame_line_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  size_t i;
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 8u) {
    emit_indent(out, depth);
    fprintf(out, "; frame_line expects 8 args, got %zu\n", expr->as.call.arg_count);
    return 0;
  }
  if (!ctx->current_worker) {
    emit_indent(out, depth);
    fputs("; frame_line only valid in workers\n", out);
    return 0;
  }
  emit_mailbox_write_i32(out, 2, depth);
  for (i = 0; i < 8u; i++) emit_mailbox_write_expr(out, expr->as.call.args[i], ctx, depth);
  return 1;
}

static int emit_frame_commit_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 0u) {
    emit_indent(out, depth);
    fprintf(out, "; frame_commit expects 0 args, got %zu\n", expr->as.call.arg_count);
    return 0;
  }
  if (!ctx->current_worker) {
    emit_indent(out, depth);
    fputs("; frame_commit only valid in workers\n", out);
    return 0;
  }
  emit_mailbox_write_i32(out, 255, depth);
  emit_indent(out, depth);
  fputs("HOST_SIGNAL\n", out);
  return 1;
}

static int emit_far_signal_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  const ELocalBinding *binding;
  const EValidatorBinding *validator;
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 2u) {
    emit_indent(out, depth);
    fprintf(out, "; far_signal expects 2 args, got %zu\n", expr->as.call.arg_count);
    return 0;
  }
  if (!ctx->current_worker) {
    emit_indent(out, depth);
    fputs("; far_signal only valid in workers\n", out);
    return 0;
  }
  if (!emit_target_string_ref_to_regs(out, expr->as.call.args[0], ctx, depth)) {
    emit_indent(out, depth);
    fputs("; far_signal target_id must be a string literal or a local/string ref variable\n", out);
    return 0;
  }
  if (expr->as.call.args[1]->kind != E_EXPR_IDENT) {
    emit_indent(out, depth);
    fputs("; far_signal payload must be a local area variable identifier\n", out);
    return 0;
  }
  binding = find_local_binding_by_name(active_frame_for_ctx(ctx), expr->as.call.args[1]->as.ident);
  if (!binding || (binding->storage != E_LOCAL_ARENA_SCOPED && binding->storage != E_LOCAL_STACK_STATIC) || binding->vm_local_words != 2u) {
    emit_indent(out, depth);
    fputs("; far_signal payload must be a local or static declared type variable\n", out);
    return 0;
  }
  emit_indent(out, depth);
  EMIT_LOAD_L(out, ctx, binding->vm_local_slot);
  emit_indent(out, depth);
  fputs("POP R3\n", out);
  validator = find_validator_binding(ctx->model, binding->type_name);
  emit_indent(out, depth);
  fprintf(out, "PUSH %u\n", validator ? validator->validator_id : 0u);
  emit_indent(out, depth);
  fprintf(out, "PUSH %zu\n", binding->byte_size);
  emit_indent(out, depth);
  fputs("FAR_SIGNAL\n", out);
  return 1;
}

static int emit_request_threads_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 1u) {
    emit_indent(out, depth);
    fprintf(out, "; request_threads expects 1 arg, got %zu\n", expr->as.call.arg_count);
    return 0;
  }
  if (ctx->current_worker || ctx->current_function) {
    emit_indent(out, depth);
    fputs("; request_threads only valid in kernel\n", out);
    return 0;
  }
  emit_expr(out, expr->as.call.args[0], ctx, depth);
  emit_indent(out, depth);
  fputs("POP R0\n", out);
  emit_indent(out, depth);
  fputs("REQUEST_THREADS\n", out);
  return 1;
}

static int emit_target_string_ref_to_regs(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  const ELocalBinding *binding;
  unsigned int string_id;
  if (!expr) return 0;
  if (expr->kind == E_EXPR_STRING) {
    string_id = find_string_const_id(ctx, expr->as.string_lit);
    emit_indent(out, depth);
    fprintf(out, "LOAD_CONST %u\n", string_id);
    return 1;
  }
  if (expr->kind == E_EXPR_IDENT) {
    binding = find_local_binding_by_name(active_frame_for_ctx(ctx), expr->as.ident);
    if (binding && binding->vm_local_words == 2u) {
      emit_indent(out, depth);
      EMIT_LOAD_L(out, ctx, binding->vm_local_slot);
      emit_indent(out, depth);
      fputs("POP R0\n", out);
      emit_indent(out, depth);
      EMIT_LOAD_L(out, ctx, binding->vm_local_slot + 1u);
      emit_indent(out, depth);
      fputs("POP R1\n", out);
      emit_indent(out, depth);
      fprintf(out, "SET_R 2 %u\n", EPA_CONST_TMP_STR);
      return 1;
    }
  }
  return 0;
}

static int emit_kernel_get_ghs_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  const EFunctionFrame *frame = active_frame_for_ctx(ctx);
  unsigned int wid_slot = 0u;
  unsigned int temp_slot = 0u;
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 1u) {
    emit_indent(out, depth);
    fprintf(out, "; %s expects 1 arg, got %zu\n", expr->as.call.callee, expr->as.call.arg_count);
    return 0;
  }
  if (ctx->current_worker || ctx->current_function) {
    emit_indent(out, depth);
    fprintf(out, "; %s only valid in kernel\n", expr->as.call.callee);
    return 0;
  }
  if (!frame) {
    emit_indent(out, depth);
    fprintf(out, "; %s missing kernel frame\n", expr->as.call.callee);
    return 0;
  }
  if (expr->as.call.args[0]->kind == E_EXPR_IDENT &&
      scalar_vm_local_slot_for_name(ctx, expr->as.call.args[0]->as.ident, &wid_slot)) {
    emit_indent(out, depth);
    fprintf(out, "KERNEL_GHS_IN_R %u\n", wid_slot);
  } else {
    temp_slot = frame->vm_local_count;
    emit_expr(out, expr->as.call.args[0], ctx, depth);
    emit_indent(out, depth);
    fputs("POP R0\n", out);
    emit_indent(out, depth);
    EMIT_STORE_L(out, ctx, temp_slot);
    emit_indent(out, depth);
    fprintf(out, "KERNEL_GHS_IN_R %u\n", temp_slot);
  }
  emit_indent(out, depth);
  fputs("PUSH R0\n", out);
  emit_indent(out, depth);
  fputs("PUSH R1\n", out);
  return 1;
}

static int emit_typeof_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  const EFunctionFrame *frame = active_frame_for_ctx(ctx);
  const ELocalBinding *local;
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 1u) {
    emit_indent(out, depth);
    fprintf(out, "; typeof expects 1 arg, got %zu\n", expr->as.call.arg_count);
    return 0;
  }
  if (expr->as.call.args[0]->kind == E_EXPR_IDENT) {
    const char *name = expr->as.call.args[0]->as.ident;
    if (ctx->current_worker && ctx->current_worker->param_count == 1u &&
        strcmp(ctx->current_worker->params[0].name, name) == 0) {
      emit_indent(out, depth);
      fputs("G_TAG\n", out);
      emit_indent(out, depth);
      fputs("PUSH R0\n", out);
      return 1;
    }
    local = find_local_binding_by_name(frame, name);
    if (local && local->vm_local_words == 2u) {
      emit_indent(out, depth);
      EMIT_LOAD_L(out, ctx, local->vm_local_slot);
      emit_indent(out, depth);
      fputs("POP R0\n", out);
      emit_indent(out, depth);
      EMIT_LOAD_L(out, ctx, local->vm_local_slot + 1u);
      emit_indent(out, depth);
      fputs("POP R1\n", out);
      emit_indent(out, depth);
      fputs("G_TAG\n", out);
      emit_indent(out, depth);
      fputs("PUSH R0\n", out);
      return 1;
    }
  }
  emit_indent(out, depth);
  fputs("; typeof unsupported for this expression\n", out);
  return 0;
}

static int emit_typeid_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  const EValidatorBinding *binding;
  if (!expr || expr->kind != E_EXPR_CALL || strcmp(expr->as.call.callee, "typeid") != 0) return 0;
  if (expr->as.call.arg_count != 1u || expr->as.call.args[0]->kind != E_EXPR_IDENT) {
    emit_indent(out, depth);
    fputs("; typeid expects a declared type name\n", out);
    return 0;
  }
  binding = find_validator_binding(ctx->model, expr->as.call.args[0]->as.ident);
  if (!binding) {
    emit_indent(out, depth);
    fprintf(out, "; unknown typeid target %s\n", expr->as.call.args[0]->as.ident);
    return 0;
  }
  emit_indent(out, depth);
  fprintf(out, "PUSH %u\n", binding->validator_id);
  return 1;
}

static int emit_dyn_alloc_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  const EDynamicPool *pool;
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 1u || expr->as.call.args[0]->kind != E_EXPR_IDENT) {
    emit_indent(out, depth);
    fputs("; dyn_alloc expects 1 dynamic pool identifier\n", out);
    return 0;
  }
  pool = find_dynamic_pool(ctx->model, expr->as.call.args[0]->as.ident);
  if (!pool) {
    emit_indent(out, depth);
    fputs("; dyn_alloc target is not a dynamic pool\n", out);
    return 0;
  }
  emit_indent(out, depth);
  fprintf(out, "DYN_ALLOC %u\n", dynamic_pool_id(ctx, pool));
  emit_indent(out, depth);
  fputs("PUSH R0\n", out);
  return 1;
}

static int emit_dyn_free_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  const EDynamicPool *pool;
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 2u || expr->as.call.args[0]->kind != E_EXPR_IDENT) {
    emit_indent(out, depth);
    fputs("; dyn_free expects (dynamic_pool, id)\n", out);
    return 0;
  }
  pool = find_dynamic_pool(ctx->model, expr->as.call.args[0]->as.ident);
  if (!pool) {
    emit_indent(out, depth);
    fputs("; dyn_free target is not a dynamic pool\n", out);
    return 0;
  }
  emit_expr(out, expr->as.call.args[1], ctx, depth);
  emit_indent(out, depth);
  fputs("POP R0\n", out);
  emit_indent(out, depth);
  fprintf(out, "DYN_FREE %u\n", dynamic_pool_id(ctx, pool));
  return 1;
}

static int emit_dyn_swap_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  const EDynamicPool *pool;
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 3u || expr->as.call.args[0]->kind != E_EXPR_IDENT) {
    emit_indent(out, depth);
    fputs("; dyn_swap expects (dynamic_pool, id_a, id_b)\n", out);
    return 0;
  }
  pool = find_dynamic_pool(ctx->model, expr->as.call.args[0]->as.ident);
  if (!pool) {
    emit_indent(out, depth);
    fputs("; dyn_swap target is not a dynamic pool\n", out);
    return 0;
  }
  emit_expr(out, expr->as.call.args[1], ctx, depth);
  emit_indent(out, depth);
  fputs("POP R0\n", out);
  emit_expr(out, expr->as.call.args[2], ctx, depth);
  emit_indent(out, depth);
  fputs("POP R1\n", out);
  emit_indent(out, depth);
  fprintf(out, "DYN_SWAP %u\n", dynamic_pool_id(ctx, pool));
  return 1;
}

static int emit_dyn_iterator_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  const EDynamicPool *pool;
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  if (expr->as.call.arg_count != 1u || expr->as.call.args[0]->kind != E_EXPR_IDENT) {
    emit_indent(out, depth);
    fputs("; dynamic_iterator expects 1 dynamic pool identifier\n", out);
    return 0;
  }
  pool = find_dynamic_pool(ctx->model, expr->as.call.args[0]->as.ident);
  if (!pool) {
    emit_indent(out, depth);
    fputs("; dynamic_iterator target is not a dynamic pool\n", out);
    return 0;
  }
  emit_indent(out, depth);
  fprintf(out, "DYN_ITER_HEAD %u\n", dynamic_pool_id(ctx, pool));
  emit_indent(out, depth);
  fputs("PUSH R0\n", out);
  return 1;
}

static int emit_dyn_next_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  /* dynamic_next() as a standalone expression is a no-op placeholder.
     Real iteration is handled by E_STMT_FOREACH in emit_stmt. */
  (void)out; (void)expr; (void)ctx; (void)depth;
  return 0;
}

static unsigned int find_iter_pool_id(const EmitCtx *ctx, const char *iter_name, int *found) {
  unsigned int i;
  for (i = 0; i < ctx->iter_binding_count; i++) {
    if (strcmp(ctx->iter_bindings[i].iter_name, iter_name) == 0) {
      *found = 1;
      return ctx->iter_bindings[i].pool_id;
    }
  }
  *found = 0;
  return 0u;
}

static unsigned int next_label(EmitCtx *ctx) {
  return ctx->next_label_id++;
}

static unsigned int worker_entry_id_for_name(const ESemanticModel *model, const char *name) {
  size_t i;
  for (i = 0; i < model->worker_count; i++) {
    if (strcmp(model->workers[i]->name, name) == 0) {
      return (unsigned int)(i + 1u);
    }
  }
  return 0u;
}

static void emit_verbatim_epa(FILE *out, const char *text, int depth) {
  const char *cursor;
  if (!text || !text[0]) return;
  cursor = text;
  while (*cursor) {
    const char *line_end = strchr(cursor, '\n');
    emit_indent(out, depth);
    if (line_end) {
      fwrite(cursor, 1, (size_t)(line_end - cursor), out);
      fputc('\n', out);
      cursor = line_end + 1;
    } else {
      fputs(cursor, out);
      fputc('\n', out);
      break;
    }
  }
}

static void emit_stmt(FILE *out, const EStmt *stmt, EmitCtx *ctx, int depth, const char *break_label, const char *continue_label) {
  size_t i;
  if (!stmt) return;
  if (stmt->line > 0) {
    int emit_line = stmt->line;
    if (ctx->line_map && (size_t)stmt->line <= ctx->line_map->count) {
      int idx = stmt->line - 1;
      unsigned int fidx = ctx->line_map->file_indices[idx];
      const char *fname = ctx->line_map->filenames[fidx];
      int is_main = 0;
      if (ctx->main_file) {
        /* Compare full paths; fall back to basename comparison */
        const char *a = strrchr(fname,          '/'); a = a ? a + 1 : fname;
        const char *b = strrchr(ctx->main_file, '/'); b = b ? b + 1 : ctx->main_file;
        is_main = (strcmp(fname, ctx->main_file) == 0) || (strcmp(a, b) == 0);
      }
      emit_line = is_main ? ctx->line_map->line_nums[idx] : 0;
    }
    if (emit_line > 0) {
      emit_indent(out, depth);
      fprintf(out, "; !LINE %d\n", emit_line);
    }
  }
  switch (stmt->kind) {
    case E_STMT_DECL:
    {
      const ELocalBinding *binding = find_local_binding(active_frame_for_ctx(ctx), stmt);
      emit_indent(out, depth);
      fputs("; decl ", out);
      if (stmt->as.decl.is_reg) fputs("reg ", out);
      if (stmt->as.decl.is_local) fputs("local ", out);
      if (stmt->as.decl.is_static) fputs("static ", out);
      fprintf(out, "%s", stmt->as.decl.type.name);
      if (stmt->as.decl.type.array_len != 0u) fprintf(out, "[%u]", stmt->as.decl.type.array_len);
      fprintf(out, " %s", stmt->as.decl.name);
      if (binding) {
        if (binding->storage == E_LOCAL_STACK) {
          fprintf(out, " stack+%zu size=%zu", binding->stack_offset, binding->byte_size);
        } else if (binding->storage == E_LOCAL_STACK_STATIC) {
          fprintf(out, " static vm_local=%u size=%zu (zero-init before loop)", binding->vm_local_slot, binding->byte_size);
        } else if (binding->storage == E_LOCAL_ARENA_SCOPED) {
          fprintf(out, " local-scope-arena size=%zu", binding->byte_size);
        } else {
          fprintf(out, " r%u", binding->reg_index);
          if (binding->reg_words == 2u) fprintf(out, ":r%u", binding->reg_index + 1u);
          fprintf(out, " size=%zu", binding->byte_size);
        }
      }
      fputc('\n', out);
      /* Static locals: init was emitted once before the worker loop; nothing to do here. */
      if (binding && binding->storage == E_LOCAL_STACK_STATIC) break;
      if (stmt->as.decl.init) {
        emit_expr(out, stmt->as.decl.init, ctx, depth);
        /* Register iterator binding if init is dynamic_iterator(pool_name) */
        if (stmt->as.decl.init->kind == E_EXPR_CALL &&
            strcmp(stmt->as.decl.init->as.call.callee, "dynamic_iterator") == 0 &&
            stmt->as.decl.init->as.call.arg_count == 1u &&
            stmt->as.decl.init->as.call.args[0]->kind == E_EXPR_IDENT &&
            ctx->iter_binding_count < 16u) {
          const EDynamicPool *iter_pool = find_dynamic_pool(ctx->model, stmt->as.decl.init->as.call.args[0]->as.ident);
          if (iter_pool) {
            ctx->iter_bindings[ctx->iter_binding_count].iter_name = stmt->as.decl.name;
            ctx->iter_bindings[ctx->iter_binding_count].pool_id = dynamic_pool_id(ctx, iter_pool);
            ctx->iter_binding_count++;
          }
        }
        if (binding && binding->vm_local_words == 1u) {
          emit_indent(out, depth);
          EMIT_STORE_L(out, ctx, binding->vm_local_slot);
        } else if (binding && binding->vm_local_words == 2u) {
          emit_indent(out, depth);
          fprintf(out, "POP R1\n");
          emit_indent(out, depth);
          EMIT_STORE_L(out, ctx, binding->vm_local_slot + 1u);
          emit_indent(out, depth);
          fprintf(out, "POP R0\n");
          emit_indent(out, depth);
          EMIT_STORE_L(out, ctx, binding->vm_local_slot);
        }
      } else if (binding && binding->storage == E_LOCAL_ARENA_SCOPED && binding->vm_local_words == 2u) {
        emit_indent(out, depth);
        fprintf(out, "SET_R 0 %zu\n", binding->byte_size);
        emit_indent(out, depth);
        fputs("L_ALLOC\n", out);
        emit_indent(out, depth);
        fputs("PUSH R0\n", out);
        emit_indent(out, depth);
        EMIT_STORE_L(out, ctx, binding->vm_local_slot);
        emit_indent(out, depth);
        fputs("PUSH R1\n", out);
        emit_indent(out, depth);
        EMIT_STORE_L(out, ctx, binding->vm_local_slot + 1u);
      }
      break;
    }
    case E_STMT_RETURN:
      if (stmt->as.ret.value) emit_expr(out, stmt->as.ret.value, ctx, depth);
      emit_indent(out, depth);
      fputs("RET\n", out);
      break;
    case E_STMT_EXPR:
      emit_expr(out, stmt->as.expr, ctx, depth);
      /* Assignment expressions leave the result on the stack for expression
         chaining. As a statement the result is unused — discard it.
         User-defined function calls always leave a return value on the stack via RET. */
      if (stmt->as.expr && stmt->as.expr->kind == E_EXPR_ASSIGN) {
        if (stmt->as.expr->as.assign.lhs &&
            stmt->as.expr->as.assign.lhs->kind == E_EXPR_IDENT) {
          unsigned int slot = 0u;
          if (scalar_vm_local_slot_for_name(ctx, stmt->as.expr->as.assign.lhs->as.ident, &slot)) {
            emit_indent(out, depth);
            fputs("POP 0\n", out);
          } else if (type_ref_vm_local_slot_for_name(ctx, stmt->as.expr->as.assign.lhs->as.ident, &slot)) {
            emit_indent(out, depth);
            fputs("POP 0\n", out);
            emit_indent(out, depth);
            fputs("POP 0\n", out);
          }
        } else if (expr_is_dynamic_index(stmt->as.expr->as.assign.lhs, ctx->model, NULL)) {
          emit_indent(out, depth);
          fputs("POP 0\n", out);
          emit_indent(out, depth);
          fputs("POP 0\n", out);
        }
      } else if (stmt->as.expr && stmt->as.expr->kind == E_EXPR_CALL) {
        unsigned int dummy_id = 0u;
        if (find_user_func_id(ctx, stmt->as.expr->as.call.callee, &dummy_id) &&
            user_func_has_return_value(ctx, stmt->as.expr->as.call.callee)) {
          emit_indent(out, depth);
          fputs("POP 0\n", out);
        }
      }
      break;
    case E_STMT_BLOCK:
      for (i = 0; i < stmt->as.block.count; i++) {
        emit_stmt(out, stmt->as.block.items[i], ctx, depth, break_label, continue_label);
      }
      break;
    case E_STMT_IF: {
      unsigned int else_id = next_label(ctx);
      unsigned int join_id = next_label(ctx);
      emit_indent(out, depth);
      fputs("; if condition\n", out);
      emit_expr(out, stmt->as.if_stmt.cond, ctx, depth);
      emit_indent(out, depth);
      fputs("POP 0\n", out);
      emit_indent(out, depth);
      fprintf(out, "JZ E_IF_ELSE_%u\n", else_id);
      emit_stmt(out, stmt->as.if_stmt.then_branch, ctx, depth, break_label, continue_label);
      emit_indent(out, depth);
      fprintf(out, "JMP E_IF_JOIN_%u\n", join_id);
      emit_indent(out, depth);
      fprintf(out, "E_IF_ELSE_%u:\n", else_id);
      if (stmt->as.if_stmt.else_branch) {
        emit_stmt(out, stmt->as.if_stmt.else_branch, ctx, depth, break_label, continue_label);
      } else {
        emit_indent(out, depth);
        fputs("NOOP\n", out);
      }
      emit_indent(out, depth);
      fprintf(out, "E_IF_JOIN_%u:\n", join_id);
      break;
    }
    case E_STMT_WHILE: {
      unsigned int head_id = next_label(ctx);
      unsigned int exit_id = next_label(ctx);
      char head_label[64];
      char exit_label[64];
      snprintf(head_label, sizeof(head_label), "E_WHILE_HEAD_%u", head_id);
      snprintf(exit_label, sizeof(exit_label), "E_WHILE_EXIT_%u", exit_id);
      emit_indent(out, depth);
      fprintf(out, "%s:\n", head_label);
      emit_indent(out, depth);
      fputs("; while condition\n", out);
      emit_expr(out, stmt->as.while_stmt.cond, ctx, depth);
      emit_indent(out, depth);
      fputs("POP 0\n", out);
      emit_indent(out, depth);
      fprintf(out, "JZ %s\n", exit_label);
      emit_stmt(out, stmt->as.while_stmt.body, ctx, depth, exit_label, head_label);
      emit_indent(out, depth);
      fprintf(out, "JMP %s\n", head_label);
      emit_indent(out, depth);
      fprintf(out, "%s:\n", exit_label);
      break;
    }
    case E_STMT_FOR: {
      unsigned int head_id = next_label(ctx);
      unsigned int step_id = next_label(ctx);
      unsigned int exit_id = next_label(ctx);
      char head_label[64];
      char step_label[64];
      char exit_label[64];
      snprintf(head_label, sizeof(head_label), "E_FOR_HEAD_%u", head_id);
      snprintf(step_label, sizeof(step_label), "E_FOR_STEP_%u", step_id);
      snprintf(exit_label, sizeof(exit_label), "E_FOR_EXIT_%u", exit_id);
      if (stmt->as.for_stmt.init) {
        emit_indent(out, depth);
        fputs("; for init\n", out);
        emit_stmt(out, stmt->as.for_stmt.init, ctx, depth, break_label, continue_label);
      }
      emit_indent(out, depth);
      fprintf(out, "%s:\n", head_label);
      if (stmt->as.for_stmt.cond) {
        emit_indent(out, depth);
        fputs("; for condition\n", out);
        emit_expr(out, stmt->as.for_stmt.cond, ctx, depth);
        emit_indent(out, depth);
        fputs("POP 0\n", out);
        emit_indent(out, depth);
        fprintf(out, "JZ %s\n", exit_label);
      }
      emit_stmt(out, stmt->as.for_stmt.body, ctx, depth, exit_label, step_label);
      emit_indent(out, depth);
      fprintf(out, "%s:\n", step_label);
      if (stmt->as.for_stmt.step) {
        emit_indent(out, depth);
        fputs("; for step\n", out);
        emit_expr(out, stmt->as.for_stmt.step, ctx, depth);
      }
      emit_indent(out, depth);
      fprintf(out, "JMP %s\n", head_label);
      emit_indent(out, depth);
      fprintf(out, "%s:\n", exit_label);
      break;
    }
    case E_STMT_FOREACH: {
      /* Emit: while(MyType t = dynamic_next(_iterator)) { body }
         Generates DYN_ITER_NEXT with L_SCOPE_ENTER/LEAVE per iteration. */
      const char *iter_name = stmt->as.foreach_stmt.iter_name;
      const ELocalBinding *iter_binding = find_local_binding_by_name(active_frame_for_ctx(ctx), iter_name);
      const ELocalBinding *var_binding  = find_local_binding(active_frame_for_ctx(ctx), stmt->as.foreach_stmt.var_decl);
      unsigned int head_id    = next_label(ctx);
      unsigned int cleanup_id = next_label(ctx);
      unsigned int cont_id    = next_label(ctx);
      unsigned int exit_id    = next_label(ctx);
      char head_label[64];
      char cleanup_label[64];
      char cont_label[64];
      char exit_label[64];
      int pool_found = 0;
      unsigned int pool_id = find_iter_pool_id(ctx, iter_name, &pool_found);
      snprintf(head_label,    sizeof(head_label),    "E_FOREACH_HEAD_%u",    head_id);
      snprintf(cleanup_label, sizeof(cleanup_label), "E_FOREACH_CLEANUP_%u", cleanup_id);
      snprintf(cont_label,    sizeof(cont_label),    "E_FOREACH_CONT_%u",    cont_id);
      snprintf(exit_label,    sizeof(exit_label),    "E_FOREACH_EXIT_%u",    exit_id);
      if (!pool_found) {
        emit_indent(out, depth);
        fprintf(out, "; foreach: iterator '%s' has no registered pool binding\n", iter_name);
        break;
      }
      if (!iter_binding) {
        emit_indent(out, depth);
        fprintf(out, "; foreach: iterator '%s' not found in frame\n", iter_name);
        break;
      }
      emit_indent(out, depth);
      fprintf(out, "; foreach %s t over pool %u via iterator '%s'\n",
              stmt->as.foreach_stmt.var_decl->as.decl.type.name, pool_id, iter_name);
      emit_indent(out, depth);
      fprintf(out, "%s:\n", head_label);
      /* Save lbytes scope first; its side-effect on csc[0] is overwritten below */
      emit_indent(out, depth);
      fputs("L_SCOPE_ENTER\n", out);
      /* Load current iterator ordinal into csc[0] for DYN_ITER_NEXT */
      emit_indent(out, depth);
      EMIT_LOAD_L(out, ctx, iter_binding->vm_local_slot);
      emit_indent(out, depth);
      fputs("POP R0\n", out);
      /* Advance: r0=next_id, r1=ok, r2=off, r3=size */
      emit_indent(out, depth);
      fprintf(out, "DYN_ITER_NEXT %u\n", pool_id);
      /* Save next iterator id */
      emit_indent(out, depth);
      fputs("PUSH R0\n", out);
      emit_indent(out, depth);
      EMIT_STORE_L(out, ctx, iter_binding->vm_local_slot);
      /* Check ok flag */
      emit_indent(out, depth);
      fputs("PUSH R1\n", out);
      emit_indent(out, depth);
      fputs("POP 0\n", out);
      emit_indent(out, depth);
      fprintf(out, "JZ %s\n", cleanup_label);
      /* Store element ref into var (slot=off, slot+1=size) */
      if (var_binding && var_binding->vm_local_words >= 2u) {
        emit_indent(out, depth);
        fputs("PUSH R2\n", out);
        emit_indent(out, depth);
        EMIT_STORE_L(out, ctx, var_binding->vm_local_slot);
        emit_indent(out, depth);
        fputs("PUSH R3\n", out);
        emit_indent(out, depth);
        EMIT_STORE_L(out, ctx, var_binding->vm_local_slot + 1u);
      }
      /* Body: break → cleanup, continue → cont (scope leave + jmp head) */
      emit_stmt(out, stmt->as.foreach_stmt.body, ctx, depth, cleanup_label, cont_label);
      /* Continue label: release scope, jump to head */
      emit_indent(out, depth);
      fprintf(out, "%s:\n", cont_label);
      emit_indent(out, depth);
      fputs("L_SCOPE_LEAVE\n", out);
      emit_indent(out, depth);
      fprintf(out, "JMP %s\n", head_label);
      /* Cleanup label: release scope on loop exit */
      emit_indent(out, depth);
      fprintf(out, "%s:\n", cleanup_label);
      emit_indent(out, depth);
      fputs("L_SCOPE_LEAVE\n", out);
      emit_indent(out, depth);
      fprintf(out, "%s:\n", exit_label);
      break;
    }
    case E_STMT_SWITCH: {
      unsigned int join_id = next_label(ctx);
      char join_label[64];
      snprintf(join_label, sizeof(join_label), "E_SWITCH_JOIN_%u", join_id);
      {
        /* Determine whether the target is a scalar we can compare directly */
        int target_is_scalar = 0;
        {
          unsigned int dummy = 0u;
          const EExpr *tgt = stmt->as.switch_stmt.target;
          if (tgt && tgt->kind == E_EXPR_INT) target_is_scalar = 1;
          else if (tgt && tgt->kind == E_EXPR_IDENT &&
                   scalar_vm_local_slot_for_name(ctx, tgt->as.ident, &dummy)) target_is_scalar = 1;
        }
        if (target_is_scalar && stmt->as.switch_stmt.case_count > 0u) {
          /* Phase 1: pre-allocate a body label ID for each case (max 32) */
          unsigned int case_body_ids[32];
          size_t n_cases = stmt->as.switch_stmt.case_count;
          int default_idx = -1;
          size_t ci;
          if (n_cases > 32u) n_cases = 32u;
          for (ci = 0; ci < n_cases; ci++) {
            case_body_ids[ci] = next_label(ctx);
            if (stmt->as.switch_stmt.cases[ci].is_default) default_idx = (int)ci;
          }
          /* Phase 2: emit comparison chain for non-default cases */
          for (ci = 0; ci < n_cases; ci++) {
            if (stmt->as.switch_stmt.cases[ci].is_default) continue;
            emit_expr(out, stmt->as.switch_stmt.target, ctx, depth);
            emit_indent(out, depth);
            fprintf(out, "PUSH %lld\n", stmt->as.switch_stmt.cases[ci].value);
            emit_indent(out, depth);
            fputs("EQ_I32\n", out);
            emit_indent(out, depth);
            fputs("POP 0\n", out);
            emit_indent(out, depth);
            fprintf(out, "JNZ E_SWITCH_CASE_%u\n", case_body_ids[ci]);
          }
          /* Fall through to default or join if no case matched */
          emit_indent(out, depth);
          if (default_idx >= 0) {
            fprintf(out, "JMP E_SWITCH_CASE_%u\n", case_body_ids[default_idx]);
          } else {
            fprintf(out, "JMP %s\n", join_label);
          }
          /* Phase 3: emit case bodies */
          for (ci = 0; ci < n_cases; ci++) {
            size_t j;
            emit_indent(out, depth);
            fprintf(out, "E_SWITCH_CASE_%u:\n", case_body_ids[ci]);
            for (j = 0; j < stmt->as.switch_stmt.cases[ci].body.count; j++) {
              emit_stmt(out, stmt->as.switch_stmt.cases[ci].body.items[j], ctx, depth, join_label, continue_label);
            }
            if (ci < n_cases - 1u) {
              emit_indent(out, depth);
              fprintf(out, "JMP %s\n", join_label);
            }
          }
        } else {
          /* Non-scalar target (type-ref dispatch, etc.): emit descriptive comment */
          emit_indent(out, depth);
          fputs("; switch on non-scalar target — dispatch pending\n", out);
          emit_expr(out, stmt->as.switch_stmt.target, ctx, depth);
          for (i = 0; i < stmt->as.switch_stmt.case_count; i++) {
            unsigned int case_id = next_label(ctx);
            size_t j;
            emit_indent(out, depth);
            if (stmt->as.switch_stmt.cases[i].is_default) {
              fprintf(out, "; default:\n");
            } else {
              fprintf(out, "; case %lld:\n", stmt->as.switch_stmt.cases[i].value);
            }
            emit_indent(out, depth);
            fprintf(out, "E_SWITCH_CASE_%u:\n", case_id);
            for (j = 0; j < stmt->as.switch_stmt.cases[i].body.count; j++) {
              emit_stmt(out, stmt->as.switch_stmt.cases[i].body.items[j], ctx, depth, join_label, continue_label);
            }
          }
        }
      }
      emit_indent(out, depth);
      fprintf(out, "%s:\n", join_label);
      break;
    }
    case E_STMT_BREAK:
      emit_indent(out, depth);
      if (break_label) {
        fprintf(out, "JMP %s\n", break_label);
      } else {
        fputs("; break outside switch pending semantic error\n", out);
      }
      break;
    case E_STMT_CONTINUE:
      emit_indent(out, depth);
      if (continue_label) {
        fprintf(out, "JMP %s\n", continue_label);
      } else {
        fputs("; continue outside loop pending semantic error\n", out);
      }
      break;
    case E_STMT_NEXT: {
      unsigned int next_worker_id = worker_entry_id_for_name(ctx->model, stmt->as.next_stmt.worker_name);
      emit_indent(out, depth);
      fprintf(out, "; next %s\n", stmt->as.next_stmt.worker_name);
      emit_indent(out, depth);
      fputs("PUSH R0\n", out);
      emit_indent(out, depth);
      fputs("PUSH R1\n", out);
      emit_indent(out, depth);
      fprintf(out, "SET_R 2 %u\n", next_worker_id);
      emit_indent(out, depth);
      fputs("G_XFER\n", out);
      break;
    }
    case E_STMT_RAW_EPA:
      emit_verbatim_epa(out, stmt->as.raw_epa.text, depth);
      break;
    case E_STMT_DYNAMIC:
      break;
    case E_STMT_STATIC_BLOCK:
      emit_indent(out, depth);
      fputs("; static { } block — emitted before worker loop\n", out);
      break;
  }
}

static void emit_static_blocks_in_stmt(FILE *out, const EStmt *stmt, EmitCtx *ctx, int depth) {
  size_t i;
  if (!stmt) return;
  if (stmt->kind == E_STMT_STATIC_BLOCK) {
    emit_indent(out, depth);
    fputs("; static { }\n", out);
    for (i = 0; i < stmt->as.static_block.count; i++)
      emit_stmt(out, stmt->as.static_block.items[i], ctx, depth, NULL, NULL);
    return;
  }
  if (stmt->kind == E_STMT_BLOCK) {
    for (i = 0; i < stmt->as.block.count; i++)
      emit_static_blocks_in_stmt(out, stmt->as.block.items[i], ctx, depth);
  }
}

static int pool_is_in_worker_body(const char *pool_name, const EStmt *stmt) {
  size_t i;
  if (!stmt) return 0;
  if (stmt->kind == E_STMT_DYNAMIC && strcmp(stmt->as.dynamic_decl.name, pool_name) == 0) return 1;
  if (stmt->kind == E_STMT_BLOCK) {
    for (i = 0; i < stmt->as.block.count; i++) {
      if (pool_is_in_worker_body(pool_name, stmt->as.block.items[i])) return 1;
    }
  }
  return 0;
}

static int frame_words_for_function(const ESemanticModel *model, const char *name) {
  const EFunctionFrame *frame = find_frame(model, name);
  if (!frame) return 0;
  return (int)((frame->total_size + 3u) / 4u);
}

static unsigned int resolved_in_words(const ESemanticModel *model, const EEntryAttributes *attrs) {
  if (attrs && attrs->has_in_words) return attrs->in_words;
  return model->default_in_words;
}

static unsigned int resolved_out_words(const ESemanticModel *model, const EEntryAttributes *attrs) {
  if (attrs && attrs->has_out_words) return attrs->out_words;
  return model->default_out_words;
}

static unsigned int resolved_signal_mail_box_size(const ESemanticModel *model, const EEntryAttributes *attrs) {
  if (attrs && attrs->has_signal_mail_box_size) return attrs->signal_mail_box_size;
  return model->default_signal_mail_box_size;
}

/* Emit block-offset map for the IDE debugger.
 * Format: "B <block_type> <block_id>\n" for block headers,
 *         "<byte_offset> <epa_line> <source_line> <epa_column>\n" for each byte position
 *         in an instruction line inside the current block body.
 * byte_offset is the EIP-relative offset within the current entry/function body.
 * epa_line is the 1-based line in the emitted .epaasm file.
 * source_line is the 1-based E source line, or 0 when the instruction has no direct source line.
 * epa_column is the 1-based visual column to place the EPA marker for this byte position.
 * block_type 0 = entry, 1 = func. block_id for funcs is the dense index (0, 1, …). */
static void e_write_epa_map(FILE *asm_out, FILE *map_out) {
  char buf[1024];
  int current_e_line = 0;
  int in_body = 0;
  int body_offset = 0;
  int func_dense_idx = 0;
  int epa_line = 0;

  rewind(asm_out);
  while (fgets(buf, sizeof(buf), asm_out)) {
    epa_line++;
    const char *p = buf;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '\r') continue;
    if (strncmp(p, "; !LINE ", 8) == 0) {
      current_e_line = atoi(p + 8);
      continue;
    }
    if (*p == ';' || *p == '.') continue;

    /* Extract first token */
    char token[64];
    size_t tlen = 0;
    const char *tp = p;
    while (*tp && !isspace((unsigned char)*tp) && *tp != ':' && tlen < 63u)
      token[tlen++] = *tp++;
    token[tlen] = '\0';
    if (tlen == 0u) continue;
    if (*tp == ':') continue;  /* label-only line */

    /* Block boundary markers */
    if (strcmp(token, "ENTRY_START") == 0) {
      const char *arg = p + tlen;
      while (*arg == ' ' || *arg == '\t') arg++;
      fprintf(map_out, "B 0 %d\n", atoi(arg));
      in_body = 1;
      body_offset = 0;
      current_e_line = 0;
      continue;
    }
    if (strcmp(token, "ENTRY_END") == 0) { in_body = 0; continue; }
    if (strcmp(token, "FUNC_START") == 0) {
      fprintf(map_out, "B 1 %d\n", func_dense_idx++);
      in_body = 1;
      body_offset = 0;
      current_e_line = 0;
      continue;
    }
    if (strcmp(token, "FUNC_END") == 0) { in_body = 0; continue; }
    if (!in_body) continue;

    /* Extract optional first argument (needed for PUSH ambiguity) */
    char first_arg[64];
    first_arg[0] = '\0';
    {
      const char *ap = tp;
      while (*ap == ' ' || *ap == '\t') ap++;
      size_t alen = 0;
      while (*ap && !isspace((unsigned char)*ap) && alen < 63u)
        first_arg[alen++] = *ap++;
      first_arg[alen] = '\0';
    }

    /* Look up instruction size using kDesc (assembler-text mnemonics) */
    int instr_bytes = epa_asm_instr_total_bytes(token, first_arg[0] ? first_arg : NULL);
    {
      int line_len = (int)strlen(buf);
      int instr_col = (int)(p - buf) + 1;
      int line_end_col = instr_col;
      int display_span = 1;
      int i;
      while (line_len > 0 && (buf[line_len - 1] == '\n' || buf[line_len - 1] == '\r')) line_len--;
      line_end_col = line_len > 0 ? line_len : instr_col;
      if (line_end_col < instr_col) line_end_col = instr_col;
      display_span = (line_end_col - instr_col) + 1;
      if (display_span < 1) display_span = 1;

      if (instr_bytes == -2) {
        /* Variable-length: emit once and stop tracking offsets for this block */
        fprintf(map_out, "%d %d %d %d\n", body_offset, epa_line, current_e_line, instr_col);
        in_body = 0;
        continue;
      }
      if (instr_bytes < 0) continue; /* unknown mnemonic — skip, don't advance offset */
      for (i = 0; i < instr_bytes; i++) {
        int col = instr_col;
        if (instr_bytes > 1 && display_span > 1) {
          col = instr_col + (i * (display_span - 1)) / (instr_bytes - 1);
        }
        fprintf(map_out, "%d %d %d %d\n", body_offset + i, epa_line, current_e_line, col);
      }
      body_offset += instr_bytes;
    }
  }
}

int e_emit_epa_asm(FILE *out, FILE *map_out,
                   const EProgram *prog, const ESemanticModel *model,
                   const ELineMap *line_map, const char *main_file,
                   char err[256]) {
  EmitCtx ctx;
  size_t i;

  if (err) err[0] = 0;
  memset(&ctx, 0, sizeof(ctx));
  ctx.next_label_id = 1u;
  ctx.next_worker_id = 1u;
  ctx.next_func_id = 1u;
  ctx.next_string_id = 1u;
  ctx.prog = prog;
  ctx.line_map = line_map;
  ctx.main_file = main_file;
  ctx.model = model;
  build_func_id_map(&ctx, prog);
  collect_program_strings(&ctx, prog);

  fputs("; E -> EPA ASM skeleton emitter\n", out);
  fputs("; Generated for manual inspection. Some operations remain comments/placeholders.\n\n", out);
  for (i = 0; i < ctx.string_count; i++) {
    fprintf(out, ".SSTR %u %s\n", ctx.strings[i].id, ctx.strings[i].literal);
  }
  if (ctx.string_count > 0u) fputc('\n', out);

  /* Emit DYNAMIC_POOL records for pools declared inside worker bodies.
     Top-level E_TOP_DYNAMIC pools are emitted inline in the main loop below;
     worker-inline ones are emitted here so the manifest precedes any ENTRY_START. */
  {
    size_t di;
    int any_inline = 0;
    for (di = 0; di < model->dynamic_pool_count; di++) {
      int is_top_level = 0;
      size_t j;
      for (j = 0; j < prog->count; j++) {
        if (prog->items[j].kind == E_TOP_DYNAMIC &&
            strcmp(prog->items[j].as.dynamic_decl.name, model->dynamic_pools[di].name) == 0) {
          is_top_level = 1;
          break;
        }
      }
      if (is_top_level) continue;
      if (!any_inline) {
        fputs("; worker-inline dynamic pool manifests\n", out);
        any_inline = 1;
      }
      fprintf(out, "DYNAMIC_POOL %zu %zu %u %u %u\n\n",
              di,
              model->dynamic_pools[di].element_size,
              model->dynamic_pools[di].min_free,
              model->dynamic_pools[di].max_free,
              model->dynamic_pools[di].grow_by);
    }
    if (any_inline) fputc('\n', out);
  }

  for (i = 0; i < prog->count; i++) {
    const ETopDecl *top = &prog->items[i];
    switch (top->kind) {
      case E_TOP_STRUCT:
        fprintf(out, "; struct %s\n\n", top->as.sdecl.name);
        break;
      case E_TOP_TYPE: {
        const ETypeLayout *layout = find_type_layout(model, top->as.tdecl.name);
        int frame_words = frame_words_for_function(model, top->as.tdecl.name);
        size_t j;
        fprintf(out, "; type %s\n", top->as.tdecl.name);
        if (layout) {
          fprintf(out, "; ghs_span=%zu\n", layout->total_size);
          for (j = 0; j < layout->field_count; j++) {
            fprintf(out, ";   field %s offset=%zu size=%zu\n",
                    layout->fields[j].name,
                    layout->fields[j].ghs_offset,
                    layout->fields[j].ghs_size);
          }
        }
        fprintf(out, "FUNC_START %u %d\n", ctx.next_func_id++, frame_words);
        ctx.current_function = NULL;
        ctx.current_worker = NULL;
        ctx.current_frame = find_frame(model, top->as.tdecl.name);
        ctx.current_type_layout = layout;
        emit_stmt(out, top->as.tdecl.body, &ctx, 1, NULL, NULL);
        ctx.current_type_layout = NULL;
        ctx.current_frame = NULL;
        fputs("FUNC_END\n\n", out);
        break;
      }
      case E_TOP_DECLARE:
        break;
      case E_TOP_DYNAMIC:
        fprintf(out, "; dynamic %s element=%s",
                top->as.dynamic_decl.name,
                top->as.dynamic_decl.element_type.name);
        if (top->as.dynamic_decl.element_type.array_len != 0u) {
          fprintf(out, "[%u]", top->as.dynamic_decl.element_type.array_len);
        }
        fprintf(out, " min_free=%u max_free=%u grow_by=%u maintenance=round-start replenish_to_min_free use_scope=entire-round\n",
                top->as.dynamic_decl.min_free,
                top->as.dynamic_decl.max_free,
                top->as.dynamic_decl.grow_by);
        {
          size_t di;
          for (di = 0; di < model->dynamic_pool_count; di++) {
            if (strcmp(model->dynamic_pools[di].name, top->as.dynamic_decl.name) == 0) {
              fprintf(out, ";   manifest element_size=%zu segmented-slot-pool pending-runtime\n\n",
                      model->dynamic_pools[di].element_size);
              fprintf(out, ";   header words=%u active_count@%u free_count@%u live_head@%u live_tail@%u free_head@%u\n",
                      model->dynamic_pools[di].header_word_count,
                      model->dynamic_pools[di].active_count_word,
                      model->dynamic_pools[di].free_count_word,
                      model->dynamic_pools[di].live_head_word,
                      model->dynamic_pools[di].live_tail_word,
                      model->dynamic_pools[di].free_head_word);
              fputs(";   grow policy: prepend new capacity onto free_head\n", out);
              fputs(";   append policy: use live_tail in main header for O(1) live append\n\n", out);
              fprintf(out, "DYNAMIC_POOL %zu %zu %u %u %u\n\n",
                      di,
                      model->dynamic_pools[di].element_size,
                      model->dynamic_pools[di].min_free,
                      model->dynamic_pools[di].max_free,
                      model->dynamic_pools[di].grow_by);
              break;
            }
          }
          if (di == model->dynamic_pool_count) fputc('\n', out);
        }
        break;
      case E_TOP_KERNEL:
        fputs("; kernel entry\n", out);
        fprintf(out, "ENTRY_START 0 %u %u %u\n",
                resolved_in_words(model, &top->as.kernel.attrs),
                resolved_out_words(model, &top->as.kernel.attrs),
                resolved_signal_mail_box_size(model, &top->as.kernel.attrs));
        ctx.current_function = NULL;
        ctx.current_worker = NULL;
        ctx.current_frame = find_frame(model, "kernel");
        emit_stmt(out, top->as.kernel.body, &ctx, 1, NULL, NULL);
        ctx.current_frame = NULL;
        fputs("  RET\n", out);
        fputs("ENTRY_END\n\n", out);
        break;
      case E_TOP_WORKER:
        fprintf(out, "; worker %s\n", top->as.worker.name);
        if (top->as.worker.param_count == 1u) {
          if (top->as.worker.params[0].type.union_count != 0u) {
            size_t ui;
            fprintf(out, "; accepted ingress types %s", top->as.worker.params[0].type.name);
            for (ui = 0; ui < top->as.worker.params[0].type.union_count; ui++) {
              fprintf(out, "|%s", top->as.worker.params[0].type.union_names[ui]);
            }
            fputc('\n', out);
          } else {
            const ETypeLayout *layout = find_type_layout(model, top->as.worker.params[0].type.name);
            if (layout) {
              fprintf(out, "; typed GHS view %s span=%zu\n", layout->type_name, layout->total_size);
            }
          }
        }
        fprintf(out, "ENTRY_START %u %u %u %u\n",
                ctx.next_worker_id++,
                resolved_in_words(model, &top->as.worker.attrs),
                resolved_out_words(model, &top->as.worker.attrs),
                resolved_signal_mail_box_size(model, &top->as.worker.attrs));
        {
          unsigned int loop_id = next_label(&ctx);
          ctx.iter_binding_count = 0u;
          ctx.current_worker = &top->as.worker;
          ctx.current_frame = find_frame(model, top->as.worker.name);
          /* Zero-initialise all static locals once before the worker loop. */
          if (ctx.current_frame) {
            size_t si;
            for (si = 0; si < ctx.current_frame->local_count; si++) {
              const ELocalBinding *sb = &ctx.current_frame->locals[si];
              if (sb->storage != E_LOCAL_STACK_STATIC) continue;
              emit_indent(out, 1);
              fprintf(out, "; static %s %s zero-init\n", sb->type_name, sb->name);
              if (sb->vm_local_words == 1u) {
                emit_indent(out, 1);
                fputs("PUSH 0\n", out);
                emit_indent(out, 1);
                EMIT_STORE_L(out, &ctx, sb->vm_local_slot);
              } else {
                /* Declared type: L_ALLOC once, then zero-fill and persist the 2-word reference. */
                emit_indent(out, 1);
                fprintf(out, "SET_R 0 %zu\n", sb->byte_size);
                emit_indent(out, 1);
                fputs("L_ALLOC\n", out);
                emit_zero_fill_static_type(out, sb, &ctx, 1);
              }
            }
          }
          /* Emit any static { } blocks — run-once code before the wait loop. */
          emit_static_blocks_in_stmt(out, top->as.worker.body, &ctx, 1);
          emit_indent(out, 1);
          fprintf(out, "E_WORKER_WAIT_%u:\n", loop_id);
          emit_indent(out, 1);
          fputs("WAIT_FOR_DATA\n", out);
          emit_indent(out, 1);
          fputs("; current wake carries the inbound GHS handle in worker ingress\n", out);
          emit_indent(out, 1);
          fputs("WORKER_TRX_IN_R 3\n", out);
          emit_indent(out, 1);
          fputs("WORKER_TRX_IN_R 0\n", out);
          emit_indent(out, 1);
          fputs("WORKER_TRX_IN_R 1\n", out);
          /* Emit maintenance comment for each pool declared inline in this worker. */
          {
            size_t pi;
            for (pi = 0; pi < ctx.model->dynamic_pool_count; pi++) {
              const EDynamicPool *pool = &ctx.model->dynamic_pools[pi];
              if (!pool_is_in_worker_body(pool->name, top->as.worker.body)) continue;
              emit_indent(out, 1);
              fprintf(out, "; dyn maintain pool %zu '%s' min_free=%u (round-start replenish)\n",
                      pi, pool->name, pool->min_free);
            }
          }
          emit_stmt(out, top->as.worker.body, &ctx, 1, NULL, NULL);
          emit_indent(out, 1);
          fprintf(out, "JMP E_WORKER_WAIT_%u\n", loop_id);
          ctx.current_worker = NULL;
          ctx.current_frame = NULL;
        }
        fputs("ENTRY_END\n\n", out);
        break;
      case E_TOP_FUNCTION: {
        int frame_words = frame_words_for_function(model, top->as.func.name);
        fprintf(out, "; function %s\n", top->as.func.name);
        fprintf(out, "FUNC_START %u %d\n", ctx.next_func_id++, frame_words);
        ctx.iter_binding_count = 0u;
        ctx.current_function = &top->as.func;
        ctx.current_frame = find_frame(model, top->as.func.name);
        emit_stmt(out, top->as.func.body, &ctx, 1, NULL, NULL);
        fputs("FUNC_END\n\n", out);
        ctx.current_function = NULL;
        ctx.current_frame = NULL;
        break;
      }
    }
  }

  fputs("END\n", out);
  for (i = 0; i < ctx.string_count; i++) free(ctx.strings[i].literal);
  free(ctx.strings);

  if (map_out) {
    fflush(out);
    e_write_epa_map(out, map_out);
  }

  return 1;
}
