#define _POSIX_C_SOURCE 200809L
#include "e_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const ETokenVec *tokens;
  size_t pos;
  char *err;
} Parser;

static char *xstrdup_local(const char *s) {
  size_t n = strlen(s);
  char *p = (char*)malloc(n + 1);
  if (!p) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  memcpy(p, s, n + 1);
  return p;
}

static void type_ref_add_union_name(ETypeRef *out, const char *name) {
  out->union_names = (char**)realloc(out->union_names, sizeof(char*) * (out->union_count + 1u));
  if (!out->union_names) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  out->union_names[out->union_count++] = xstrdup_local(name);
}

static EExpr *new_expr(EExprKind kind) {
  EExpr *e = (EExpr*)calloc(1, sizeof(EExpr));
  if (!e) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  e->kind = kind;
  return e;
}

static EStmt *new_stmt(EStmtKind kind) {
  EStmt *s = (EStmt*)calloc(1, sizeof(EStmt));
  if (!s) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  s->kind = kind;
  return s;
}

static const EToken *peek(const Parser *p) {
  return &p->tokens->items[p->pos];
}

static const EToken *peek_n(const Parser *p, size_t n) {
  size_t idx = p->pos + n;
  if (idx >= p->tokens->count) idx = p->tokens->count - 1;
  return &p->tokens->items[idx];
}

static int set_err(Parser *p, const char *fmt, const EToken *tok) {
  snprintf(p->err, 256, "%s at %d:%d near '%s'", fmt, tok->line, tok->col, tok->text);
  return 0;
}

static int match(Parser *p, ETokenKind kind) {
  if (peek(p)->kind != kind) return 0;
  p->pos++;
  return 1;
}

static int expect(Parser *p, ETokenKind kind, const char *what) {
  if (peek(p)->kind != kind) return set_err(p, what, peek(p));
  p->pos++;
  return 1;
}

static int parse_type(Parser *p, ETypeRef *out) {
  if (!expect(p, E_TOK_IDENT, "expected type name")) return 0;
  out->name = xstrdup_local(p->tokens->items[p->pos - 1].text);
  out->array_len = 0u;
  out->union_names = NULL;
  out->union_count = 0u;
  while (match(p, E_TOK_PIPE)) {
    if (!expect(p, E_TOK_IDENT, "expected type name after '|'")) return 0;
    type_ref_add_union_name(out, p->tokens->items[p->pos - 1].text);
  }
  if (match(p, E_TOK_LBRACKET)) {
    if (peek(p)->kind != E_TOK_INT_LIT) return set_err(p, "expected integer array length", peek(p));
    out->array_len = (unsigned int)strtoul(peek(p)->text, NULL, 10);
    p->pos++;
    if (!expect(p, E_TOK_RBRACKET, "expected ']' after array length")) return 0;
  }
  return 1;
}

static int is_decl_start(const Parser *p) {
  size_t pos = p->pos;
  if (peek_n(p, 0)->kind == E_TOK_KW_REG || peek_n(p, 0)->kind == E_TOK_KW_LOCAL) pos++;
  if (p->tokens->items[pos].kind != E_TOK_IDENT) return 0;
  pos++;
  if (p->tokens->items[pos].kind == E_TOK_LBRACKET) {
    pos++;
    if (p->tokens->items[pos].kind != E_TOK_INT_LIT) return 0;
    pos++;
    if (p->tokens->items[pos].kind != E_TOK_RBRACKET) return 0;
    pos++;
  }
  return p->tokens->items[pos].kind == E_TOK_IDENT;
}

