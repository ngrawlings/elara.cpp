#define _POSIX_C_SOURCE 200809L
#include "e_ast.h"

#include <stdlib.h>
#include <string.h>

static void free_type_ref(ETypeRef *type) {
  size_t i;
  if (!type) return;
  free(type->name);
  for (i = 0; i < type->union_count; i++) free(type->union_names[i]);
  free(type->union_names);
  type->name = NULL;
  type->union_names = NULL;
  type->union_count = 0;
  type->array_len = 0u;
}

static void free_expr(EExpr *e) {
  size_t i;
  if (!e) return;
  switch (e->kind) {
    case E_EXPR_IDENT:
      free(e->as.ident);
      break;
    case E_EXPR_STRING:
      free(e->as.string_lit);
      break;
    case E_EXPR_BINARY:
      free_expr(e->as.binary.lhs);
      free_expr(e->as.binary.rhs);
      break;
    case E_EXPR_ASSIGN:
      free_expr(e->as.assign.lhs);
      free_expr(e->as.assign.rhs);
      break;
    case E_EXPR_CALL:
      free(e->as.call.callee);
      for (i = 0; i < e->as.call.arg_count; i++) free_expr(e->as.call.args[i]);
      free(e->as.call.args);
      break;
    case E_EXPR_FIELD:
      free_expr(e->as.field.base);
      free(e->as.field.field);
      break;
    case E_EXPR_INDEX:
      free_expr(e->as.index.base);
      free_expr(e->as.index.index);
      break;
    case E_EXPR_INT:
      break;
  }
  free(e);
}

static void free_stmt(EStmt *s) {
  size_t i;
  if (!s) return;
  switch (s->kind) {
    case E_STMT_DECL:
      free_type_ref(&s->as.decl.type);
      free(s->as.decl.name);
      free_expr(s->as.decl.init);
      break;
    case E_STMT_RETURN:
      free_expr(s->as.ret.value);
      break;
    case E_STMT_EXPR:
      free_expr(s->as.expr);
      break;
    case E_STMT_BLOCK:
      for (i = 0; i < s->as.block.count; i++) free_stmt(s->as.block.items[i]);
      free(s->as.block.items);
      break;
    case E_STMT_IF:
      free_expr(s->as.if_stmt.cond);
      free_stmt(s->as.if_stmt.then_branch);
      free_stmt(s->as.if_stmt.else_branch);
      break;
    case E_STMT_WHILE:
      free_expr(s->as.while_stmt.cond);
      free_stmt(s->as.while_stmt.body);
      break;
    case E_STMT_FOR:
      free_stmt(s->as.for_stmt.init);
      free_expr(s->as.for_stmt.cond);
      free_expr(s->as.for_stmt.step);
      free_stmt(s->as.for_stmt.body);
      break;
    case E_STMT_SWITCH:
      free_expr(s->as.switch_stmt.target);
      for (i = 0; i < s->as.switch_stmt.case_count; i++) {
        size_t j;
        for (j = 0; j < s->as.switch_stmt.cases[i].body.count; j++) {
          free_stmt(s->as.switch_stmt.cases[i].body.items[j]);
        }
        free(s->as.switch_stmt.cases[i].body.items);
      }
      free(s->as.switch_stmt.cases);
      break;
    case E_STMT_BREAK:
    case E_STMT_CONTINUE:
      break;
    case E_STMT_NEXT:
      free(s->as.next_stmt.worker_name);
      break;
    case E_STMT_RAW_EPA:
      free(s->as.raw_epa.text);
      break;
    case E_STMT_KERNAL_ID:
      free(s->as.kernal_id.name);
      break;
    case E_STMT_FOREACH:
      free_stmt(s->as.foreach_stmt.var_decl);
      free(s->as.foreach_stmt.iter_name);
      free_stmt(s->as.foreach_stmt.body);
      break;
    case E_STMT_DYNAMIC:
      free(s->as.dynamic_decl.name);
      free(s->as.dynamic_decl.element_type.name);
      break;
    case E_STMT_STATIC_BLOCK: {
      size_t i;
      for (i = 0; i < s->as.static_block.count; i++) free_stmt(s->as.static_block.items[i]);
      free(s->as.static_block.items);
      break;
    }
  }
  free(s);
}

