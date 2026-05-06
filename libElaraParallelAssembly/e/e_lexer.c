#define _POSIX_C_SOURCE 200809L
#include "e_lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n);
  if (!q) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  return q;
}

static char *xstrndup_local(const char *s, size_t n) {
  char *p = (char*)malloc(n + 1);
  if (!p) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  memcpy(p, s, n);
  p[n] = 0;
  return p;
}

static int push_tok(ETokenVec *v, ETokenKind kind, const char *start, size_t len, int line, int col) {
  if (v->count == v->cap) {
    size_t next = v->cap ? v->cap * 2u : 64u;
    v->items = (EToken*)xrealloc(v->items, next * sizeof(EToken));
    v->cap = next;
  }
  v->items[v->count].kind = kind;
  v->items[v->count].text = xstrndup_local(start, len);
  v->items[v->count].line = line;
  v->items[v->count].col = col;
  v->count++;
  return 1;
}

void e_token_vec_free(ETokenVec *v) {
  size_t i;
  if (!v) return;
  for (i = 0; i < v->count; i++) {
    free(v->items[i].text);
  }
  free(v->items);
  v->items = NULL;
  v->count = 0;
  v->cap = 0;
}

static ETokenKind kw_kind(const char *text) {
  if (strcmp(text, "return") == 0) return E_TOK_KW_RETURN;
  if (strcmp(text, "struct") == 0) return E_TOK_KW_STRUCT;
  if (strcmp(text, "type") == 0) return E_TOK_KW_TYPE;
  if (strcmp(text, "kernel") == 0) return E_TOK_KW_KERNEL;
  if (strcmp(text, "worker") == 0) return E_TOK_KW_WORKER;
  if (strcmp(text, "function") == 0) return E_TOK_KW_FUNCTION;
  if (strcmp(text, "if") == 0) return E_TOK_KW_IF;
  if (strcmp(text, "else") == 0) return E_TOK_KW_ELSE;
  if (strcmp(text, "switch") == 0) return E_TOK_KW_SWITCH;
  if (strcmp(text, "case") == 0) return E_TOK_KW_CASE;
  if (strcmp(text, "default") == 0) return E_TOK_KW_DEFAULT;
  if (strcmp(text, "break") == 0) return E_TOK_KW_BREAK;
  return E_TOK_IDENT;
}

int e_lex_source(const char *src, ETokenVec *out_tokens, char err[256]) {
  int line = 1;
  int col = 1;
  const char *p = src;

  if (err) err[0] = 0;
  memset(out_tokens, 0, sizeof(*out_tokens));

  while (*p) {
    if (*p == ' ' || *p == '\t' || *p == '\r') {
      p++;
      col++;
      continue;
    }
    if (*p == '\n') {
      p++;
      line++;
      col = 1;
      continue;
    }
    if (p[0] == '/' && p[1] == '/') {
      p += 2;
      col += 2;
      while (*p && *p != '\n') {
        p++;
        col++;
      }
      continue;
    }

    if (isalpha((unsigned char)*p) || *p == '_') {
      const char *start = p;
      int start_col = col;
      while (isalnum((unsigned char)*p) || *p == '_') {
        p++;
        col++;
      }
      {
        char *tmp = xstrndup_local(start, (size_t)(p - start));
        ETokenKind kind = kw_kind(tmp);
        free(tmp);
        if (!push_tok(out_tokens, kind, start, (size_t)(p - start), line, start_col)) return 0;
      }
      continue;
    }

    if (isdigit((unsigned char)*p)) {
      const char *start = p;
      int start_col = col;
      while (isdigit((unsigned char)*p)) {
        p++;
        col++;
      }
      if (!push_tok(out_tokens, E_TOK_INT_LIT, start, (size_t)(p - start), line, start_col)) return 0;
      continue;
    }

    if (*p == '"') {
      const char *start = p;
      int start_col = col;
      p++;
      col++;
      while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
          p += 2;
          col += 2;
        } else {
          if (*p == '\n') {
            if (err) snprintf(err, 256, "unterminated string at %d:%d", line, start_col);
            e_token_vec_free(out_tokens);
            return 0;
          }
          p++;
          col++;
        }
      }
      if (*p != '"') {
        if (err) snprintf(err, 256, "unterminated string at %d:%d", line, start_col);
        e_token_vec_free(out_tokens);
        return 0;
      }
      p++;
      col++;
      if (!push_tok(out_tokens, E_TOK_STRING_LIT, start, (size_t)(p - start), line, start_col)) return 0;
      continue;
    }

    {
      ETokenKind kind = E_TOK_EOF;
      int start_col = col;
      switch (*p) {
        case '(': kind = E_TOK_LPAREN; break;
        case ')': kind = E_TOK_RPAREN; break;
        case '{': kind = E_TOK_LBRACE; break;
        case '}': kind = E_TOK_RBRACE; break;
        case ',': kind = E_TOK_COMMA; break;
        case ';': kind = E_TOK_SEMI; break;
        case ':': kind = E_TOK_COLON; break;
        case '.': kind = E_TOK_DOT; break;
        case '=': kind = E_TOK_ASSIGN; break;
        case '+': kind = E_TOK_PLUS; break;
        case '-': kind = E_TOK_MINUS; break;
        case '*': kind = E_TOK_STAR; break;
        case '/': kind = E_TOK_SLASH; break;
        default: break;
      }
      if (kind == E_TOK_EOF) {
        if (err) snprintf(err, 256, "unexpected character '%c' at %d:%d", *p, line, col);
        e_token_vec_free(out_tokens);
        return 0;
      }
      if (!push_tok(out_tokens, kind, p, 1u, line, start_col)) return 0;
      p++;
      col++;
    }
  }

  return push_tok(out_tokens, E_TOK_EOF, "", 0u, line, col);
}