static int apply_entry_attribute(
  Parser *p,
  EEntryAttributes *attrs,
  const EToken *name_tok,
  unsigned int value
) {
  if (strcmp(name_tok->text, "in_words") == 0) {
    if (attrs->has_in_words) return set_err(p, "duplicate in_words attribute", name_tok);
    attrs->has_in_words = 1;
    attrs->in_words = value;
    return 1;
  }
  if (strcmp(name_tok->text, "out_words") == 0) {
    if (attrs->has_out_words) return set_err(p, "duplicate out_words attribute", name_tok);
    attrs->has_out_words = 1;
    attrs->out_words = value;
    return 1;
  }
  if (strcmp(name_tok->text, "signal_mail_box_size") == 0) {
    if (attrs->has_signal_mail_box_size) return set_err(p, "duplicate signal_mail_box_size attribute", name_tok);
    attrs->has_signal_mail_box_size = 1;
    attrs->signal_mail_box_size = value;
    return 1;
  }
  return set_err(p, "unknown entry attribute", name_tok);
}

static int parse_entry_attributes(Parser *p, EEntryAttributes *attrs) {
  const EToken *name_tok;
  if (!expect(p, E_TOK_IDENT, "expected attributes after '@'")) return 0;
  name_tok = &p->tokens->items[p->pos - 1];
  if (strcmp(name_tok->text, "attributes") != 0) {
    return set_err(p, "expected attributes after '@'", name_tok);
  }

  while (peek(p)->kind == E_TOK_IDENT) {
    const EToken *attr_tok = peek(p);
    unsigned int value;
    if (!expect(p, E_TOK_IDENT, "expected attribute name")) return 0;
    if (!expect(p, E_TOK_COLON, "expected ':' after attribute name")) return 0;
    if (peek(p)->kind != E_TOK_INT_LIT) return set_err(p, "expected integer after ':'", peek(p));
    value = (unsigned int)strtoul(peek(p)->text, NULL, 10);
    p->pos++;
    if (!apply_entry_attribute(p, attrs, attr_tok, value)) return 0;
  }
  return 1;
}

static EExpr *parse_expr(Parser *p);

static EExpr *parse_primary(Parser *p) {
  const EToken *tok = peek(p);
  if (match(p, E_TOK_IDENT)) {
    EExpr *e;
    char *name = xstrdup_local(tok->text);
    if (match(p, E_TOK_LPAREN)) {
      EExpr *call = new_expr(E_EXPR_CALL);
      call->as.call.callee = name;
      while (!match(p, E_TOK_RPAREN)) {
        EExpr *arg = parse_expr(p);
        if (!arg) return NULL;
        call->as.call.args = (EExpr**)realloc(call->as.call.args, sizeof(EExpr*) * (call->as.call.arg_count + 1));
        if (!call->as.call.args) {
          fprintf(stderr, "OOM\n");
          exit(1);
        }
        call->as.call.args[call->as.call.arg_count++] = arg;
        if (match(p, E_TOK_RPAREN)) break;
        if (!expect(p, E_TOK_COMMA, "expected ',' or ')'")) return NULL;
      }
      e = call;
    } else {
      e = new_expr(E_EXPR_IDENT);
      e->as.ident = name;
    }

    while (match(p, E_TOK_DOT)) {
      EExpr *field_expr;
      if (!expect(p, E_TOK_IDENT, "expected field name after '.'")) return NULL;
      field_expr = new_expr(E_EXPR_FIELD);
      field_expr->as.field.base = e;
      field_expr->as.field.field = xstrdup_local(p->tokens->items[p->pos - 1].text);
      e = field_expr;
    }
    return e;
  }
  if (match(p, E_TOK_INT_LIT)) {
    EExpr *e = new_expr(E_EXPR_INT);
    e->as.int_lit = strtoll(tok->text, NULL, 10);
    return e;
  }
  if (match(p, E_TOK_STRING_LIT)) {
    EExpr *e = new_expr(E_EXPR_STRING);
    e->as.string_lit = xstrdup_local(tok->text);
    return e;
  }
  if (match(p, E_TOK_LPAREN)) {
    EExpr *e = parse_expr(p);
    if (!e) return NULL;
    if (!expect(p, E_TOK_RPAREN, "expected ')'")) return NULL;
    return e;
  }
  set_err(p, "expected expression", peek(p));
  return NULL;
}