static void free_params(EParam *params, size_t param_count) {
  size_t i;
  for (i = 0; i < param_count; i++) {
    free_type_ref(&params[i].type);
    free(params[i].name);
  }
  free(params);
}

static void free_acl_entries(EAclEntry *entries, size_t count) {
  size_t i;
  for (i = 0; i < count; i++) {
    free(entries[i].remote_kernel);
    free(entries[i].local_worker);
  }
  free(entries);
}

void e_program_free(EProgram *p) {
  size_t i;
  if (!p) return;
  for (i = 0; i < p->count; i++) {
    ETopDecl *d = &p->items[i];
    switch (d->kind) {
      case E_TOP_STRUCT:
        free(d->as.sdecl.name);
        break;
      case E_TOP_KERNEL:
        free_params(d->as.kernel.params, d->as.kernel.param_count);
        free_acl_entries(d->as.kernel.acl_entries, d->as.kernel.acl_count);
        free_stmt(d->as.kernel.body);
        break;
      case E_TOP_WORKER:
        free(d->as.worker.name);
        free_params(d->as.worker.params, d->as.worker.param_count);
        free_stmt(d->as.worker.body);
        break;
      case E_TOP_FUNCTION:
        free_type_ref(&d->as.func.return_type);
        free(d->as.func.name);
        free_params(d->as.func.params, d->as.func.param_count);
        free_stmt(d->as.func.body);
        break;
      case E_TOP_TYPE:
        free(d->as.tdecl.name);
        free_params(d->as.tdecl.params, d->as.tdecl.param_count);
        free_stmt(d->as.tdecl.body);
        break;
      case E_TOP_DECLARE:
        break;
      case E_TOP_DYNAMIC:
        free(d->as.dynamic_decl.name);
        free_type_ref(&d->as.dynamic_decl.element_type);
        break;
    }
  }
  free(p->items);
  p->items = NULL;
  p->count = 0;
}

static void indent(FILE *out, int depth) {
  int i;
  for (i = 0; i < depth; i++) fputs("  ", out);
}

static void dump_type_ref(FILE *out, const ETypeRef *type) {
  size_t i;
  fprintf(out, "%s", type->name);
  for (i = 0; i < type->union_count; i++) {
    fprintf(out, "|%s", type->union_names[i]);
  }
  if (type->array_len != 0u) {
    fprintf(out, "[%u]", type->array_len);
  }
}

static const char *bin_name(EBinaryOp op) {
  switch (op) {
    case E_BIN_ADD: return "+";
    case E_BIN_SUB: return "-";
    case E_BIN_MUL: return "*";
    case E_BIN_DIV: return "/";
    case E_BIN_EQ: return "==";
    case E_BIN_NE: return "!=";
    case E_BIN_LT: return "<";
    case E_BIN_LE: return "<=";
    case E_BIN_GT: return ">";
    case E_BIN_GE: return ">=";
  }
  return "?";
}

