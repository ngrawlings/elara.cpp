#define _POSIX_C_SOURCE 200809L
#include "e_emit_epa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char *literal;
  unsigned int id;
} EmitStringConst;

typedef struct {
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
} EmitCtx;

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

static const ETypeLayout *find_type_layout(const ESemanticModel *model, const char *name) {
  size_t i;
  for (i = 0; i < model->type_layout_count; i++) {
    if (strcmp(model->type_layouts[i].type_name, name) == 0) return &model->type_layouts[i];
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
    case E_STMT_BREAK:
    case E_STMT_NEXT:
    case E_STMT_RAW_EPA:
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
        break;
    }
  }
}

static void emit_expr(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_log_builtin(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth);
static int emit_scalar_store(FILE *out, const char *name, EmitCtx *ctx, int depth);
static int emit_worker_field_load(FILE *out, const char *base_name, const char *field_name, EmitCtx *ctx, int depth);

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

static void emit_expr(FILE *out, const EExpr *expr, EmitCtx *ctx, int depth) {
  size_t i;
  emit_indent(out, depth);
  if (!expr) {
    fputs("; <null-expr>\n", out);
    return;
  }
  switch (expr->kind) {
    case E_EXPR_IDENT:
    {
      const ELocalBinding *local = find_local_binding_by_name(ctx->current_frame, expr->as.ident);
      const EFunctionParamCheck *param = find_param_check_by_name(ctx, expr->as.ident);
      if (local && local->vm_local_words == 1u) {
        fprintf(out, "LOAD_L %u\n", local->vm_local_slot);
      } else if (param && param->vm_local_words == 1u) {
        fprintf(out, "LOAD_L %u\n", param->vm_local_slot);
      } else {
        fprintf(out, "; expr ident %s\n", expr->as.ident);
      }
      break;
    }
    case E_EXPR_INT:
      fprintf(out, "PUSH %lld\n", expr->as.int_lit);
      break;
    case E_EXPR_STRING:
      fprintf(out, "; expr string %s\n", expr->as.string_lit);
      break;
    case E_EXPR_BINARY:
      fprintf(out, "; expr binary\n");
      emit_expr(out, expr->as.binary.lhs, ctx, depth);
      emit_expr(out, expr->as.binary.rhs, ctx, depth);
      emit_indent(out, depth);
      switch (expr->as.binary.op) {
        case E_BIN_ADD: fputs("ADD_I32\n", out); break;
        case E_BIN_SUB: fputs("SUB_I32\n", out); break;
        case E_BIN_MUL: fputs("MUL_I32\n", out); break;
        case E_BIN_DIV: fputs("; DIV pending lowering\n", out); break;
      }
      break;
    case E_EXPR_ASSIGN:
      emit_expr(out, expr->as.assign.rhs, ctx, depth);
      if (expr->as.assign.lhs && expr->as.assign.lhs->kind == E_EXPR_IDENT) {
        emit_scalar_store(out, expr->as.assign.lhs->as.ident, ctx, depth);
        emit_indent(out, depth);
        {
          const ELocalBinding *local = find_local_binding_by_name(ctx->current_frame, expr->as.assign.lhs->as.ident);
          const EFunctionParamCheck *param = find_param_check_by_name(ctx, expr->as.assign.lhs->as.ident);
          if (local && local->vm_local_words == 1u) {
            fprintf(out, "LOAD_L %u\n", local->vm_local_slot);
          } else if (param && param->vm_local_words == 1u) {
            fprintf(out, "LOAD_L %u\n", param->vm_local_slot);
          } else {
            fprintf(out, "; assign target %s unsupported\n", expr->as.assign.lhs->as.ident);
          }
        }
      } else {
        emit_indent(out, depth);
        fputs("; assign pending lowering\n", out);
      }
      break;
    case E_EXPR_CALL:
      if (strcmp(expr->as.call.callee, "log") == 0) {
        emit_log_builtin(out, expr, ctx, depth);
        break;
      }
      fprintf(out, "; call %s argc=%zu pending lowering\n", expr->as.call.callee, expr->as.call.arg_count);
      for (i = 0; i < expr->as.call.arg_count; i++) emit_expr(out, expr->as.call.args[i], ctx, depth);
      break;
    case E_EXPR_FIELD:
      if (expr->as.field.base && expr->as.field.base->kind == E_EXPR_IDENT &&
          emit_worker_field_load(out, expr->as.field.base->as.ident, expr->as.field.field, ctx, depth)) {
        break;
      }
      fputs("; field ", out);
      emit_field_path(out, expr, ctx->model);
      fputc('\n', out);
      break;
  }
}

static int emit_scalar_store(FILE *out, const char *name, EmitCtx *ctx, int depth) {
  const ELocalBinding *local = find_local_binding_by_name(ctx->current_frame, name);
  const EFunctionParamCheck *param = find_param_check_by_name(ctx, name);
  emit_indent(out, depth);
  if (local && local->vm_local_words == 1u) {
    fprintf(out, "STORE_L %u\n", local->vm_local_slot);
    return 1;
  }
  if (param && param->vm_local_words == 1u) {
    fprintf(out, "STORE_L %u\n", param->vm_local_slot);
    return 1;
  }
  fprintf(out, "; store %s unsupported\n", name);
  return 0;
}

static int emit_worker_field_load(FILE *out, const char *base_name, const char *field_name, EmitCtx *ctx, int depth) {
  const EWorker *worker = ctx->current_worker;
  const EGhsField *field;
  if (!worker) return 0;
  if (worker->param_count != 1u) return 0;
  if (strcmp(worker->params[0].name, base_name) != 0) return 0;
  field = find_field_layout(ctx->model, worker->params[0].type.name, field_name);
  if (!field || field->ghs_size != 4u) return 0;
  emit_indent(out, depth);
  fprintf(out, "SET_R 2 %zu\n", field->ghs_offset);
  emit_indent(out, depth);
  fputs("GR_MOV4 0\n", out);
  emit_indent(out, depth);
  fputs("PUSH R0\n", out);
  return 1;
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

static void emit_stmt(FILE *out, const EStmt *stmt, EmitCtx *ctx, int depth, const char *break_label) {
  size_t i;
  if (!stmt) return;
  switch (stmt->kind) {
    case E_STMT_DECL:
    {
      const ELocalBinding *binding = find_local_binding(ctx->current_frame, stmt);
      emit_indent(out, depth);
      fputs("; decl ", out);
      if (stmt->as.decl.is_reg) fputs("reg ", out);
      if (stmt->as.decl.is_local) fputs("local ", out);
      fprintf(out, "%s", stmt->as.decl.type.name);
      if (stmt->as.decl.type.array_len != 0u) fprintf(out, "[%u]", stmt->as.decl.type.array_len);
      fprintf(out, " %s", stmt->as.decl.name);
      if (binding) {
        if (binding->storage == E_LOCAL_STACK) {
          fprintf(out, " stack+%zu size=%zu", binding->stack_offset, binding->byte_size);
        } else if (binding->storage == E_LOCAL_ARENA_SCOPED) {
          fprintf(out, " local-scope-arena size=%zu", binding->byte_size);
        } else {
          fprintf(out, " r%u", binding->reg_index);
          if (binding->reg_words == 2u) fprintf(out, ":r%u", binding->reg_index + 1u);
          fprintf(out, " size=%zu", binding->byte_size);
        }
      }
      fputc('\n', out);
      if (stmt->as.decl.init) {
        emit_expr(out, stmt->as.decl.init, ctx, depth);
        if (binding && binding->vm_local_words == 1u) {
          emit_indent(out, depth);
          fprintf(out, "STORE_L %u\n", binding->vm_local_slot);
        }
      }
      break;
    }
    case E_STMT_RETURN:
      emit_indent(out, depth);
      fputs("; return\n", out);
      if (stmt->as.ret.value) emit_expr(out, stmt->as.ret.value, ctx, depth);
      emit_indent(out, depth);
      fputs("RET\n", out);
      break;
    case E_STMT_EXPR:
      emit_expr(out, stmt->as.expr, ctx, depth);
      break;
    case E_STMT_BLOCK:
      for (i = 0; i < stmt->as.block.count; i++) {
        emit_stmt(out, stmt->as.block.items[i], ctx, depth, break_label);
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
      emit_stmt(out, stmt->as.if_stmt.then_branch, ctx, depth, break_label);
      emit_indent(out, depth);
      fprintf(out, "JMP E_IF_JOIN_%u\n", join_id);
      emit_indent(out, depth);
      fprintf(out, "E_IF_ELSE_%u:\n", else_id);
      if (stmt->as.if_stmt.else_branch) {
        emit_stmt(out, stmt->as.if_stmt.else_branch, ctx, depth, break_label);
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
      char exit_label[64];
      snprintf(exit_label, sizeof(exit_label), "E_WHILE_EXIT_%u", exit_id);
      emit_indent(out, depth);
      fprintf(out, "E_WHILE_HEAD_%u:\n", head_id);
      emit_indent(out, depth);
      fputs("; while condition\n", out);
      emit_expr(out, stmt->as.while_stmt.cond, ctx, depth);
      emit_indent(out, depth);
      fputs("POP 0\n", out);
      emit_indent(out, depth);
      fprintf(out, "JZ %s\n", exit_label);
      emit_stmt(out, stmt->as.while_stmt.body, ctx, depth, exit_label);
      emit_indent(out, depth);
      fprintf(out, "JMP E_WHILE_HEAD_%u\n", head_id);
      emit_indent(out, depth);
      fprintf(out, "%s:\n", exit_label);
      break;
    }
    case E_STMT_FOR: {
      unsigned int head_id = next_label(ctx);
      unsigned int step_id = next_label(ctx);
      unsigned int exit_id = next_label(ctx);
      char exit_label[64];
      snprintf(exit_label, sizeof(exit_label), "E_FOR_EXIT_%u", exit_id);
      if (stmt->as.for_stmt.init) {
        emit_indent(out, depth);
        fputs("; for init\n", out);
        emit_stmt(out, stmt->as.for_stmt.init, ctx, depth, break_label);
      }
      emit_indent(out, depth);
      fprintf(out, "E_FOR_HEAD_%u:\n", head_id);
      if (stmt->as.for_stmt.cond) {
        emit_indent(out, depth);
        fputs("; for condition\n", out);
        emit_expr(out, stmt->as.for_stmt.cond, ctx, depth);
        emit_indent(out, depth);
        fputs("POP 0\n", out);
        emit_indent(out, depth);
        fprintf(out, "JZ %s\n", exit_label);
      }
      emit_stmt(out, stmt->as.for_stmt.body, ctx, depth, exit_label);
      emit_indent(out, depth);
      fprintf(out, "E_FOR_STEP_%u:\n", step_id);
      if (stmt->as.for_stmt.step) {
        emit_indent(out, depth);
        fputs("; for step\n", out);
        emit_expr(out, stmt->as.for_stmt.step, ctx, depth);
      }
      emit_indent(out, depth);
      fprintf(out, "JMP E_FOR_HEAD_%u\n", head_id);
      emit_indent(out, depth);
      fprintf(out, "%s:\n", exit_label);
      break;
    }
    case E_STMT_SWITCH: {
      unsigned int join_id = next_label(ctx);
      emit_indent(out, depth);
      fputs("; switch target\n", out);
      emit_expr(out, stmt->as.switch_stmt.target, ctx, depth);
      for (i = 0; i < stmt->as.switch_stmt.case_count; i++) {
        unsigned int case_id = next_label(ctx);
        emit_indent(out, depth);
        if (stmt->as.switch_stmt.cases[i].is_default) {
          fprintf(out, "; default -> E_SWITCH_CASE_%u\n", case_id);
        } else {
          fprintf(out, "; case %lld -> E_SWITCH_CASE_%u\n",
                  stmt->as.switch_stmt.cases[i].value, case_id);
        }
        emit_indent(out, depth);
        fprintf(out, "E_SWITCH_CASE_%u:\n", case_id);
        {
          size_t j;
          char join_label[64];
          snprintf(join_label, sizeof(join_label), "E_SWITCH_JOIN_%u", join_id);
          for (j = 0; j < stmt->as.switch_stmt.cases[i].body.count; j++) {
            emit_stmt(out, stmt->as.switch_stmt.cases[i].body.items[j], ctx, depth, join_label);
          }
        }
      }
      emit_indent(out, depth);
      fprintf(out, "E_SWITCH_JOIN_%u:\n", join_id);
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
  }
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

int e_emit_epa_asm(FILE *out, const EProgram *prog, const ESemanticModel *model, char err[256]) {
  EmitCtx ctx;
  size_t i;

  if (err) err[0] = 0;
  memset(&ctx, 0, sizeof(ctx));
  ctx.next_label_id = 1u;
  ctx.next_worker_id = 1u;
  ctx.next_func_id = 1u;
  ctx.next_string_id = 1u;
  ctx.prog = prog;
  ctx.model = model;
  collect_program_strings(&ctx, prog);

  fputs("; E -> EPA ASM skeleton emitter\n", out);
  fputs("; Generated for manual inspection. Some operations remain comments/placeholders.\n\n", out);
  for (i = 0; i < ctx.string_count; i++) {
    fprintf(out, ".SSTR %u %s\n", ctx.strings[i].id, ctx.strings[i].literal);
  }
  if (ctx.string_count > 0u) fputc('\n', out);

  for (i = 0; i < prog->count; i++) {
    const ETopDecl *top = &prog->items[i];
    switch (top->kind) {
      case E_TOP_STRUCT:
        fprintf(out, "; struct %s\n\n", top->as.sdecl.name);
        break;
      case E_TOP_TYPE: {
        const ETypeLayout *layout = find_type_layout(model, top->as.tdecl.name);
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
        fprintf(out, "FUNC_START %u 0\n", ctx.next_func_id++);
        ctx.current_frame = NULL;
        emit_stmt(out, top->as.tdecl.body, &ctx, 1, NULL);
        fputs("FUNC_END\n\n", out);
        break;
      }
      case E_TOP_DECLARE:
        break;
      case E_TOP_KERNEL:
        fputs("; kernel entry\n", out);
        fprintf(out, "ENTRY_START 0 %u %u %u\n",
                resolved_in_words(model, &top->as.kernel.attrs),
                resolved_out_words(model, &top->as.kernel.attrs),
                resolved_signal_mail_box_size(model, &top->as.kernel.attrs));
        ctx.current_frame = NULL;
        emit_stmt(out, top->as.kernel.body, &ctx, 1, NULL);
        fputs("ENTRY_END\n\n", out);
        break;
      case E_TOP_WORKER:
        fprintf(out, "; worker %s\n", top->as.worker.name);
        if (top->as.worker.param_count == 1u) {
          const ETypeLayout *layout = find_type_layout(model, top->as.worker.params[0].type.name);
          if (layout) {
            fprintf(out, "; typed GHS view %s span=%zu\n", layout->type_name, layout->total_size);
          }
        }
        fprintf(out, "ENTRY_START %u %u %u %u\n",
                ctx.next_worker_id++,
                resolved_in_words(model, &top->as.worker.attrs),
                resolved_out_words(model, &top->as.worker.attrs),
                resolved_signal_mail_box_size(model, &top->as.worker.attrs));
        {
          unsigned int loop_id = next_label(&ctx);
          ctx.current_worker = &top->as.worker;
          ctx.current_frame = find_frame(model, top->as.worker.name);
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
          emit_indent(out, 1);
          fputs("; E worker body begins after ingress wake-up\n", out);
          emit_stmt(out, top->as.worker.body, &ctx, 1, NULL);
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
        ctx.current_function = &top->as.func;
        ctx.current_frame = find_frame(model, top->as.func.name);
        emit_stmt(out, top->as.func.body, &ctx, 1, NULL);
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
  return 1;
}