static EExpr *parse_mul(Parser *p) {
  EExpr *lhs = parse_primary(p);
  while (lhs && (peek(p)->kind == E_TOK_STAR || peek(p)->kind == E_TOK_SLASH)) {
    ETokenKind op = peek(p)->kind;
    EExpr *rhs;
    EExpr *e;
    p->pos++;
    rhs = parse_primary(p);
    if (!rhs) return NULL;
    e = new_expr(E_EXPR_BINARY);
    e->as.binary.op = (op == E_TOK_STAR) ? E_BIN_MUL : E_BIN_DIV;
    e->as.binary.lhs = lhs;
    e->as.binary.rhs = rhs;
    lhs = e;
  }
  return lhs;
}

static EExpr *parse_add(Parser *p) {
  EExpr *lhs = parse_mul(p);
  while (lhs && (peek(p)->kind == E_TOK_PLUS || peek(p)->kind == E_TOK_MINUS)) {
    ETokenKind op = peek(p)->kind;
    EExpr *rhs;
    EExpr *e;
    p->pos++;
    rhs = parse_mul(p);
    if (!rhs) return NULL;
    e = new_expr(E_EXPR_BINARY);
    e->as.binary.op = (op == E_TOK_PLUS) ? E_BIN_ADD : E_BIN_SUB;
    e->as.binary.lhs = lhs;
    e->as.binary.rhs = rhs;
    lhs = e;
  }
  return lhs;
}

static EExpr *parse_eq(Parser *p) {
  EExpr *lhs = parse_add(p);
  while (lhs && peek(p)->kind == E_TOK_EQEQ) {
    EExpr *rhs;
    EExpr *e;
    p->pos++;
    rhs = parse_add(p);
    if (!rhs) return NULL;
    e = new_expr(E_EXPR_BINARY);
    e->as.binary.op = E_BIN_EQ;
    e->as.binary.lhs = lhs;
    e->as.binary.rhs = rhs;
    lhs = e;
  }
  return lhs;
}

static EExpr *parse_assign(Parser *p) {
  EExpr *lhs = parse_eq(p);
  if (!lhs) return NULL;
  if (match(p, E_TOK_ASSIGN)) {
    EExpr *rhs = parse_assign(p);
    EExpr *e;
    if (!rhs) return NULL;
    e = new_expr(E_EXPR_ASSIGN);
    e->as.assign.lhs = lhs;
    e->as.assign.rhs = rhs;
    return e;
  }
  return lhs;
}

static EExpr *parse_expr(Parser *p) {
  return parse_assign(p);
}

static int block_push(EStmt *block, EStmt *child) {
  size_t next = block->as.block.count + 1u;
  EStmt **items = (EStmt**)realloc(block->as.block.items, next * sizeof(EStmt*));
  if (!items) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  block->as.block.items = items;
  block->as.block.items[block->as.block.count++] = child;
  return 1;
}

static int stmt_list_push(EStmtList *list, EStmt *child) {
  size_t next = list->count + 1u;
  EStmt **items = (EStmt**)realloc(list->items, next * sizeof(EStmt*));
  if (!items) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  list->items = items;
  list->items[list->count++] = child;
  return 1;
}

static EStmt *parse_stmt(Parser *p);

static EStmt *parse_block(Parser *p) {
  EStmt *block = new_stmt(E_STMT_BLOCK);
  if (!expect(p, E_TOK_LBRACE, "expected '{'")) return NULL;
  while (peek(p)->kind != E_TOK_RBRACE && peek(p)->kind != E_TOK_EOF) {
    EStmt *s = parse_stmt(p);
    if (!s) return NULL;
    block_push(block, s);
  }
  if (!expect(p, E_TOK_RBRACE, "expected '}'")) return NULL;
  return block;
}