static void dump_expr(FILE *out, const EExpr *e, int depth) {
  size_t i;
  if (!e) {
    indent(out, depth);
    fputs("(null-expr)\n", out);
    return;
  }
  switch (e->kind) {
    case E_EXPR_IDENT:
      indent(out, depth);
      fprintf(out, "ident %s\n", e->as.ident);
      break;
    case E_EXPR_INT:
      indent(out, depth);
      fprintf(out, "int %lld\n", e->as.int_lit);
      break;
    case E_EXPR_STRING:
      indent(out, depth);
      fprintf(out, "string %s\n", e->as.string_lit);
      break;
    case E_EXPR_BINARY:
      indent(out, depth);
      fprintf(out, "binary %s\n", bin_name(e->as.binary.op));
      dump_expr(out, e->as.binary.lhs, depth + 1);
      dump_expr(out, e->as.binary.rhs, depth + 1);
      break;
    case E_EXPR_ASSIGN:
      indent(out, depth);
      fputs("assign\n", out);
      dump_expr(out, e->as.assign.lhs, depth + 1);
      dump_expr(out, e->as.assign.rhs, depth + 1);
      break;
    case E_EXPR_CALL:
      indent(out, depth);
      fprintf(out, "call %s argc=%zu\n", e->as.call.callee, e->as.call.arg_count);
      for (i = 0; i < e->as.call.arg_count; i++) dump_expr(out, e->as.call.args[i], depth + 1);
      break;
    case E_EXPR_FIELD:
      indent(out, depth);
      fprintf(out, "field %s\n", e->as.field.field);
      dump_expr(out, e->as.field.base, depth + 1);
      break;
    case E_EXPR_INDEX:
      indent(out, depth);
      fputs("index\n", out);
      dump_expr(out, e->as.index.base, depth + 1);
      dump_expr(out, e->as.index.index, depth + 1);
      break;
  }
}

