#pragma once

#include <stddef.h>
#include <stdio.h>

typedef struct ETypeRef {
  char *name;
  unsigned int array_len;
  char **union_names;
  size_t union_count;
} ETypeRef;

typedef struct EParam {
  ETypeRef type;
  char *name;
} EParam;

typedef struct EExpr EExpr;
typedef struct EStmt EStmt;
typedef struct ESwitchCase ESwitchCase;

typedef struct {
  int has_in_words;
  int has_out_words;
  int has_signal_mail_box_size;
  unsigned int in_words;
  unsigned int out_words;
  unsigned int signal_mail_box_size;
} EEntryAttributes;

typedef enum {
  E_EXPR_IDENT = 1,
  E_EXPR_INT,
  E_EXPR_STRING,
  E_EXPR_BINARY,
  E_EXPR_ASSIGN,
  E_EXPR_CALL,
  E_EXPR_FIELD,
  E_EXPR_INDEX,
} EExprKind;

typedef enum {
  E_BIN_ADD = 1,
  E_BIN_SUB,
  E_BIN_MUL,
  E_BIN_DIV,
  E_BIN_EQ,
  E_BIN_NE,
  E_BIN_LT,
  E_BIN_LE,
  E_BIN_GT,
  E_BIN_GE,
} EBinaryOp;

struct EExpr {
  EExprKind kind;
  union {
    char *ident;
    long long int_lit;
    char *string_lit;
    struct {
      EBinaryOp op;
      EExpr *lhs;
      EExpr *rhs;
    } binary;
    struct {
      EExpr *lhs;
      EExpr *rhs;
    } assign;
    struct {
      char *callee;
      EExpr **args;
      size_t arg_count;
    } call;
    struct {
      EExpr *base;
      char *field;
    } field;
    struct {
      EExpr *base;
      EExpr *index;
    } index;
  } as;
};

typedef enum {
  E_STMT_DECL = 1,
  E_STMT_RETURN,
  E_STMT_EXPR,
  E_STMT_BLOCK,
  E_STMT_IF,
  E_STMT_WHILE,
  E_STMT_FOR,
  E_STMT_SWITCH,
  E_STMT_BREAK,
  E_STMT_CONTINUE,
  E_STMT_NEXT,
  E_STMT_RAW_EPA,
  E_STMT_FOREACH,
  E_STMT_DYNAMIC,
  E_STMT_STATIC_BLOCK,
} EStmtKind;

typedef struct {
  ETypeRef type;
  char *name;
  int is_reg;
  int is_local;
  int is_static;
  EExpr *init;
} EDeclStmt;

typedef struct {
  char *name;
  ETypeRef element_type;
  unsigned int min_free;
  unsigned int max_free;
  unsigned int grow_by;
} EDynamicDecl;

typedef struct {
  EStmt **items;
  size_t count;
} EStmtList;

struct ESwitchCase {
  int is_default;
  long long value;
  EStmtList body;
};

struct EStmt {
  EStmtKind kind;
  union {
    EDeclStmt decl;
    EExpr *expr;
    struct {
      EExpr *value;
    } ret;
    EStmtList block;
    struct {
      EExpr *cond;
      EStmt *then_branch;
      EStmt *else_branch;
    } if_stmt;
    struct {
      EExpr *cond;
      EStmt *body;
    } while_stmt;
    struct {
      EStmt *init;
      EExpr *cond;
      EExpr *step;
      EStmt *body;
    } for_stmt;
    struct {
      EExpr *target;
      ESwitchCase *cases;
      size_t case_count;
    } switch_stmt;
    struct {
      char *worker_name;
    } next_stmt;
    struct {
      char *text;
    } raw_epa;
    struct {
      EStmt *var_decl;   /* E_STMT_DECL for loop variable (MyType t), no init */
      char *iter_name;   /* name of the Iterator variable */
      EStmt *body;
    } foreach_stmt;
    EDynamicDecl dynamic_decl;
    EStmtList static_block;
  } as;
};

typedef struct {
  ETypeRef return_type;
  char *name;
  EParam *params;
  size_t param_count;
  EStmt *body;
} EFunction;

typedef struct {
  char *name;
  EParam *params;
  size_t param_count;
  EStmt *body;
  EEntryAttributes attrs;
} EWorker;

typedef struct {
  EParam *params;
  size_t param_count;
  EStmt *body;
  EEntryAttributes attrs;
} EKernel;

typedef struct {
  char *name;
} EStructDecl;

typedef struct {
  char *name;
  EParam *params;
  size_t param_count;
  EStmt *body;
} ETypeDecl;

typedef enum {
  E_DECLARE_DEFAULT_IN_WORDS = 1,
  E_DECLARE_DEFAULT_OUT_WORDS,
  E_DECLARE_DEFAULT_SIGNAL_MAIL_BOX_SIZE,
} EDeclareKind;

typedef struct {
  EDeclareKind kind;
  unsigned int value;
} EDeclareDecl;

typedef enum {
  E_TOP_STRUCT = 1,
  E_TOP_KERNEL,
  E_TOP_WORKER,
  E_TOP_FUNCTION,
  E_TOP_TYPE,
  E_TOP_DECLARE,
  E_TOP_DYNAMIC,
} ETopKind;

typedef struct {
  ETopKind kind;
  union {
    EStructDecl sdecl;
    EKernel kernel;
    EWorker worker;
    EFunction func;
    ETypeDecl tdecl;
    EDeclareDecl declare_decl;
    EDynamicDecl dynamic_decl;
  } as;
} ETopDecl;

typedef struct {
  ETopDecl *items;
  size_t count;
} EProgram;

void e_program_free(EProgram *p);
void e_program_dump(FILE *out, const EProgram *p);
