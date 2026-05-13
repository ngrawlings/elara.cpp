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
  if (strcmp(text, "while") == 0) return E_TOK_KW_WHILE;
  if (strcmp(text, "for") == 0) return E_TOK_KW_FOR;
  if (strcmp(text, "switch") == 0) return E_TOK_KW_SWITCH;
  if (strcmp(text, "case") == 0) return E_TOK_KW_CASE;
  if (strcmp(text, "default") == 0) return E_TOK_KW_DEFAULT;
  if (strcmp(text, "break") == 0) return E_TOK_KW_BREAK;
  if (strcmp(text, "continue") == 0) return E_TOK_KW_CONTINUE;
  if (strcmp(text, "declare") == 0) return E_TOK_KW_DECLARE;
  if (strcmp(text, "next") == 0) return E_TOK_KW_NEXT;
  if (strcmp(text, "reg") == 0) return E_TOK_KW_REG;
  if (strcmp(text, "local") == 0) return E_TOK_KW_LOCAL;
  return E_TOK_IDENT;
}

static void advance_char(const char **p, int *line, int *col) {
  if (**p == '\n') {
    (*p)++;
    (*line)++;
    *col = 1;
    return;
  }
  (*p)++;
  (*col)++;
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

    if (strncmp(p, "EPA", 3) == 0 &&
        !isalnum((unsigned char)p[3]) &&
        p[3] != '_') {
      const char *q = p + 3;
      int qline = line;
      int qcol = col + 3;

      for (;;) {
        if (*q == ' ' || *q == '\t' || *q == '\r') {
          q++;
          qcol++;
          continue;
        }
        if (*q == '\n') {
          q++;
          qline++;
          qcol = 1;
          continue;
        }
        if (q[0] == '/' && q[1] == '/') {
          q += 2;
          qcol += 2;
          while (*q && *q != '\n') {
            q++;
            qcol++;
          }
          continue;
        }
        break;
      }

      if (*q == '{') {
        const char *raw_start;
        int depth = 1;
        p += 3;
        col += 3;
        while (p < q) {
          advance_char(&p, &line, &col);
        }
        advance_char(&p, &line, &col); /* consume '{' */
        raw_start = p;

        while (*p && depth > 0) {
          if (*p == '{') {
            depth++;
            advance_char(&p, &line, &col);
            continue;
          }
          if (*p == '}') {
            depth--;
            if (depth == 0) break;
            advance_char(&p, &line, &col);
            continue;
          }
          advance_char(&p, &line, &col);
        }

        if (depth != 0) {
          if (err) snprintf(err, 256, "unterminated EPA block at %d:%d", qline, qcol);
          e_token_vec_free(out_tokens);
          return 0;
        }

        if (!push_tok(out_tokens, E_TOK_RAW_EPA, raw_start, (size_t)(p - raw_start), qline, qcol)) return 0;
        advance_char(&p, &line, &col); /* consume '}' */
        continue;
      }
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
        case '[': kind = E_TOK_LBRACKET; break;
        case ']': kind = E_TOK_RBRACKET; break;
        case ',': kind = E_TOK_COMMA; break;
        case ';': kind = E_TOK_SEMI; break;
        case ':': kind = E_TOK_COLON; break;
        case '.': kind = E_TOK_DOT; break;
        case '=': kind = E_TOK_ASSIGN; break;
        case '+': kind = E_TOK_PLUS; break;
        case '-': kind = E_TOK_MINUS; break;
        case '*': kind = E_TOK_STAR; break;
        case '/': kind = E_TOK_SLASH; break;
        case '@': kind = E_TOK_AT; break;
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