static void dump_stmt(FILE *out, const EStmt *s, int depth) {
  size_t i;
  if (!s) return;
  switch (s->kind) {
    case E_STMT_DECL:
      indent(out, depth);
      fputs("decl ", out);
      if (s->as.decl.is_reg) fputs("reg ", out);
      if (s->as.decl.is_local) fputs("local ", out);
      dump_type_ref(out, &s->as.decl.type);
      fprintf(out, " %s\n", s->as.decl.name);
      if (s->as.decl.init) dump_expr(out, s->as.decl.init, depth + 1);
      break;
    case E_STMT_RETURN:
      indent(out, depth);
      fputs("return\n", out);
      if (s->as.ret.value) dump_expr(out, s->as.ret.value, depth + 1);
      break;
    case E_STMT_EXPR:
      indent(out, depth);
      fputs("expr\n", out);
      dump_expr(out, s->as.expr, depth + 1);
      break;
    case E_STMT_BLOCK:
      indent(out, depth);
      fputs("block\n", out);
      for (i = 0; i < s->as.block.count; i++) dump_stmt(out, s->as.block.items[i], depth + 1);
      break;
    case E_STMT_IF:
      indent(out, depth);
      fputs("if\n", out);
      indent(out, depth + 1);
      fputs("cond\n", out);
      dump_expr(out, s->as.if_stmt.cond, depth + 2);
      indent(out, depth + 1);
      fputs("then\n", out);
      dump_stmt(out, s->as.if_stmt.then_branch, depth + 2);
      if (s->as.if_stmt.else_branch) {
        indent(out, depth + 1);
        fputs("else\n", out);
        dump_stmt(out, s->as.if_stmt.else_branch, depth + 2);
      }
      break;
    case E_STMT_WHILE:
      indent(out, depth);
      fputs("while\n", out);
      indent(out, depth + 1);
      fputs("cond\n", out);
      dump_expr(out, s->as.while_stmt.cond, depth + 2);
      indent(out, depth + 1);
      fputs("body\n", out);
      dump_stmt(out, s->as.while_stmt.body, depth + 2);
      break;
    case E_STMT_FOR:
      indent(out, depth);
      fputs("for\n", out);
      if (s->as.for_stmt.init) {
        indent(out, depth + 1);
        fputs("init\n", out);
        dump_stmt(out, s->as.for_stmt.init, depth + 2);
      }
      if (s->as.for_stmt.cond) {
        indent(out, depth + 1);
        fputs("cond\n", out);
        dump_expr(out, s->as.for_stmt.cond, depth + 2);
      }
      if (s->as.for_stmt.step) {
        indent(out, depth + 1);
        fputs("step\n", out);
        dump_expr(out, s->as.for_stmt.step, depth + 2);
      }
      indent(out, depth + 1);
      fputs("body\n", out);
      dump_stmt(out, s->as.for_stmt.body, depth + 2);
      break;
    case E_STMT_SWITCH:
      indent(out, depth);
      fputs("switch\n", out);
      indent(out, depth + 1);
      fputs("target\n", out);
      dump_expr(out, s->as.switch_stmt.target, depth + 2);
      for (i = 0; i < s->as.switch_stmt.case_count; i++) {
        size_t j;
        indent(out, depth + 1);
        if (s->as.switch_stmt.cases[i].is_default) {
          fputs("default\n", out);
        } else {
          fprintf(out, "case %lld\n", s->as.switch_stmt.cases[i].value);
        }
        for (j = 0; j < s->as.switch_stmt.cases[i].body.count; j++) {
          dump_stmt(out, s->as.switch_stmt.cases[i].body.items[j], depth + 2);
        }
      }
      break;
    case E_STMT_BREAK:
      indent(out, depth);
      fputs("break\n", out);
      break;
    case E_STMT_CONTINUE:
      indent(out, depth);
      fputs("continue\n", out);
      break;
    case E_STMT_NEXT:
      indent(out, depth);
      fprintf(out, "next %s\n", s->as.next_stmt.worker_name);
      break;
    case E_STMT_RAW_EPA:
      indent(out, depth);
      fputs("EPA {\n", out);
      if (s->as.raw_epa.text && s->as.raw_epa.text[0]) {
        const char *cursor = s->as.raw_epa.text;
        while (*cursor) {
          const char *line_end = strchr(cursor, '\n');
          indent(out, depth + 1);
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
      indent(out, depth);
      fputs("}\n", out);
      break;
    case E_STMT_KERNAL_ID:
      indent(out, depth);
      fprintf(out, "kernalId(\"%s\");\n", s->as.kernal_id.name ? s->as.kernal_id.name : "");
      break;
    case E_STMT_FOREACH:
      indent(out, depth);
      fprintf(out, "foreach(%s = dynamic_next(%s))\n",
              s->as.foreach_stmt.var_decl ? s->as.foreach_stmt.var_decl->as.decl.name : "?",
              s->as.foreach_stmt.iter_name ? s->as.foreach_stmt.iter_name : "?");
      dump_stmt(out, s->as.foreach_stmt.body, depth + 1);
      break;
    case E_STMT_DYNAMIC:
      indent(out, depth);
      fprintf(out, "dynamic %s(%s, %u, %u, %u);\n",
              s->as.dynamic_decl.name ? s->as.dynamic_decl.name : "?",
              s->as.dynamic_decl.element_type.name ? s->as.dynamic_decl.element_type.name : "?",
              s->as.dynamic_decl.min_free, s->as.dynamic_decl.max_free, s->as.dynamic_decl.grow_by);
      break;
    case E_STMT_STATIC_BLOCK: {
      size_t i;
      indent(out, depth);
      fputs("static {\n", out);
      for (i = 0; i < s->as.static_block.count; i++) dump_stmt(out, s->as.static_block.items[i], depth + 1);
      indent(out, depth);
      fputs("}\n", out);
      break;
    }
  }
}

static void dump_entry_attrs(FILE *out, const EEntryAttributes *attrs) {
  int any = 0;
  if (!attrs) return;
  if (attrs->has_in_words) {
    fprintf(out, " in_words:%u", attrs->in_words);
    any = 1;
  }
  if (attrs->has_out_words) {
    fprintf(out, " out_words:%u", attrs->out_words);
    any = 1;
  }
  if (attrs->has_signal_mail_box_size) {
    fprintf(out, " signal_mail_box_size:%u", attrs->signal_mail_box_size);
    any = 1;
  }
  if (any) {
    fputc('\n', out);
  }
}

void e_program_dump(FILE *out, const EProgram *p) {
  size_t i, j;
  for (i = 0; i < p->count; i++) {
    const ETopDecl *d = &p->items[i];
    switch (d->kind) {
      case E_TOP_STRUCT:
        fprintf(out, "struct %s;\n", d->as.sdecl.name);
        break;
      case E_TOP_KERNEL:
        if (d->as.kernel.attrs.has_in_words ||
            d->as.kernel.attrs.has_out_words ||
            d->as.kernel.attrs.has_signal_mail_box_size) {
          fputs("@attributes", out);
          dump_entry_attrs(out, &d->as.kernel.attrs);
        }
        fputs("kernel(", out);
        for (j = 0; j < d->as.kernel.param_count; j++) {
          if (j) fputs(", ", out);
          dump_type_ref(out, &d->as.kernel.params[j].type);
          fprintf(out, " %s", d->as.kernel.params[j].name);
        }
        fputs(")\n", out);
        if (d->as.kernel.acl_count > 0u) {
          size_t ai;
          fputs("acl {\n", out);
          for (ai = 0; ai < d->as.kernel.acl_count; ai++) {
            fprintf(out, "  %s -> %s;\n",
                    d->as.kernel.acl_entries[ai].remote_kernel,
                    d->as.kernel.acl_entries[ai].local_worker);
          }
          fputs("}\n", out);
        }
        dump_stmt(out, d->as.kernel.body, 1);
        break;
      case E_TOP_WORKER:
        if (d->as.worker.attrs.has_in_words ||
            d->as.worker.attrs.has_out_words ||
            d->as.worker.attrs.has_signal_mail_box_size) {
          fputs("@attributes", out);
          dump_entry_attrs(out, &d->as.worker.attrs);
        }
        fprintf(out, "worker %s(", d->as.worker.name);
        for (j = 0; j < d->as.worker.param_count; j++) {
          if (j) fputs(", ", out);
          dump_type_ref(out, &d->as.worker.params[j].type);
          fprintf(out, " %s", d->as.worker.params[j].name);
        }
        fputs(")\n", out);
        dump_stmt(out, d->as.worker.body, 1);
        break;
      case E_TOP_FUNCTION:
        fputs("function ", out);
        dump_type_ref(out, &d->as.func.return_type);
        fprintf(out, " %s(", d->as.func.name);
        for (j = 0; j < d->as.func.param_count; j++) {
          if (j) fputs(", ", out);
          dump_type_ref(out, &d->as.func.params[j].type);
          fprintf(out, " %s", d->as.func.params[j].name);
        }
        fputs(")\n", out);
        dump_stmt(out, d->as.func.body, 1);
        break;
      case E_TOP_TYPE:
        fprintf(out, "type %s(", d->as.tdecl.name);
        for (j = 0; j < d->as.tdecl.param_count; j++) {
          if (j) fputs(", ", out);
          dump_type_ref(out, &d->as.tdecl.params[j].type);
          fprintf(out, " %s", d->as.tdecl.params[j].name);
        }
        fputs(")\n", out);
        dump_stmt(out, d->as.tdecl.body, 1);
        break;
      case E_TOP_DECLARE:
        switch (d->as.declare_decl.kind) {
          case E_DECLARE_DEFAULT_IN_WORDS:
            fprintf(out, "declare default_in_words %u\n", d->as.declare_decl.value);
            break;
          case E_DECLARE_DEFAULT_OUT_WORDS:
            fprintf(out, "declare default_out_words %u\n", d->as.declare_decl.value);
            break;
          case E_DECLARE_DEFAULT_SIGNAL_MAIL_BOX_SIZE:
            fprintf(out, "declare default_signal_mail_box_size %u\n", d->as.declare_decl.value);
            break;
        }
        break;
      case E_TOP_DYNAMIC:
        fprintf(out, "dynamic %s(", d->as.dynamic_decl.name);
        dump_type_ref(out, &d->as.dynamic_decl.element_type);
        fprintf(out, ", %u, %u, %u);\n",
                d->as.dynamic_decl.min_free,
                d->as.dynamic_decl.max_free,
                d->as.dynamic_decl.grow_by);
        break;
    }
  }
}