static EStmt *parse_decl_stmt(Parser *p) {
  EStmt *s = new_stmt(E_STMT_DECL);
  if (match(p, E_TOK_KW_REG)) s->as.decl.is_reg = 1;
  else if (match(p, E_TOK_KW_LOCAL)) s->as.decl.is_local = 1;
  if (!parse_type(p, &s->as.decl.type)) return NULL;
  if (!expect(p, E_TOK_IDENT, "expected local name")) return NULL;
  s->as.decl.name = xstrdup_local(p->tokens->items[p->pos - 1].text);
  if (match(p, E_TOK_ASSIGN)) {
    s->as.decl.init = parse_expr(p);
    if (!s->as.decl.init) return NULL;
  }
  if (!expect(p, E_TOK_SEMI, "expected ';'")) return NULL;
  return s;
}

static EStmt *parse_for_init_stmt(Parser *p) {
  if (peek(p)->kind == E_TOK_SEMI) {
    p->pos++;
    return NULL;
  }
  if (is_decl_start(p)) return parse_decl_stmt(p);
  {
    EStmt *s = new_stmt(E_STMT_EXPR);
    s->as.expr = parse_expr(p);
    if (!s->as.expr) return NULL;
    if (!expect(p, E_TOK_SEMI, "expected ';' after for init")) return NULL;
    return s;
  }
}

static EStmt *parse_if_stmt(Parser *p) {
  EStmt *s = new_stmt(E_STMT_IF);
  if (!expect(p, E_TOK_LPAREN, "expected '(' after if")) return NULL;
  s->as.if_stmt.cond = parse_expr(p);
  if (!s->as.if_stmt.cond) return NULL;
  if (!expect(p, E_TOK_RPAREN, "expected ')' after if condition")) return NULL;
  s->as.if_stmt.then_branch = parse_stmt(p);
  if (!s->as.if_stmt.then_branch) return NULL;
  if (match(p, E_TOK_KW_ELSE)) {
    s->as.if_stmt.else_branch = parse_stmt(p);
    if (!s->as.if_stmt.else_branch) return NULL;
  }
  return s;
}

static EStmt *parse_while_stmt(Parser *p) {
  EStmt *s = new_stmt(E_STMT_WHILE);
  if (!expect(p, E_TOK_LPAREN, "expected '(' after while")) return NULL;
  s->as.while_stmt.cond = parse_expr(p);
  if (!s->as.while_stmt.cond) return NULL;
  if (!expect(p, E_TOK_RPAREN, "expected ')' after while condition")) return NULL;
  s->as.while_stmt.body = parse_stmt(p);
  if (!s->as.while_stmt.body) return NULL;
  return s;
}

static EStmt *parse_for_stmt(Parser *p) {
  EStmt *s = new_stmt(E_STMT_FOR);
  if (!expect(p, E_TOK_LPAREN, "expected '(' after for")) return NULL;
  s->as.for_stmt.init = parse_for_init_stmt(p);
  if (peek(p)->kind != E_TOK_SEMI) {
    s->as.for_stmt.cond = parse_expr(p);
    if (!s->as.for_stmt.cond) return NULL;
  }
  if (!expect(p, E_TOK_SEMI, "expected ';' after for condition")) return NULL;
  if (peek(p)->kind != E_TOK_RPAREN) {
    s->as.for_stmt.step = parse_expr(p);
    if (!s->as.for_stmt.step) return NULL;
  }
  if (!expect(p, E_TOK_RPAREN, "expected ')' after for clauses")) return NULL;
  s->as.for_stmt.body = parse_stmt(p);
  if (!s->as.for_stmt.body) return NULL;
  return s;
}

static int switch_case_push(EStmt *s, ESwitchCase item) {
  ESwitchCase *next;
  next = (ESwitchCase*)realloc(s->as.switch_stmt.cases, sizeof(ESwitchCase) * (s->as.switch_stmt.case_count + 1u));
  if (!next) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  s->as.switch_stmt.cases = next;
  s->as.switch_stmt.cases[s->as.switch_stmt.case_count++] = item;
  return 1;
}

