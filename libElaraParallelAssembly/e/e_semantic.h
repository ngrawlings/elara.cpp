#pragma once

#include "e_ast.h"

#include <stddef.h>
#include <stdio.h>

typedef enum {
  E_PARAM_PRIMITIVE = 1,
  E_PARAM_CUSTOM_VALIDATED,
  E_PARAM_CUSTOM_UNVALIDATED,
} EParamValidationKind;

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
  unsigned int validator_id;
  size_t ghs_offset;
  size_t ghs_size;
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
  const EFunction *function;
  size_t total_size;
} EFunctionFrame;

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
