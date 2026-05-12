#pragma once

#include <stddef.h>

typedef enum {
  E_TOK_EOF = 0,
  E_TOK_IDENT,
  E_TOK_INT_LIT,
  E_TOK_STRING_LIT,

  E_TOK_KW_RETURN,
  E_TOK_KW_STRUCT,
  E_TOK_KW_TYPE,
  E_TOK_KW_KERNEL,
  E_TOK_KW_WORKER,
  E_TOK_KW_FUNCTION,
  E_TOK_KW_IF,
  E_TOK_KW_ELSE,
  E_TOK_KW_SWITCH,
  E_TOK_KW_CASE,
  E_TOK_KW_DEFAULT,
  E_TOK_KW_BREAK,
  E_TOK_KW_DECLARE,
  E_TOK_KW_NEXT,
  E_TOK_RAW_EPA,

  E_TOK_LPAREN,
  E_TOK_RPAREN,
  E_TOK_LBRACE,
  E_TOK_RBRACE,
  E_TOK_COMMA,
  E_TOK_SEMI,
  E_TOK_COLON,
  E_TOK_DOT,
  E_TOK_ASSIGN,
  E_TOK_PLUS,
  E_TOK_MINUS,
  E_TOK_STAR,
  E_TOK_SLASH,
  E_TOK_AT,
} ETokenKind;

typedef struct {
  ETokenKind kind;
  char *text;
  int line;
  int col;
} EToken;

typedef struct {
  EToken *items;
  size_t count;
  size_t cap;
} ETokenVec;

void e_token_vec_free(ETokenVec *v);
int e_lex_source(const char *src, ETokenVec *out_tokens, char err[256]);