static EStmt *parse_switch_stmt(Parser *p) {
  EStmt *s = new_stmt(E_STMT_SWITCH);
  if (!expect(p, E_TOK_LPAREN, "expected '(' after switch")) return NULL;
  s->as.switch_stmt.target = parse_expr(p);
  if (!s->as.switch_stmt.target) return NULL;
  if (!expect(p, E_TOK_RPAREN, "expected ')' after switch target")) return NULL;
  if (!expect(p, E_TOK_LBRACE, "expected '{' after switch")) return NULL;

  while (peek(p)->kind != E_TOK_RBRACE && peek(p)->kind != E_TOK_EOF) {
    ESwitchCase item;
    memset(&item, 0, sizeof(item));

    if (match(p, E_TOK_KW_CASE)) {
      const EToken *tok = peek(p);
      if (!expect(p, E_TOK_INT_LIT, "expected integer literal after case")) return NULL;
      item.value = strtoll(tok->text, NULL, 10);
      if (!expect(p, E_TOK_COLON, "expected ':' after case value")) return NULL;
    } else if (match(p, E_TOK_KW_DEFAULT)) {
      item.is_default = 1;
      if (!expect(p, E_TOK_COLON, "expected ':' after default")) return NULL;
    } else {
      set_err(p, "expected case/default in switch", peek(p));
      return NULL;
    }

    while (peek(p)->kind != E_TOK_KW_CASE &&
           peek(p)->kind != E_TOK_KW_DEFAULT &&
           peek(p)->kind != E_TOK_RBRACE &&
           peek(p)->kind != E_TOK_EOF) {
      EStmt *body_stmt = parse_stmt(p);
      if (!body_stmt) return NULL;
      stmt_list_push(&item.body, body_stmt);
    }

    switch_case_push(s, item);
  }

  if (!expect(p, E_TOK_RBRACE, "expected '}' after switch")) return NULL;
  return s;
}

static EStmt *parse_stmt(Parser *p) {
  if (peek(p)->kind == E_TOK_LBRACE) return parse_block(p);
  if (peek(p)->kind == E_TOK_RAW_EPA) {
    EStmt *s = new_stmt(E_STMT_RAW_EPA);
    s->as.raw_epa.text = xstrdup_local(peek(p)->text);
    p->pos++;
    return s;
  }
  if (match(p, E_TOK_KW_IF)) return parse_if_stmt(p);
  if (match(p, E_TOK_KW_WHILE)) return parse_while_stmt(p);
  if (match(p, E_TOK_KW_FOR)) return parse_for_stmt(p);
  if (match(p, E_TOK_KW_SWITCH)) return parse_switch_stmt(p);
  if (match(p, E_TOK_KW_RETURN)) {
    EStmt *s = new_stmt(E_STMT_RETURN);
    if (!match(p, E_TOK_SEMI)) {
      s->as.ret.value = parse_expr(p);
      if (!s->as.ret.value) return NULL;
      if (!expect(p, E_TOK_SEMI, "expected ';'")) return NULL;
    }
    return s;
  }
  if (match(p, E_TOK_KW_BREAK)) {
    EStmt *s = new_stmt(E_STMT_BREAK);
    if (!expect(p, E_TOK_SEMI, "expected ';'")) return NULL;
    return s;
  }
  if (match(p, E_TOK_KW_CONTINUE)) {
    EStmt *s = new_stmt(E_STMT_CONTINUE);
    if (!expect(p, E_TOK_SEMI, "expected ';'")) return NULL;
    return s;
  }
  if (match(p, E_TOK_KW_NEXT)) {
    EStmt *s = new_stmt(E_STMT_NEXT);
    if (!expect(p, E_TOK_IDENT, "expected worker name after next")) return NULL;
    s->as.next_stmt.worker_name = xstrdup_local(p->tokens->items[p->pos - 1].text);
    if (!expect(p, E_TOK_SEMI, "expected ';'")) return NULL;
    return s;
  }

  if (is_decl_start(p)) {
    return parse_decl_stmt(p);
  }

  {
    EStmt *s = new_stmt(E_STMT_EXPR);
    s->as.expr = parse_expr(p);
    if (!s->as.expr) return NULL;
    if (!expect(p, E_TOK_SEMI, "expected ';'")) return NULL;
    return s;
  }
}

