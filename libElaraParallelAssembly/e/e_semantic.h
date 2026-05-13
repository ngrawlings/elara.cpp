#pragma once

#include "e_ast.h"

#include <stddef.h>
#include <stdio.h>

typedef enum {
  E_PARAM_PRIMITIVE = 1,
  E_PARAM_CUSTOM_VALIDATED,
  E_PARAM_CUSTOM_UNVALIDATED,
} EParamValidationKind;

typedef enum {
  E_PARAM_ABI_VALUE = 1,
  E_PARAM_ABI_TYPE_REF,
} EParamAbiKind;

typedef struct {
  char *type_name;
  unsigned int validator_id;
  const ETypeDecl *decl;
  size_t ghs_span;
} EValidatorBinding;

typedef struct {
  const EFunction *function;
  size_t param_index;
  EParamValidationKind kind;
  EParamAbiKind abi_kind;
  unsigned int validator_id;
  size_t ghs_offset;
  size_t ghs_size;
  size_t referent_size;
  unsigned int vm_local_slot;
  unsigned int vm_local_words;
} EFunctionParamCheck;

typedef struct {
  char *name;
  char *type_name;
  size_t ghs_offset;
  size_t ghs_size;
} EGhsField;

typedef struct {
  char *type_name;
  size_t total_size;
  EGhsField *fields;
  size_t field_count;
} ETypeLayout;

typedef struct {
  char *owner_name;
  const EFunction *function;
  size_t total_size;
  size_t param_size;
  size_t stack_local_size;
  unsigned int reserved_reg_words;
  struct ELocalBinding *locals;
  size_t local_count;
} EFunctionFrame;

typedef enum {
  E_LOCAL_STACK = 1,
  E_LOCAL_REG,
  E_LOCAL_ARENA_SCOPED,
} ELocalStorageKind;

typedef struct ELocalBinding {
  const EStmt *decl_stmt;
  char *name;
  char *type_name;
  unsigned int array_len;
  size_t byte_size;
  ELocalStorageKind storage;
  size_t stack_offset;
  unsigned int reg_index;
  unsigned int reg_words;
  unsigned int vm_local_slot;
  unsigned int vm_local_words;
} ELocalBinding;

typedef struct {
  const EKernel *kernel;
  size_t kernel_count;
  unsigned int default_in_words;
  unsigned int default_out_words;
  unsigned int default_signal_mail_box_size;

  const EWorker **workers;
  size_t worker_count;

  const EFunction **functions;
  size_t function_count;

  EValidatorBinding *validators;
  size_t validator_count;

  ETypeLayout *type_layouts;
  size_t type_layout_count;

  EFunctionParamCheck *checks;
  size_t check_count;

  EFunctionFrame *frames;
  size_t frame_count;
} ESemanticModel;

int e_build_semantic_model(const EProgram *program, ESemanticModel *out_model, char err[256]);
void e_semantic_model_free(ESemanticModel *model);
void e_semantic_model_dump(FILE *out, const ESemanticModel *model);
