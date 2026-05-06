#define _POSIX_C_SOURCE 200809L
#include "e_emit_epa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  unsigned int next_label_id;
  unsigned int next_worker_id;
  unsigned int next_func_id;
  const EProgram *prog;
  const ESemanticModel *model;
} EmitCtx;

static void emit_indent(FILE *out, int depth) {
  int i;
  for (i = 0; i < depth; i++) fputs("  ", out);
}

static const EFunctionFrame *find_frame(const ESemanticModel *model, const char *name) {
  size_t i;
  for (i = 0; i < model->frame_count; i++) {
    if (strcmp(model->frames[i].function->name, name) == 0) return &model->frames[i];
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

static void emit_expr(FILE *out, const EExpr *expr, const ESemanticModel *model, int depth);

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

static void emit_expr(FILE *out, const EExpr *expr, const ESemanticModel *model, int depth) {
  size_t i;
  emit_indent(out, depth);
  if (!expr) {
    fputs("; <null-expr>\n", out);
    return;
  }
  switch (expr->kind) {
    case E_EXPR_IDENT:
      fprintf(out, "; expr ident %s\n", expr->as.ident);
      break;
    case E_EXPR_INT:
      fprintf(out, "PUSH %lld\n", expr->as.int_lit);
      break;
    case E_EXPR_STRING:
      fprintf(out, "; expr string %s\n", expr->as.string_lit);
      break;
    case E_EXPR_BINARY:
      fprintf(out, "; expr binary\n");
      emit_expr(out, expr->as.binary.lhs, model, depth);
      emit_expr(out, expr->as.binary.rhs, model, depth);
      emit_indent(out, depth);
      switch (expr->as.binary.op) {
        case E_BIN_ADD: fputs("ADD_I32\n", out); break;
        case E_BIN_SUB: fputs("SUB_I32\n", out); break;
        case E_BIN_MUL: fputs("MUL_I32\n", out); break;
        case E_BIN_DIV: fputs("; DIV pending lowering\n", out); break;
      }
      break;
    case E_EXPR_ASSIGN:
      fprintf(out, "; assign pending lowering\n");
      emit_expr(out, expr->as.assign.lhs, model, depth);
      emit_expr(out, expr->as.assign.rhs, model, depth);
      break;
    case E_EXPR_CALL:
      fprintf(out, "; call %s argc=%zu pending lowering\n", expr->as.call.callee, expr->as.call.arg_count);
      for (i = 0; i < expr->as.call.arg_count; i++) emit_expr(out, expr->as.call.args[i], model, depth);
      break;
    case E_EXPR_FIELD:
      fputs("; field ", out);
      emit_field_path(out, expr, model);
      fputc('\n', out);
      break;
  }
}

static unsigned int next_label(EmitCtx *ctx) {
  return ctx->next_label_id++;
}

static void emit_stmt(FILE *out, const EStmt *stmt, EmitCtx *ctx, int depth, const char *break_label) {
  size_t i;
  if (!stmt) return;
  switch (stmt->kind) {
    case E_STMT_DECL:
      emit_indent(out, depth);
      fprintf(out, "; decl %s %s\n", stmt->as.decl.type.name, stmt->as.decl.name);
      if (stmt->as.decl.init) emit_expr(out, stmt->as.decl.init, ctx->model, depth);
      break;
    case E_STMT_RETURN:
      emit_indent(out, depth);
      fputs("; return\n", out);
      if (stmt->as.ret.value) emit_expr(out, stmt->as.ret.value, ctx->model, depth);
      emit_indent(out, depth);
      fputs("RET\n", out);
      break;
    case E_STMT_EXPR:
      emit_expr(out, stmt->as.expr, ctx->model, depth);
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
      emit_expr(out, stmt->as.if_stmt.cond, ctx->model, depth);
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
    case E_STMT_SWITCH: {
      unsigned int join_id = next_label(ctx);
      emit_indent(out, depth);
      fputs("; switch target\n", out);
      emit_expr(out, stmt->as.switch_stmt.target, ctx->model, depth);
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
  }
}

static int frame_words_for_function(const ESemanticModel *model, const char *name) {
  const EFunctionFrame *frame = find_frame(model, name);
  if (!frame) return 0;
  return (int)((frame->total_size + 3u) / 4u);
}

int e_emit_epa_asm(FILE *out, const EProgram *prog, const ESemanticModel *model, char err[256]) {
  EmitCtx ctx;
  size_t i;

  if (err) err[0] = 0;
  memset(&ctx, 0, sizeof(ctx));
  ctx.next_label_id = 1u;
  ctx.next_worker_id = 1u;
  ctx.next_func_id = 1u;
  ctx.prog = prog;
  ctx.model = model;

  fputs("; E -> EPA ASM skeleton emitter\n", out);
  fputs("; Generated for manual inspection. Some operations remain comments/placeholders.\n\n", out);

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
        emit_stmt(out, top->as.tdecl.body, &ctx, 1, NULL);
        fputs("FUNC_END\n\n", out);
        break;
      }
      case E_TOP_KERNEL:
        fputs("; kernel entry\n", out);
        fputs("ENTRY_START 0 0 0 0\n", out);
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
        fprintf(out, "ENTRY_START %u 0 0 0\n", ctx.next_worker_id++);
        emit_stmt(out, top->as.worker.body, &ctx, 1, NULL);
        fputs("ENTRY_END\n\n", out);
        break;
      case E_TOP_FUNCTION: {
        int frame_words = frame_words_for_function(model, top->as.func.name);
        fprintf(out, "; function %s\n", top->as.func.name);
        fprintf(out, "FUNC_START %u %d\n", ctx.next_func_id++, frame_words);
        emit_stmt(out, top->as.func.body, &ctx, 1, NULL);
        fputs("FUNC_END\n\n", out);
        break;
      }
    }
  }

  fputs("END\n", out);
  return 1;
}
