#pragma once

#include "e_ast.h"

#include <stdint.h>
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
  unsigned int vm_local_count;
  int has_varargs;
  unsigned int vararg_off_slot;
  unsigned int vararg_count_slot;
  struct ELocalBinding *locals;
  size_t local_count;
} EFunctionFrame;

typedef enum {
  E_LOCAL_STACK = 1,
  E_LOCAL_REG,
  E_LOCAL_ARENA_SCOPED,
  E_LOCAL_STACK_STATIC,
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
  char *name;
  char *element_type_name;
  unsigned int element_array_len;
  size_t element_size;
  unsigned int min_free;
  unsigned int max_free;
  unsigned int grow_by;
  unsigned int header_word_count;
  unsigned int active_count_word;
  unsigned int free_count_word;
  unsigned int live_head_word;
  unsigned int live_tail_word;
  unsigned int free_head_word;
} EDynamicPool;

typedef struct {
  char *kernel_id;
  char *worker_name;
  unsigned int worker_index; /* 1-based index within that kernel */
} ECrossKernelWorkerEntry;

typedef struct {
  ECrossKernelWorkerEntry *entries;
  size_t count;
} ECrossKernelIndex;

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

  const EFunction **at_entries;
  size_t at_entry_count;

  EValidatorBinding *validators;
  size_t validator_count;

  ETypeLayout *type_layouts;
  size_t type_layout_count;

  EFunctionParamCheck *checks;
  size_t check_count;

  EFunctionFrame *frames;
  size_t frame_count;

  EDynamicPool *dynamic_pools;
  size_t dynamic_pool_count;

  char *kernel_declared_id;
  uint64_t kernel_declared_uid;

  ECrossKernelIndex cross_kernel; /* workers from other kernels compiled together */
} ESemanticModel;

int e_build_semantic_model(const EProgram *program, ESemanticModel *out_model, char err[256]);
void e_semantic_model_free(ESemanticModel *model);
void e_semantic_model_dump(FILE *out, const ESemanticModel *model);

/* Validate references that may be deferred until the multi-file cross-kernel
 * index has been installed on the semantic model. */
int e_validate_cross_kernel_references(const EProgram *program,
                                       const ESemanticModel *model,
                                       char err[256]);

/* Build a cross-kernel index from an array of already-built semantic models. */
int e_build_cross_kernel_index(const ESemanticModel **models, size_t model_count,
                                ECrossKernelIndex *out_index, char err[256]);
void e_cross_kernel_index_free(ECrossKernelIndex *index);

/* Lookup a worker index (1-based) by kernel_id + worker_name; returns 0 if not found. */
unsigned int e_cross_kernel_lookup(const ECrossKernelIndex *index,
                                    const char *kernel_id, const char *worker_name);