static int top_push(EProgram *prog, ETopDecl *d) {
  ETopDecl *items = (ETopDecl*)realloc(prog->items, sizeof(ETopDecl) * (prog->count + 1u));
  if (!items) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  prog->items = items;
  prog->items[prog->count++] = *d;
  return 1;
}

static int parse_param_list(Parser *p, EParam **out_params, size_t *out_count) {
  if (match(p, E_TOK_RPAREN)) return 1;
  for (;;) {
    EParam param;
    memset(&param, 0, sizeof(param));
    if (!parse_type(p, &param.type)) return 0;
    if (!expect(p, E_TOK_IDENT, "expected parameter name")) return 0;
    param.name = xstrdup_local(p->tokens->items[p->pos - 1].text);
    *out_params = (EParam*)realloc(*out_params, sizeof(EParam) * (*out_count + 1u));
    if (!*out_params) {
      fprintf(stderr, "OOM\n");
      exit(1);
    }
    (*out_params)[(*out_count)++] = param;
    if (match(p, E_TOK_RPAREN)) break;
    if (!expect(p, E_TOK_COMMA, "expected ',' or ')'")) return 0;
  }
  return 1;
}

static int parse_type_param_list(Parser *p, ETypeDecl *decl) {
  if (match(p, E_TOK_RPAREN)) return 1;
  for (;;) {
    EParam param;
    memset(&param, 0, sizeof(param));
    if (!parse_type(p, &param.type)) return 0;
    if (!expect(p, E_TOK_IDENT, "expected parameter name")) return 0;
    param.name = xstrdup_local(p->tokens->items[p->pos - 1].text);
    decl->params = (EParam*)realloc(decl->params, sizeof(EParam) * (decl->param_count + 1u));
    if (!decl->params) {
      fprintf(stderr, "OOM\n");
      exit(1);
    }
    decl->params[decl->param_count++] = param;
    if (match(p, E_TOK_RPAREN)) break;
    if (!expect(p, E_TOK_COMMA, "expected ',' or ')'")) return 0;
  }
  return 1;
}

int e_parse_program(const ETokenVec *tokens, EProgram *out_program, char err[256]) {
  Parser p;
  memset(&p, 0, sizeof(p));
  memset(out_program, 0, sizeof(*out_program));
  if (err) err[0] = 0;
  p.tokens = tokens;
  p.err = err;

  while (peek(&p)->kind != E_TOK_EOF) {
    ETopDecl top;
    EEntryAttributes pending_attrs;
    memset(&top, 0, sizeof(top));
    memset(&pending_attrs, 0, sizeof(pending_attrs));

    while (match(&p, E_TOK_AT)) {
      if (!parse_entry_attributes(&p, &pending_attrs)) return 0;
    }

    if (match(&p, E_TOK_KW_DECLARE)) {
      const EToken *name_tok;
      unsigned int value;
      if (pending_attrs.has_in_words || pending_attrs.has_out_words || pending_attrs.has_signal_mail_box_size) {
        return set_err(&p, "attributes must be attached to kernel or worker", peek(&p));
      }
      top.kind = E_TOP_DECLARE;
      if (!expect(&p, E_TOK_IDENT, "expected declaration name")) return 0;
      name_tok = &tokens->items[p.pos - 1];
      if (peek(&p)->kind != E_TOK_INT_LIT) return set_err(&p, "expected integer after declaration name", peek(&p));
      value = (unsigned int)strtoul(peek(&p)->text, NULL, 10);
      p.pos++;
      if (match(&p, E_TOK_SEMI)) {}

      if (strcmp(name_tok->text, "default_in_words") == 0) {
        top.as.declare_decl.kind = E_DECLARE_DEFAULT_IN_WORDS;
      } else if (strcmp(name_tok->text, "default_out_words") == 0) {
        top.as.declare_decl.kind = E_DECLARE_DEFAULT_OUT_WORDS;
      } else if (strcmp(name_tok->text, "default_signal_mail_box_size") == 0) {
        top.as.declare_decl.kind = E_DECLARE_DEFAULT_SIGNAL_MAIL_BOX_SIZE;
      } else {
        return set_err(&p, "unknown declaration name", name_tok);
      }
      top.as.declare_decl.value = value;
      top_push(out_program, &top);
      continue;
    }

    if (match(&p, E_TOK_KW_STRUCT)) {
      if (pending_attrs.has_in_words || pending_attrs.has_out_words || pending_attrs.has_signal_mail_box_size) {
        return set_err(&p, "attributes must be attached to kernel or worker", peek(&p));
      }
      top.kind = E_TOP_STRUCT;
      if (!expect(&p, E_TOK_IDENT, "expected struct name")) return 0;
      top.as.sdecl.name = xstrdup_local(tokens->items[p.pos - 1].text);
      if (!expect(&p, E_TOK_SEMI, "expected ';'")) return 0;
      top_push(out_program, &top);
      continue;
    }

    if (match(&p, E_TOK_KW_TYPE)) {
      if (pending_attrs.has_in_words || pending_attrs.has_out_words || pending_attrs.has_signal_mail_box_size) {
        return set_err(&p, "attributes must be attached to kernel or worker", peek(&p));
      }
      top.kind = E_TOP_TYPE;
      if (!expect(&p, E_TOK_IDENT, "expected type name")) return 0;
      top.as.tdecl.name = xstrdup_local(tokens->items[p.pos - 1].text);
      if (!expect(&p, E_TOK_LPAREN, "expected '('")) return 0;
      if (!parse_type_param_list(&p, &top.as.tdecl)) return 0;
      top.as.tdecl.body = parse_block(&p);
      if (!top.as.tdecl.body) return 0;
      top_push(out_program, &top);
      continue;
    }

    if (match(&p, E_TOK_KW_KERNEL)) {
      top.kind = E_TOP_KERNEL;
      top.as.kernel.attrs = pending_attrs;
      if (!expect(&p, E_TOK_LPAREN, "expected '('")) return 0;
      if (!parse_param_list(&p, &top.as.kernel.params, &top.as.kernel.param_count)) return 0;
      top.as.kernel.body = parse_block(&p);
      if (!top.as.kernel.body) return 0;
      top_push(out_program, &top);
      continue;
    }

    if (match(&p, E_TOK_KW_WORKER)) {
      top.kind = E_TOP_WORKER;
      top.as.worker.attrs = pending_attrs;
      if (!expect(&p, E_TOK_IDENT, "expected worker name")) return 0;
      top.as.worker.name = xstrdup_local(tokens->items[p.pos - 1].text);
      if (!expect(&p, E_TOK_LPAREN, "expected '('")) return 0;
      if (!parse_param_list(&p, &top.as.worker.params, &top.as.worker.param_count)) return 0;
      top.as.worker.body = parse_block(&p);
      if (!top.as.worker.body) return 0;
      top_push(out_program, &top);
      continue;
    }

    if (match(&p, E_TOK_KW_FUNCTION)) {
      if (pending_attrs.has_in_words || pending_attrs.has_out_words || pending_attrs.has_signal_mail_box_size) {
        return set_err(&p, "attributes must be attached to kernel or worker", peek(&p));
      }
      top.kind = E_TOP_FUNCTION;
      if (!parse_type(&p, &top.as.func.return_type)) return 0;
      if (!expect(&p, E_TOK_IDENT, "expected function name")) return 0;
      top.as.func.name = xstrdup_local(tokens->items[p.pos - 1].text);
      if (!expect(&p, E_TOK_LPAREN, "expected '('")) return 0;
      if (!parse_param_list(&p, &top.as.func.params, &top.as.func.param_count)) return 0;
      top.as.func.body = parse_block(&p);
      if (!top.as.func.body) return 0;
      top_push(out_program, &top);
      continue;
    }

    return set_err(&p, "expected top-level declaration", peek(&p));
  }

  return 1;
}
