#define _POSIX_C_SOURCE 200809L
#include "e_semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *xstrdup_local(const char *s) {
  size_t n = strlen(s);
  char *p = (char*)malloc(n + 1u);
  if (!p) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  memcpy(p, s, n + 1u);
  return p;
}

static const ETypeLayout *find_type_layout(const ESemanticModel *model, const char *name);

#define E_TYPE_REF_ABI_WORDS 2u
#define E_TYPE_REF_ABI_SIZE (E_TYPE_REF_ABI_WORDS * 4u)

static int type_ref_is_union(const ETypeRef *type) {
  return type && type->union_count > 0u;
}

static int is_primitive_type(const char *name) {
  return strcmp(name, "int") == 0 ||
         strcmp(name, "long") == 0 ||
         strcmp(name, "short") == 0 ||
         strcmp(name, "byte") == 0 ||
         strcmp(name, "char") == 0 ||
         strcmp(name, "float") == 0 ||
         strcmp(name, "double") == 0 ||
         strcmp(name, "void") == 0 ||
         strcmp(name, "z") == 0 ||
         strcmp(name, "Iterator") == 0;
}

static int streq(const char *a, const char *b) {
  return strcmp(a, b) == 0;
}

static size_t primitive_size(const char *name) {
  if (strcmp(name, "byte") == 0) return 1u;
  if (strcmp(name, "char") == 0) return 1u;
  if (strcmp(name, "short") == 0) return 2u;
  if (strcmp(name, "int") == 0) return 4u;
  if (strcmp(name, "float") == 0) return 4u;
  if (strcmp(name, "long") == 0) return 8u;
  if (strcmp(name, "double") == 0) return 8u;
  if (strcmp(name, "z") == 0) return 8u;
  if (strcmp(name, "void") == 0) return 0u;
  if (strcmp(name, "Iterator") == 0) return 4u;
  return 0u;
}

static size_t type_ref_size(const ESemanticModel *model, const ETypeRef *type, char err[256]) {
  size_t elem_size = 0u;
  size_t count = (type->array_len == 0u) ? 1u : (size_t)type->array_len;
  if (type_ref_is_union(type)) {
    snprintf(err, 256, "union type '%s|...' does not have a fixed inline size", type->name);
    return 0u;
  }
  if (is_primitive_type(type->name)) {
    elem_size = primitive_size(type->name);
  } else {
    const ETypeLayout *layout = find_type_layout(model, type->name);
    if (!layout) {
      snprintf(err, 256, "missing GHS layout for type '%s'", type->name);
      return 0u;
    }
    elem_size = layout->total_size;
  }
  return elem_size * count;
}

static unsigned int stable_validator_id(const char *name) {
  unsigned int h = 2166136261u;
  while (*name) {
    h ^= (unsigned char)*name++;
    h *= 16777619u;
  }
  if (h == 0u) h = 1u;
  return h;
}

static const EValidatorBinding *find_validator(const ESemanticModel *model, const char *name) {
  size_t i;
  for (i = 0; i < model->validator_count; i++) {
    if (strcmp(model->validators[i].type_name, name) == 0) return &model->validators[i];
  }
  return NULL;
}

static int validate_typeof_typeid_expr(const EExpr *expr, const ESemanticModel *model, char err[256]) {
  size_t i;
  if (!expr) return 1;
  switch (expr->kind) {
    case E_EXPR_IDENT:
    case E_EXPR_INT:
    case E_EXPR_STRING:
      return 1;
    case E_EXPR_FIELD:
      return validate_typeof_typeid_expr(expr->as.field.base, model, err);
    case E_EXPR_INDEX:
      return validate_typeof_typeid_expr(expr->as.index.base, model, err) &&
             validate_typeof_typeid_expr(expr->as.index.index, model, err);
    case E_EXPR_ASSIGN:
      return validate_typeof_typeid_expr(expr->as.assign.lhs, model, err) &&
             validate_typeof_typeid_expr(expr->as.assign.rhs, model, err);
    case E_EXPR_BINARY:
      return validate_typeof_typeid_expr(expr->as.binary.lhs, model, err) &&
             validate_typeof_typeid_expr(expr->as.binary.rhs, model, err);
    case E_EXPR_CALL:
      if (strcmp(expr->as.call.callee, "typeid") == 0) {
        if (expr->as.call.arg_count != 1u || expr->as.call.args[0]->kind != E_EXPR_IDENT) {
          snprintf(err, 256, "typeid expects exactly 1 declared type name");
          return 0;
        }
        if (!find_validator(model, expr->as.call.args[0]->as.ident)) {
          snprintf(err, 256, "typeid target '%s' is not a declared E type", expr->as.call.args[0]->as.ident);
          return 0;
        }
      } else if (strcmp(expr->as.call.callee, "typeof") == 0) {
        if (expr->as.call.arg_count != 1u) {
          snprintf(err, 256, "typeof expects exactly 1 argument");
          return 0;
        }
      }
      for (i = 0; i < expr->as.call.arg_count; i++) {
        if (!validate_typeof_typeid_expr(expr->as.call.args[i], model, err)) return 0;
      }
      return 1;
  }
  return 1;
}

static EValidatorBinding *find_validator_mut(ESemanticModel *model, const char *name) {
  size_t i;
  for (i = 0; i < model->validator_count; i++) {
    if (strcmp(model->validators[i].type_name, name) == 0) return &model->validators[i];
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

static int is_custom_type_with_layout(const ESemanticModel *model, const char *name) {
  if (is_primitive_type(name)) return 0;
  return find_type_layout(model, name) != NULL;
}

static int push_validator(ESemanticModel *model, const ETypeDecl *decl) {
  EValidatorBinding *next;
  next = (EValidatorBinding*)realloc(model->validators, sizeof(EValidatorBinding) * (model->validator_count + 1u));
  if (!next) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  model->validators = next;
  model->validators[model->validator_count].type_name = xstrdup_local(decl->name);
  model->validators[model->validator_count].validator_id = stable_validator_id(decl->name);
  model->validators[model->validator_count].decl = decl;
  model->validators[model->validator_count].ghs_span = 0u;
  model->validator_count++;
  return 1;
}

static int push_check(ESemanticModel *model, const EFunction *fn, size_t param_index,
                      EParamValidationKind kind, EParamAbiKind abi_kind,
                      unsigned int validator_id, size_t ghs_offset,
                      size_t ghs_size, size_t referent_size,
                      unsigned int vm_local_slot, unsigned int vm_local_words) {
  EFunctionParamCheck *next;
  next = (EFunctionParamCheck*)realloc(model->checks, sizeof(EFunctionParamCheck) * (model->check_count + 1u));
  if (!next) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  model->checks = next;
  model->checks[model->check_count].function = fn;
  model->checks[model->check_count].param_index = param_index;
  model->checks[model->check_count].kind = kind;
  model->checks[model->check_count].abi_kind = abi_kind;
  model->checks[model->check_count].validator_id = validator_id;
  model->checks[model->check_count].ghs_offset = ghs_offset;
  model->checks[model->check_count].ghs_size = ghs_size;
  model->checks[model->check_count].referent_size = referent_size;
  model->checks[model->check_count].vm_local_slot = vm_local_slot;
  model->checks[model->check_count].vm_local_words = vm_local_words;
  model->check_count++;
  return 1;
}

static int type_layout_push_field(ETypeLayout *layout, const char *name, const char *type_name,
                                  size_t ghs_offset, size_t ghs_size) {
  EGhsField *next;
  next = (EGhsField*)realloc(layout->fields, sizeof(EGhsField) * (layout->field_count + 1u));
  if (!next) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  layout->fields = next;
  layout->fields[layout->field_count].name = xstrdup_local(name);
  layout->fields[layout->field_count].type_name = xstrdup_local(type_name);
  layout->fields[layout->field_count].ghs_offset = ghs_offset;
  layout->fields[layout->field_count].ghs_size = ghs_size;
  layout->field_count++;
  return 1;
}

static int push_type_layout(ESemanticModel *model, const ETypeDecl *decl, size_t total_size) {
  ETypeLayout *next;
  next = (ETypeLayout*)realloc(model->type_layouts, sizeof(ETypeLayout) * (model->type_layout_count + 1u));
  if (!next) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  model->type_layouts = next;
  memset(&model->type_layouts[model->type_layout_count], 0, sizeof(ETypeLayout));
  model->type_layouts[model->type_layout_count].type_name = xstrdup_local(decl->name);
  model->type_layouts[model->type_layout_count].total_size = total_size;
  model->type_layout_count++;
  return 1;
}

static int push_local_binding(EFunctionFrame *frame, const EStmt *decl_stmt, const char *name,
                              const ETypeRef *type, size_t byte_size,
                              ELocalStorageKind storage, size_t stack_offset,
                              unsigned int reg_index, unsigned int reg_words) {
  ELocalBinding *next;
  next = (ELocalBinding*)realloc(frame->locals, sizeof(ELocalBinding) * (frame->local_count + 1u));
  if (!next) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  frame->locals = next;
  memset(&frame->locals[frame->local_count], 0, sizeof(ELocalBinding));
  frame->locals[frame->local_count].decl_stmt = decl_stmt;
  frame->locals[frame->local_count].name = xstrdup_local(name);
  frame->locals[frame->local_count].type_name = xstrdup_local(type->name);
  frame->locals[frame->local_count].array_len = type->array_len;
  frame->locals[frame->local_count].byte_size = byte_size;
  frame->locals[frame->local_count].storage = storage;
  frame->locals[frame->local_count].stack_offset = stack_offset;
  frame->locals[frame->local_count].reg_index = reg_index;
  frame->locals[frame->local_count].reg_words = reg_words;
  frame->locals[frame->local_count].vm_local_slot = 0u;
  frame->locals[frame->local_count].vm_local_words = 0u;
  frame->local_count++;
  return 1;
}

static int push_frame(ESemanticModel *model, const char *owner_name, const EFunction *fn,
                      size_t param_size, size_t stack_local_size,
                      unsigned int reserved_reg_words, unsigned int vm_local_count,
                      ELocalBinding *locals,
                      size_t local_count) {
  EFunctionFrame *next;
  next = (EFunctionFrame*)realloc(model->frames, sizeof(EFunctionFrame) * (model->frame_count + 1u));
  if (!next) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  model->frames = next;
  model->frames[model->frame_count].owner_name = xstrdup_local(owner_name);
  model->frames[model->frame_count].function = fn;
  model->frames[model->frame_count].param_size = param_size;
  model->frames[model->frame_count].stack_local_size = stack_local_size;
  model->frames[model->frame_count].reserved_reg_words = reserved_reg_words;
  model->frames[model->frame_count].vm_local_count = vm_local_count;
  model->frames[model->frame_count].total_size = param_size + stack_local_size;
  model->frames[model->frame_count].locals = locals;
  model->frames[model->frame_count].local_count = local_count;
  model->frame_count++;
  return 1;
}

static const EFunctionFrame *find_frame(const ESemanticModel *model, const char *owner_name) {
  size_t i;
  for (i = 0; i < model->frame_count; i++) {
    if (strcmp(model->frames[i].owner_name, owner_name) == 0) return &model->frames[i];
  }
  return NULL;
}

static const ELocalBinding *find_local_binding_by_name(const EFunctionFrame *frame, const char *name) {
  size_t i;
  if (!frame || !name) return NULL;
  for (i = 0; i < frame->local_count; i++) {
    if (strcmp(frame->locals[i].name, name) == 0) return &frame->locals[i];
  }
  return NULL;
}

static int push_worker(ESemanticModel *model, const EWorker *worker) {
  const EWorker **next;
  next = (const EWorker**)realloc(model->workers, sizeof(EWorker*) * (model->worker_count + 1u));
  if (!next) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  model->workers = next;
  model->workers[model->worker_count++] = worker;
  return 1;
}

static int push_function_decl(ESemanticModel *model, const EFunction *fn) {
  const EFunction **next;
  next = (const EFunction**)realloc(model->functions, sizeof(EFunction*) * (model->function_count + 1u));
  if (!next) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  model->functions = next;
  model->functions[model->function_count++] = fn;
  return 1;
}

static const EDynamicPool *find_dynamic_pool(const ESemanticModel *model, const char *name) {
  size_t i;
  for (i = 0; i < model->dynamic_pool_count; i++) {
    if (strcmp(model->dynamic_pools[i].name, name) == 0) return &model->dynamic_pools[i];
  }
  return NULL;
}

static int push_dynamic_pool(ESemanticModel *model, const EDynamicDecl *decl, size_t element_size) {
  EDynamicPool *next;
  next = (EDynamicPool*)realloc(model->dynamic_pools, sizeof(EDynamicPool) * (model->dynamic_pool_count + 1u));
  if (!next) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  model->dynamic_pools = next;
  memset(&model->dynamic_pools[model->dynamic_pool_count], 0, sizeof(EDynamicPool));
  model->dynamic_pools[model->dynamic_pool_count].name = xstrdup_local(decl->name);
  model->dynamic_pools[model->dynamic_pool_count].element_type_name = xstrdup_local(decl->element_type.name);
  model->dynamic_pools[model->dynamic_pool_count].element_array_len = decl->element_type.array_len;
  model->dynamic_pools[model->dynamic_pool_count].element_size = element_size;
  model->dynamic_pools[model->dynamic_pool_count].min_free = decl->min_free;
  model->dynamic_pools[model->dynamic_pool_count].max_free = decl->max_free;
  model->dynamic_pools[model->dynamic_pool_count].grow_by = decl->grow_by;
  model->dynamic_pools[model->dynamic_pool_count].header_word_count = 5u;
  model->dynamic_pools[model->dynamic_pool_count].active_count_word = 0u;
  model->dynamic_pools[model->dynamic_pool_count].free_count_word = 1u;
  model->dynamic_pools[model->dynamic_pool_count].live_head_word = 2u;
  model->dynamic_pools[model->dynamic_pool_count].live_tail_word = 3u;
  model->dynamic_pools[model->dynamic_pool_count].free_head_word = 4u;
  model->dynamic_pool_count++;
  return 1;
}

static int collect_validators(const EProgram *program, ESemanticModel *model, char err[256]) {
  size_t i;
  for (i = 0; i < program->count; i++) {
    const ETopDecl *top = &program->items[i];
    if (top->kind != E_TOP_TYPE) continue;
    if (find_validator(model, top->as.tdecl.name)) {
      snprintf(err, 256, "duplicate validator type '%s'", top->as.tdecl.name);
      return 0;
    }
    push_validator(model, &top->as.tdecl);
  }
  return 1;
}

static int collect_declares(const EProgram *program, ESemanticModel *model, char err[256]) {
  size_t i;
  int seen_default_in_words = 0;
  int seen_default_out_words = 0;
  int seen_default_signal_mail_box_size = 0;

  for (i = 0; i < program->count; i++) {
    const ETopDecl *top = &program->items[i];
    if (top->kind != E_TOP_DECLARE) continue;

    switch (top->as.declare_decl.kind) {
      case E_DECLARE_DEFAULT_IN_WORDS:
        if (seen_default_in_words) {
          snprintf(err, 256, "duplicate declare default_in_words");
          return 0;
        }
        model->default_in_words = top->as.declare_decl.value;
        seen_default_in_words = 1;
        break;
      case E_DECLARE_DEFAULT_OUT_WORDS:
        if (seen_default_out_words) {
          snprintf(err, 256, "duplicate declare default_out_words");
          return 0;
        }
        model->default_out_words = top->as.declare_decl.value;
        seen_default_out_words = 1;
        break;
      case E_DECLARE_DEFAULT_SIGNAL_MAIL_BOX_SIZE:
        if (seen_default_signal_mail_box_size) {
          snprintf(err, 256, "duplicate declare default_signal_mail_box_size");
          return 0;
        }
        model->default_signal_mail_box_size = top->as.declare_decl.value;
        seen_default_signal_mail_box_size = 1;
        break;
    }
  }

  return 1;
}

static int collect_top_level_roles(const EProgram *program, ESemanticModel *model, char err[256]) {
  size_t i;
  for (i = 0; i < program->count; i++) {
    const ETopDecl *top = &program->items[i];
    switch (top->kind) {
      case E_TOP_KERNEL:
        if (model->kernel_count != 0u) {
          snprintf(err, 256, "multiple kernel declarations are not allowed");
          return 0;
        }
        model->kernel = &top->as.kernel;
        model->kernel_count = 1u;
        if (top->as.kernel.param_count < 1u) {
          snprintf(err, 256, "kernel must declare at least one parameter");
          return 0;
        }
        if (!streq(top->as.kernel.params[0].type.name, "VM")) {
          snprintf(err, 256, "kernel first parameter must be VM");
          return 0;
        }
        break;
      case E_TOP_WORKER:
        if (top->as.worker.param_count != 1u) {
          snprintf(err, 256, "worker '%s' must take exactly one parameter", top->as.worker.name);
          return 0;
        }
        if (type_ref_is_union(&top->as.worker.params[0].type)) {
          size_t ui;
          if (top->as.worker.params[0].type.array_len != 0u) {
            snprintf(err, 256, "worker '%s' union ingress parameter cannot be an array", top->as.worker.name);
            return 0;
          }
          if (!is_custom_type_with_layout(model, top->as.worker.params[0].type.name)) {
            snprintf(err, 256, "worker '%s' ingress option '%s' must be a custom type with a GHS layout",
                     top->as.worker.name, top->as.worker.params[0].type.name);
            return 0;
          }
          for (ui = 0; ui < top->as.worker.params[0].type.union_count; ui++) {
            if (!is_custom_type_with_layout(model, top->as.worker.params[0].type.union_names[ui])) {
              snprintf(err, 256, "worker '%s' ingress option '%s' must be a custom type with a GHS layout",
                       top->as.worker.name, top->as.worker.params[0].type.union_names[ui]);
              return 0;
            }
          }
        } else if (!is_custom_type_with_layout(model, top->as.worker.params[0].type.name)) {
          snprintf(err, 256, "worker '%s' parameter must be a custom type with a GHS layout", top->as.worker.name);
          return 0;
        }
        push_worker(model, &top->as.worker);
        break;
      case E_TOP_FUNCTION:
        push_function_decl(model, &top->as.func);
        break;
      case E_TOP_STRUCT:
      case E_TOP_TYPE:
      case E_TOP_DECLARE:
      case E_TOP_DYNAMIC:
        break;
    }
  }

  if (model->kernel_count != 1u) {
    snprintf(err, 256, "program must declare exactly one kernel");
    return 0;
  }

  if (model->worker_count == 0u) {
    snprintf(err, 256, "program must declare at least one worker");
    return 0;
  }

  return 1;
}

static int collect_dynamic_pools(const EProgram *program, ESemanticModel *model, char err[256]) {
  size_t i;
  for (i = 0; i < program->count; i++) {
    const ETopDecl *top = &program->items[i];
    size_t element_size;
    if (top->kind != E_TOP_DYNAMIC) continue;
    if (find_dynamic_pool(model, top->as.dynamic_decl.name)) {
      snprintf(err, 256, "duplicate dynamic pool '%s'", top->as.dynamic_decl.name);
      return 0;
    }
    if (find_validator(model, top->as.dynamic_decl.name) || find_type_layout(model, top->as.dynamic_decl.name)) {
      snprintf(err, 256, "dynamic pool '%s' collides with an existing type name", top->as.dynamic_decl.name);
      return 0;
    }
    if (top->as.dynamic_decl.element_type.union_count != 0u) {
      snprintf(err, 256, "dynamic pool '%s' element type cannot be a union", top->as.dynamic_decl.name);
      return 0;
    }
    if (top->as.dynamic_decl.min_free > top->as.dynamic_decl.max_free) {
      snprintf(err, 256, "dynamic pool '%s' min_free cannot exceed max_free", top->as.dynamic_decl.name);
      return 0;
    }
    if (top->as.dynamic_decl.grow_by == 0u) {
      snprintf(err, 256, "dynamic pool '%s' grow_by must be greater than zero", top->as.dynamic_decl.name);
      return 0;
    }
    element_size = type_ref_size(model, &top->as.dynamic_decl.element_type, err);
    if (element_size == 0u &&
        top->as.dynamic_decl.element_type.array_len == 0u &&
        primitive_size(top->as.dynamic_decl.element_type.name) == 0u &&
        !find_type_layout(model, top->as.dynamic_decl.element_type.name)) {
      return 0;
    }
    if (element_size == 0u) {
      snprintf(err, 256, "dynamic pool '%s' element type must have non-zero inline size", top->as.dynamic_decl.name);
      return 0;
    }
    push_dynamic_pool(model, &top->as.dynamic_decl, element_size);
  }
  return 1;
}

static int collect_type_layouts(const EProgram *program, ESemanticModel *model, char err[256]) {
  size_t i;
  for (i = 0; i < program->count; i++) {
    const ETopDecl *top = &program->items[i];
    size_t j;
    size_t next_offset = 0u;
    ETypeLayout *layout;
    EValidatorBinding *binding;

    if (top->kind != E_TOP_TYPE) continue;

    for (j = 0; j < top->as.tdecl.param_count; j++) {
      size_t field_size;
      field_size = type_ref_size(model, &top->as.tdecl.params[j].type, err);
      if (field_size == 0u && top->as.tdecl.params[j].type.array_len == 0u &&
          primitive_size(top->as.tdecl.params[j].type.name) == 0u &&
          !find_type_layout(model, top->as.tdecl.params[j].type.name)) {
        return 0;
      }
      next_offset += field_size;
    }

    push_type_layout(model, &top->as.tdecl, next_offset);
    layout = &model->type_layouts[model->type_layout_count - 1u];

    next_offset = 0u;
    for (j = 0; j < top->as.tdecl.param_count; j++) {
      size_t field_size;
      field_size = type_ref_size(model, &top->as.tdecl.params[j].type, err);
      type_layout_push_field(layout,
                             top->as.tdecl.params[j].name,
                             top->as.tdecl.params[j].type.name,
                             next_offset,
                             field_size);
      next_offset += field_size;
    }

    binding = find_validator_mut(model, top->as.tdecl.name);
    if (binding) binding->ghs_span = layout->total_size;
  }
  return 1;
}

static int collect_local_decls_in_stmt(const EStmt *stmt, const ESemanticModel *model,
                                       EFunctionFrame *frame, int in_worker, char err[256]) {
  size_t i;
  if (!stmt) return 1;
  switch (stmt->kind) {
    case E_STMT_BLOCK:
      for (i = 0; i < stmt->as.block.count; i++) {
        if (!collect_local_decls_in_stmt(stmt->as.block.items[i], model, frame, in_worker, err)) return 0;
      }
      return 1;
    case E_STMT_IF:
      if (!collect_local_decls_in_stmt(stmt->as.if_stmt.then_branch, model, frame, in_worker, err)) return 0;
      if (!collect_local_decls_in_stmt(stmt->as.if_stmt.else_branch, model, frame, in_worker, err)) return 0;
      return 1;
    case E_STMT_WHILE:
      return collect_local_decls_in_stmt(stmt->as.while_stmt.body, model, frame, in_worker, err);
    case E_STMT_FOR:
      if (!collect_local_decls_in_stmt(stmt->as.for_stmt.init, model, frame, in_worker, err)) return 0;
      return collect_local_decls_in_stmt(stmt->as.for_stmt.body, model, frame, in_worker, err);
    case E_STMT_FOREACH:
      if (!collect_local_decls_in_stmt(stmt->as.foreach_stmt.var_decl, model, frame, in_worker, err)) return 0;
      return collect_local_decls_in_stmt(stmt->as.foreach_stmt.body, model, frame, in_worker, err);
    case E_STMT_SWITCH:
      for (i = 0; i < stmt->as.switch_stmt.case_count; i++) {
        size_t j;
        for (j = 0; j < stmt->as.switch_stmt.cases[i].body.count; j++) {
          if (!collect_local_decls_in_stmt(stmt->as.switch_stmt.cases[i].body.items[j], model, frame, in_worker, err)) return 0;
        }
      }
      return 1;
    case E_STMT_DECL: {
      if (type_ref_is_union(&stmt->as.decl.type)) {
        snprintf(err, 256, "decl '%s' cannot use union type '%s|...' outside a worker ingress parameter",
                 stmt->as.decl.name, stmt->as.decl.type.name);
        return 0;
      }
      size_t byte_size = type_ref_size(model, &stmt->as.decl.type, err);
      if (byte_size == 0u && stmt->as.decl.type.array_len == 0u && primitive_size(stmt->as.decl.type.name) == 0u &&
          !find_type_layout(model, stmt->as.decl.type.name)) {
        return 0;
      }
      if (stmt->as.decl.is_reg) {
        unsigned int reg_words;
        if (stmt->as.decl.is_local || stmt->as.decl.is_static) {
          snprintf(err, 256, "decl '%s' cannot combine reg with local or static", stmt->as.decl.name);
          return 0;
        }
        if (stmt->as.decl.type.array_len != 0u) {
          snprintf(err, 256, "reg local '%s' cannot be an array", stmt->as.decl.name);
          return 0;
        }
        if (strcmp(stmt->as.decl.type.name, "int") == 0) {
          reg_words = 1u;
        } else if (strcmp(stmt->as.decl.type.name, "long") == 0) {
          reg_words = 2u;
        } else {
          snprintf(err, 256, "reg local '%s' must be int or long", stmt->as.decl.name);
          return 0;
        }
        if (frame->reserved_reg_words + reg_words > 4u) {
          snprintf(err, 256, "reg local '%s' exceeds 4 available register words", stmt->as.decl.name);
          return 0;
        }
        push_local_binding(frame, stmt, stmt->as.decl.name, &stmt->as.decl.type, byte_size,
                           E_LOCAL_REG, 0u, frame->reserved_reg_words, reg_words);
        frame->reserved_reg_words += reg_words;
      } else if (stmt->as.decl.is_static) {
        if (!in_worker) {
          snprintf(err, 256, "static decl '%s' is only allowed inside a worker", stmt->as.decl.name);
          return 0;
        }
        if (stmt->as.decl.is_local) {
          snprintf(err, 256, "decl '%s' cannot be both static and local", stmt->as.decl.name);
          return 0;
        }
        if (stmt->as.decl.type.array_len != 0u) {
          snprintf(err, 256, "static decl '%s' cannot be an array", stmt->as.decl.name);
          return 0;
        }
        if (stmt->as.decl.init) {
          snprintf(err, 256, "static decl '%s' cannot have an initializer; statics are zero-initialized", stmt->as.decl.name);
          return 0;
        }
        push_local_binding(frame, stmt, stmt->as.decl.name, &stmt->as.decl.type, byte_size,
                           E_LOCAL_STACK_STATIC, 0u, 0u, 0u);
        if (frame->local_count > 0u) {
          ELocalBinding *binding = &frame->locals[frame->local_count - 1u];
          binding->vm_local_slot = frame->vm_local_count;
          if (is_primitive_type(stmt->as.decl.type.name)) {
            if (byte_size == 0u || byte_size > 4u) {
              snprintf(err, 256, "static decl '%s' primitive type must fit in one word", stmt->as.decl.name);
              return 0;
            }
            binding->vm_local_words = 1u;
            frame->vm_local_count += 1u;
          } else if (find_type_layout(model, stmt->as.decl.type.name)) {
            /* Declared type: L_ALLOC from local memory once before the loop, persist the reference. */
            binding->vm_local_words = E_TYPE_REF_ABI_WORDS;
            frame->vm_local_count += E_TYPE_REF_ABI_WORDS;
          } else {
            snprintf(err, 256, "static decl '%s' must be a primitive or a declared type with a layout", stmt->as.decl.name);
            return 0;
          }
        }
      } else if (stmt->as.decl.is_local) {
        if (strcmp(stmt->as.decl.type.name, "byte") == 0) {
          if (stmt->as.decl.type.array_len == 0u) {
            snprintf(err, 256, "local decl '%s' using byte must be byte[N]", stmt->as.decl.name);
            return 0;
          }
        } else if (!find_type_layout(model, stmt->as.decl.type.name)) {
          snprintf(err, 256, "local decl '%s' must be byte[N] or a declared type", stmt->as.decl.name);
          return 0;
        }
        push_local_binding(frame, stmt, stmt->as.decl.name, &stmt->as.decl.type, byte_size,
                           E_LOCAL_ARENA_SCOPED, 0u, 0u, 0u);
        if (frame->local_count > 0u) {
          ELocalBinding *binding = &frame->locals[frame->local_count - 1u];
          binding->vm_local_slot = frame->vm_local_count;
          binding->vm_local_words = E_TYPE_REF_ABI_WORDS;
          frame->vm_local_count += E_TYPE_REF_ABI_WORDS;
        }
      } else {
        push_local_binding(frame, stmt, stmt->as.decl.name, &stmt->as.decl.type, byte_size,
                           E_LOCAL_STACK, frame->stack_local_size, 0u, 0u);
        if (frame->local_count > 0u) {
          ELocalBinding *binding = &frame->locals[frame->local_count - 1u];
          if (stmt->as.decl.type.array_len == 0u && !is_primitive_type(stmt->as.decl.type.name)) {
            binding->vm_local_slot = frame->vm_local_count;
            binding->vm_local_words = E_TYPE_REF_ABI_WORDS;
            frame->vm_local_count += E_TYPE_REF_ABI_WORDS;
          } else if (stmt->as.decl.type.array_len == 0u && byte_size > 0u && byte_size <= 4u) {
            binding->vm_local_slot = frame->vm_local_count;
            binding->vm_local_words = 1u;
            frame->vm_local_count += 1u;
          }
        }
        frame->stack_local_size += byte_size;
      }
      return 1;
    }
    case E_STMT_RETURN:
    case E_STMT_EXPR:
    case E_STMT_BREAK:
    case E_STMT_CONTINUE:
    case E_STMT_NEXT:
    case E_STMT_RAW_EPA:
      return 1;
  }
  return 1;
}

static int collect_function_checks(const EProgram *program, ESemanticModel *model, char err[256]) {
  size_t i;
  for (i = 0; i < program->count; i++) {
    const ETopDecl *top = &program->items[i];
    size_t j;
    size_t next_offset = 0u;
    unsigned int next_vm_local_slot = 0u;
    EFunctionFrame frame;
    if (top->kind != E_TOP_FUNCTION) continue;
    memset(&frame, 0, sizeof(frame));
    for (j = 0; j < top->as.func.param_count; j++) {
      const EParam *param = &top->as.func.params[j];
      if (type_ref_is_union(&param->type)) {
        snprintf(err, 256, "function '%s' parameter '%s' cannot use a union ingress type",
                 top->as.func.name, param->name);
        return 0;
      }
      size_t field_size = type_ref_size(model, &param->type, err);
      size_t abi_size = field_size;
      unsigned int vm_local_slot = 0u;
      unsigned int vm_local_words = 0u;

      if (field_size == 0u && param->type.array_len == 0u && primitive_size(param->type.name) == 0u &&
          !find_type_layout(model, param->type.name)) {
        return 0;
      }

      if (is_primitive_type(param->type.name) && param->type.array_len == 0u) {
        if (field_size > 0u && field_size <= 4u) {
          vm_local_slot = next_vm_local_slot;
          vm_local_words = 1u;
          next_vm_local_slot += 1u;
        }
        push_check(model, &top->as.func, j, E_PARAM_PRIMITIVE, E_PARAM_ABI_VALUE,
                   0u, next_offset, abi_size, field_size,
                   vm_local_slot, vm_local_words);
      } else {
        const EValidatorBinding *binding = find_validator(model, param->type.name);
        if (!is_primitive_type(param->type.name)) {
          abi_size = E_TYPE_REF_ABI_SIZE;
        }
        if (binding) {
          push_check(model, &top->as.func, j, E_PARAM_CUSTOM_VALIDATED,
                     !is_primitive_type(param->type.name) ? E_PARAM_ABI_TYPE_REF : E_PARAM_ABI_VALUE,
                     binding->validator_id, next_offset, abi_size, field_size,
                     vm_local_slot, vm_local_words);
        } else {
          push_check(model, &top->as.func, j, E_PARAM_CUSTOM_UNVALIDATED,
                     !is_primitive_type(param->type.name) ? E_PARAM_ABI_TYPE_REF : E_PARAM_ABI_VALUE,
                     0u, next_offset, abi_size, field_size,
                     vm_local_slot, vm_local_words);
        }
      }
      next_offset += abi_size;
    }
    frame.param_size = next_offset;
    frame.vm_local_count = next_vm_local_slot;
    if (!collect_local_decls_in_stmt(top->as.func.body, model, &frame, 0, err)) {
      return 0;
    }
    push_frame(model, top->as.func.name, &top->as.func, frame.param_size, frame.stack_local_size,
               frame.reserved_reg_words, frame.vm_local_count, frame.locals, frame.local_count);
  }
  return 1;
}

static int worker_exists(const ESemanticModel *model, const char *name) {
  size_t i;
  for (i = 0; i < model->worker_count; i++) {
    if (strcmp(model->workers[i]->name, name) == 0) return 1;
  }
  return 0;
}

static int validate_next_stmt_in_stmt(
  const EStmt *stmt,
  const ESemanticModel *model,
  const char *current_worker,
  char err[256]
) {
  size_t i;
  if (!stmt) return 1;
  switch (stmt->kind) {
    case E_STMT_BLOCK:
      for (i = 0; i < stmt->as.block.count; i++) {
        if (!validate_next_stmt_in_stmt(stmt->as.block.items[i], model, current_worker, err)) return 0;
      }
      return 1;
    case E_STMT_IF:
      if (!validate_next_stmt_in_stmt(stmt->as.if_stmt.then_branch, model, current_worker, err)) return 0;
      if (!validate_next_stmt_in_stmt(stmt->as.if_stmt.else_branch, model, current_worker, err)) return 0;
      return 1;
    case E_STMT_WHILE:
      return validate_next_stmt_in_stmt(stmt->as.while_stmt.body, model, current_worker, err);
    case E_STMT_FOR:
      if (!validate_next_stmt_in_stmt(stmt->as.for_stmt.init, model, current_worker, err)) return 0;
      return validate_next_stmt_in_stmt(stmt->as.for_stmt.body, model, current_worker, err);
    case E_STMT_FOREACH:
      return validate_next_stmt_in_stmt(stmt->as.foreach_stmt.body, model, current_worker, err);
    case E_STMT_SWITCH:
      for (i = 0; i < stmt->as.switch_stmt.case_count; i++) {
        size_t j;
        for (j = 0; j < stmt->as.switch_stmt.cases[i].body.count; j++) {
          if (!validate_next_stmt_in_stmt(stmt->as.switch_stmt.cases[i].body.items[j], model, current_worker, err)) return 0;
        }
      }
      return 1;
    case E_STMT_NEXT:
      if (!worker_exists(model, stmt->as.next_stmt.worker_name)) {
        snprintf(err, 256, "worker '%s' routes to unknown worker '%s'", current_worker, stmt->as.next_stmt.worker_name);
        return 0;
      }
      return 1;
    case E_STMT_DECL:
    case E_STMT_RETURN:
    case E_STMT_EXPR:
    case E_STMT_BREAK:
    case E_STMT_CONTINUE:
    case E_STMT_RAW_EPA:
      return 1;
  }
  return 1;
}

static int validate_loop_control_in_stmt(const EStmt *stmt, int loop_depth, int switch_depth, char err[256]) {
  size_t i;
  if (!stmt) return 1;
  switch (stmt->kind) {
    case E_STMT_BLOCK:
      for (i = 0; i < stmt->as.block.count; i++) {
        if (!validate_loop_control_in_stmt(stmt->as.block.items[i], loop_depth, switch_depth, err)) return 0;
      }
      return 1;
    case E_STMT_IF:
      if (!validate_loop_control_in_stmt(stmt->as.if_stmt.then_branch, loop_depth, switch_depth, err)) return 0;
      if (!validate_loop_control_in_stmt(stmt->as.if_stmt.else_branch, loop_depth, switch_depth, err)) return 0;
      return 1;
    case E_STMT_WHILE:
      return validate_loop_control_in_stmt(stmt->as.while_stmt.body, loop_depth + 1, switch_depth, err);
    case E_STMT_FOR:
      if (!validate_loop_control_in_stmt(stmt->as.for_stmt.init, loop_depth, switch_depth, err)) return 0;
      return validate_loop_control_in_stmt(stmt->as.for_stmt.body, loop_depth + 1, switch_depth, err);
    case E_STMT_FOREACH:
      return validate_loop_control_in_stmt(stmt->as.foreach_stmt.body, loop_depth + 1, switch_depth, err);
    case E_STMT_SWITCH:
      for (i = 0; i < stmt->as.switch_stmt.case_count; i++) {
        size_t j;
        for (j = 0; j < stmt->as.switch_stmt.cases[i].body.count; j++) {
          if (!validate_loop_control_in_stmt(stmt->as.switch_stmt.cases[i].body.items[j], loop_depth, switch_depth + 1, err)) return 0;
        }
      }
      return 1;
    case E_STMT_BREAK:
      if (loop_depth == 0 && switch_depth == 0) {
        snprintf(err, 256, "break used outside loop or switch");
        return 0;
      }
      return 1;
    case E_STMT_CONTINUE:
      if (loop_depth == 0) {
        snprintf(err, 256, "continue used outside loop");
        return 0;
      }
      return 1;
    case E_STMT_DECL:
    case E_STMT_RETURN:
    case E_STMT_EXPR:
    case E_STMT_NEXT:
    case E_STMT_RAW_EPA:
      return 1;
  }
  return 1;
}

static int validate_loop_control_usage(const EProgram *program, char err[256]) {
  size_t i;
  for (i = 0; i < program->count; i++) {
    const ETopDecl *top = &program->items[i];
    const EStmt *body = NULL;
    switch (top->kind) {
      case E_TOP_KERNEL: body = top->as.kernel.body; break;
      case E_TOP_WORKER: body = top->as.worker.body; break;
      case E_TOP_FUNCTION: body = top->as.func.body; break;
      case E_TOP_TYPE: body = top->as.tdecl.body; break;
      case E_TOP_STRUCT:
      case E_TOP_DECLARE:
      case E_TOP_DYNAMIC:
        break;
    }
    if (body && !validate_loop_control_in_stmt(body, 0, 0, err)) return 0;
  }
  return 1;
}

static int validate_worker_next_targets(const ESemanticModel *model, char err[256]) {
  size_t i;
  for (i = 0; i < model->worker_count; i++) {
    const EWorker *worker = model->workers[i];
    if (!validate_next_stmt_in_stmt(worker->body, model, worker->name, err)) {
      return 0;
    }
  }
  return 1;
}

static const char *worker_payload_type_by_entry_id(const ESemanticModel *model, unsigned int wid) {
  if (wid == 0u || (size_t)wid > model->worker_count) return NULL;
  return model->workers[wid - 1u]->params[0].type.name;
}

static const char *shared_worker_payload_type(const ESemanticModel *model) {
  size_t i;
  const char *type_name;
  if (model->worker_count == 0u) return NULL;
  type_name = model->workers[0]->params[0].type.name;
  for (i = 1; i < model->worker_count; i++) {
    if (strcmp(model->workers[i]->params[0].type.name, type_name) != 0) return NULL;
  }
  return type_name;
}

static const char *find_custom_param_type_by_name(const EFunction *function, const char *name) {
  size_t i;
  if (!function || !name) return NULL;
  for (i = 0; i < function->param_count; i++) {
    if (strcmp(function->params[i].name, name) != 0) continue;
    if (!is_primitive_type(function->params[i].type.name)) return function->params[i].type.name;
    return NULL;
  }
  return NULL;
}

static const char *find_expected_custom_lvalue_type(const EExpr *lhs,
                                                    const ESemanticModel *model,
                                                    const EFunctionFrame *frame,
                                                    const EFunction *function) {
  const ELocalBinding *local;
  const char *param_type;
  const EDynamicPool *pool;
  if (!lhs) return NULL;
  if (lhs->kind == E_EXPR_IDENT) {
    local = find_local_binding_by_name(frame, lhs->as.ident);
    if (local && !is_primitive_type(local->type_name)) return local->type_name;
    param_type = find_custom_param_type_by_name(function, lhs->as.ident);
    if (param_type) return param_type;
  }
  if (lhs->kind == E_EXPR_INDEX &&
      lhs->as.index.base &&
      lhs->as.index.base->kind == E_EXPR_IDENT) {
    pool = find_dynamic_pool(model, lhs->as.index.base->as.ident);
    if (pool && (pool->element_array_len != 0u || !is_primitive_type(pool->element_type_name))) {
      return pool->element_type_name;
    }
  }
  return NULL;
}

static int is_kernel_get_ghs_call(const EExpr *expr) {
  if (!expr || expr->kind != E_EXPR_CALL) return 0;
  return strcmp(expr->as.call.callee, "kernal_get_ghs") == 0 ||
         strcmp(expr->as.call.callee, "kernel_get_ghs") == 0;
}

static const char *expr_source_name(const EExpr *expr) {
  if (!expr) return "expression";
  if (expr->kind == E_EXPR_CALL) return expr->as.call.callee;
  if (expr->kind == E_EXPR_IDENT) return expr->as.ident;
  return "expression";
}

static const char *infer_custom_expr_type(const EExpr *expr,
                                          const ESemanticModel *model,
                                          const EFunctionFrame *frame,
                                          const EFunction *function,
                                          const EWorker *worker,
                                          int in_kernel,
                                          char err[256]) {
  const ELocalBinding *local;
  const char *shared_type;
  if (!expr) return NULL;
  switch (expr->kind) {
    case E_EXPR_IDENT:
      local = find_local_binding_by_name(frame, expr->as.ident);
      if (local && !is_primitive_type(local->type_name)) return local->type_name;
      if (worker && worker->param_count == 1u && strcmp(worker->params[0].name, expr->as.ident) == 0) {
        return worker->params[0].type.name;
      }
      return find_custom_param_type_by_name(function, expr->as.ident);
    case E_EXPR_INDEX:
      if (expr->as.index.base &&
          expr->as.index.base->kind == E_EXPR_IDENT) {
        const EDynamicPool *pool = find_dynamic_pool(model, expr->as.index.base->as.ident);
        if (pool && (pool->element_array_len != 0u || !is_primitive_type(pool->element_type_name))) {
          return pool->element_type_name;
        }
      }
      return NULL;
    case E_EXPR_CALL:
      if (!is_kernel_get_ghs_call(expr)) return NULL;
      if (!in_kernel) {
        snprintf(err, 256, "%s only valid in kernel", expr->as.call.callee);
        return (const char*)-1;
      }
      if (expr->as.call.arg_count != 1u) {
        snprintf(err, 256, "%s expects exactly 1 arg", expr->as.call.callee);
        return (const char*)-1;
      }
      if (expr->as.call.args[0]->kind == E_EXPR_INT) {
        const char *worker_type = worker_payload_type_by_entry_id(model, (unsigned int)expr->as.call.args[0]->as.int_lit);
        if (!worker_type) {
          snprintf(err, 256, "%s wid literal %lld does not map to a worker entry id",
                   expr->as.call.callee, expr->as.call.args[0]->as.int_lit);
          return (const char*)-1;
        }
        return worker_type;
      }
      shared_type = shared_worker_payload_type(model);
      if (!shared_type) {
        snprintf(err, 256, "%s with non-constant wid requires all workers to share the same declared payload type",
                 expr->as.call.callee);
        return (const char*)-1;
      }
      return shared_type;
    case E_EXPR_FIELD:
    case E_EXPR_STRING:
    case E_EXPR_INT:
    case E_EXPR_BINARY:
    case E_EXPR_ASSIGN:
      return NULL;
  }
  return NULL;
}

static int validate_kernel_get_ghs_usage_in_stmt(const EStmt *stmt,
                                                 const ESemanticModel *model,
                                                 const EFunctionFrame *frame,
                                                 const EFunction *function,
                                                 const EWorker *worker,
                                                 int in_kernel,
                                                 char err[256]) {
  size_t i;
  if (!stmt) return 1;
  switch (stmt->kind) {
    case E_STMT_BLOCK:
      for (i = 0; i < stmt->as.block.count; i++) {
        if (!validate_kernel_get_ghs_usage_in_stmt(stmt->as.block.items[i], model, frame, function, worker, in_kernel, err)) return 0;
      }
      return 1;
    case E_STMT_IF:
      if (!validate_kernel_get_ghs_usage_in_stmt(stmt->as.if_stmt.then_branch, model, frame, function, worker, in_kernel, err)) return 0;
      if (!validate_kernel_get_ghs_usage_in_stmt(stmt->as.if_stmt.else_branch, model, frame, function, worker, in_kernel, err)) return 0;
      return 1;
    case E_STMT_WHILE:
      return validate_kernel_get_ghs_usage_in_stmt(stmt->as.while_stmt.body, model, frame, function, worker, in_kernel, err);
    case E_STMT_FOR:
      if (!validate_kernel_get_ghs_usage_in_stmt(stmt->as.for_stmt.init, model, frame, function, worker, in_kernel, err)) return 0;
      return validate_kernel_get_ghs_usage_in_stmt(stmt->as.for_stmt.body, model, frame, function, worker, in_kernel, err);
    case E_STMT_FOREACH:
      return validate_kernel_get_ghs_usage_in_stmt(stmt->as.foreach_stmt.body, model, frame, function, worker, in_kernel, err);
    case E_STMT_SWITCH:
      for (i = 0; i < stmt->as.switch_stmt.case_count; i++) {
        size_t j;
        for (j = 0; j < stmt->as.switch_stmt.cases[i].body.count; j++) {
          if (!validate_kernel_get_ghs_usage_in_stmt(stmt->as.switch_stmt.cases[i].body.items[j], model, frame, function, worker, in_kernel, err)) return 0;
        }
      }
      return 1;
    case E_STMT_DECL:
      if (stmt->as.decl.init) {
        const char *rhs_type = infer_custom_expr_type(stmt->as.decl.init, model, frame, function, worker, in_kernel, err);
        if (rhs_type == (const char*)-1) return 0;
        if (rhs_type) {
          if (is_primitive_type(stmt->as.decl.type.name)) {
            snprintf(err, 256, "decl '%s' cannot initialize primitive type '%s' from %s",
                     stmt->as.decl.name, stmt->as.decl.type.name, expr_source_name(stmt->as.decl.init));
            return 0;
          }
          if (strcmp(rhs_type, stmt->as.decl.type.name) != 0) {
            snprintf(err, 256, "decl '%s' type '%s' does not match %s payload type '%s'",
                     stmt->as.decl.name, stmt->as.decl.type.name,
                     expr_source_name(stmt->as.decl.init), rhs_type);
            return 0;
          }
        }
      }
      return 1;
    case E_STMT_EXPR:
      if (stmt->as.expr && stmt->as.expr->kind == E_EXPR_ASSIGN) {
        const char *lhs_type = find_expected_custom_lvalue_type(stmt->as.expr->as.assign.lhs, model, frame, function);
        const char *rhs_type = infer_custom_expr_type(stmt->as.expr->as.assign.rhs, model, frame, function, worker, in_kernel, err);
        if (rhs_type == (const char*)-1) return 0;
        if (rhs_type) {
          if (!lhs_type) {
            snprintf(err, 256, "%s result must be assigned to a declared custom type",
                     expr_source_name(stmt->as.expr->as.assign.rhs));
            return 0;
          }
          if (strcmp(lhs_type, rhs_type) != 0) {
            snprintf(err, 256, "assignment target type '%s' does not match %s payload type '%s'",
                     lhs_type, expr_source_name(stmt->as.expr->as.assign.rhs), rhs_type);
            return 0;
          }
        }
      } else if (is_kernel_get_ghs_call(stmt->as.expr)) {
        snprintf(err, 256, "%s result must be assigned to a declared custom type", stmt->as.expr->as.call.callee);
        return 0;
      }
      return 1;
    case E_STMT_RETURN:
      if (function && stmt->as.ret.value) {
        const char *rhs_type = infer_custom_expr_type(stmt->as.ret.value, model, frame, function, worker, in_kernel, err);
        if (rhs_type == (const char*)-1) return 0;
        if (rhs_type) {
          if (is_primitive_type(function->return_type.name)) {
            snprintf(err, 256, "function '%s' cannot return primitive type '%s' from %s",
                     function->name, function->return_type.name, expr_source_name(stmt->as.ret.value));
            return 0;
          }
          if (strcmp(rhs_type, function->return_type.name) != 0) {
            snprintf(err, 256, "function '%s' return type '%s' does not match %s payload type '%s'",
                     function->name, function->return_type.name, expr_source_name(stmt->as.ret.value), rhs_type);
            return 0;
          }
        }
      }
      return 1;
    case E_STMT_BREAK:
    case E_STMT_CONTINUE:
    case E_STMT_NEXT:
    case E_STMT_RAW_EPA:
      return 1;
  }
  return 1;
}

static int validate_typeof_typeid_usage_in_stmt(const EStmt *stmt,
                                                const ESemanticModel *model,
                                                char err[256]) {
  size_t i;
  if (!stmt) return 1;
  switch (stmt->kind) {
    case E_STMT_DECL:
      return validate_typeof_typeid_expr(stmt->as.decl.init, model, err);
    case E_STMT_RETURN:
      return validate_typeof_typeid_expr(stmt->as.ret.value, model, err);
    case E_STMT_EXPR:
      return validate_typeof_typeid_expr(stmt->as.expr, model, err);
    case E_STMT_BLOCK:
      for (i = 0; i < stmt->as.block.count; i++) {
        if (!validate_typeof_typeid_usage_in_stmt(stmt->as.block.items[i], model, err)) return 0;
      }
      return 1;
    case E_STMT_IF:
      return validate_typeof_typeid_expr(stmt->as.if_stmt.cond, model, err) &&
             validate_typeof_typeid_usage_in_stmt(stmt->as.if_stmt.then_branch, model, err) &&
             validate_typeof_typeid_usage_in_stmt(stmt->as.if_stmt.else_branch, model, err);
    case E_STMT_WHILE:
      return validate_typeof_typeid_expr(stmt->as.while_stmt.cond, model, err) &&
             validate_typeof_typeid_usage_in_stmt(stmt->as.while_stmt.body, model, err);
    case E_STMT_FOR:
      return validate_typeof_typeid_usage_in_stmt(stmt->as.for_stmt.init, model, err) &&
             validate_typeof_typeid_expr(stmt->as.for_stmt.cond, model, err) &&
             validate_typeof_typeid_expr(stmt->as.for_stmt.step, model, err) &&
             validate_typeof_typeid_usage_in_stmt(stmt->as.for_stmt.body, model, err);
    case E_STMT_FOREACH:
      return validate_typeof_typeid_usage_in_stmt(stmt->as.foreach_stmt.body, model, err);
    case E_STMT_SWITCH:
      if (!validate_typeof_typeid_expr(stmt->as.switch_stmt.target, model, err)) return 0;
      for (i = 0; i < stmt->as.switch_stmt.case_count; i++) {
        size_t j;
        for (j = 0; j < stmt->as.switch_stmt.cases[i].body.count; j++) {
          if (!validate_typeof_typeid_usage_in_stmt(stmt->as.switch_stmt.cases[i].body.items[j], model, err)) return 0;
        }
      }
      return 1;
    case E_STMT_BREAK:
    case E_STMT_CONTINUE:
    case E_STMT_NEXT:
    case E_STMT_RAW_EPA:
      return 1;
  }
  return 1;
}

static int validate_typeof_typeid_usage(const EProgram *program, const ESemanticModel *model, char err[256]) {
  size_t i;
  for (i = 0; i < program->count; i++) {
    const ETopDecl *top = &program->items[i];
    const EStmt *body = NULL;
    switch (top->kind) {
      case E_TOP_KERNEL: body = top->as.kernel.body; break;
      case E_TOP_WORKER: body = top->as.worker.body; break;
      case E_TOP_FUNCTION: body = top->as.func.body; break;
      case E_TOP_TYPE: body = top->as.tdecl.body; break;
      case E_TOP_STRUCT:
      case E_TOP_DECLARE:
      case E_TOP_DYNAMIC:
        break;
    }
    if (body && !validate_typeof_typeid_usage_in_stmt(body, model, err)) return 0;
  }
  return 1;
}

static int validate_kernel_get_ghs_usage(const EProgram *program, const ESemanticModel *model, char err[256]) {
  size_t i;
  for (i = 0; i < program->count; i++) {
    const ETopDecl *top = &program->items[i];
    const EStmt *body = NULL;
    const EFunctionFrame *frame = NULL;
    const EFunction *function = NULL;
    const EWorker *worker = NULL;
    int in_kernel = 0;

    switch (top->kind) {
      case E_TOP_KERNEL:
        body = top->as.kernel.body;
        frame = find_frame(model, "kernel");
        in_kernel = 1;
        break;
      case E_TOP_WORKER:
        body = top->as.worker.body;
        frame = find_frame(model, top->as.worker.name);
        worker = &top->as.worker;
        break;
      case E_TOP_FUNCTION:
        body = top->as.func.body;
        frame = find_frame(model, top->as.func.name);
        function = &top->as.func;
        break;
      case E_TOP_TYPE:
        body = top->as.tdecl.body;
        frame = find_frame(model, top->as.tdecl.name);
        break;
      case E_TOP_STRUCT:
      case E_TOP_DECLARE:
      case E_TOP_DYNAMIC:
        break;
    }

    if (body && !validate_kernel_get_ghs_usage_in_stmt(body, model, frame, function, worker, in_kernel, err)) {
      return 0;
    }
  }
  return 1;
}

static void free_temp_frame(EFunctionFrame *frame) {
  size_t i;
  for (i = 0; i < frame->local_count; i++) {
    free(frame->locals[i].name);
    free(frame->locals[i].type_name);
  }
  free(frame->locals);
  memset(frame, 0, sizeof(*frame));
}

static int validate_non_function_local_decls(const EProgram *program, const ESemanticModel *model, char err[256]) {
  size_t i;
  for (i = 0; i < program->count; i++) {
    const ETopDecl *top = &program->items[i];
    const EStmt *body = NULL;
    const char *owner_name = NULL;
    int in_worker = 0;
    EFunctionFrame frame;
    memset(&frame, 0, sizeof(frame));

    switch (top->kind) {
      case E_TOP_KERNEL:
        body = top->as.kernel.body;
        owner_name = "kernel";
        break;
      case E_TOP_WORKER:
        body = top->as.worker.body;
        owner_name = top->as.worker.name;
        in_worker = 1;
        break;
      case E_TOP_TYPE:
        body = top->as.tdecl.body;
        owner_name = top->as.tdecl.name;
        break;
      case E_TOP_STRUCT:
      case E_TOP_FUNCTION:
      case E_TOP_DECLARE:
      case E_TOP_DYNAMIC:
        break;
    }

    if (!body) continue;
    if (!collect_local_decls_in_stmt(body, model, &frame, in_worker, err)) {
      free_temp_frame(&frame);
      return 0;
    }
    push_frame((ESemanticModel*)model, owner_name, NULL, frame.param_size, frame.stack_local_size,
               frame.reserved_reg_words, frame.vm_local_count, frame.locals, frame.local_count);
    memset(&frame, 0, sizeof(frame));
    free_temp_frame(&frame);
  }
  return 1;
}

int e_build_semantic_model(const EProgram *program, ESemanticModel *out_model, char err[256]) {
  memset(out_model, 0, sizeof(*out_model));
  if (err) err[0] = 0;
  out_model->default_in_words = 256u;
  out_model->default_out_words = 256u;
  out_model->default_signal_mail_box_size = 128u;

  if (!collect_declares(program, out_model, err)) {
    e_semantic_model_free(out_model);
    return 0;
  }

  if (!collect_validators(program, out_model, err)) {
    e_semantic_model_free(out_model);
    return 0;
  }

  if (!collect_type_layouts(program, out_model, err)) {
    e_semantic_model_free(out_model);
    return 0;
  }

  if (!collect_dynamic_pools(program, out_model, err)) {
    e_semantic_model_free(out_model);
    return 0;
  }

  if (!collect_top_level_roles(program, out_model, err)) {
    e_semantic_model_free(out_model);
    return 0;
  }

  if (!collect_function_checks(program, out_model, err)) {
    e_semantic_model_free(out_model);
    return 0;
  }

  if (!validate_non_function_local_decls(program, out_model, err)) {
    e_semantic_model_free(out_model);
    return 0;
  }

  if (!validate_worker_next_targets(out_model, err)) {
    e_semantic_model_free(out_model);
    return 0;
  }

  if (!validate_kernel_get_ghs_usage(program, out_model, err)) {
    e_semantic_model_free(out_model);
    return 0;
  }

  if (!validate_typeof_typeid_usage(program, out_model, err)) {
    e_semantic_model_free(out_model);
    return 0;
  }

  if (!validate_loop_control_usage(program, err)) {
    e_semantic_model_free(out_model);
    return 0;
  }

  return 1;
}

void e_semantic_model_free(ESemanticModel *model) {
  size_t i;
  if (!model) return;
  for (i = 0; i < model->validator_count; i++) {
    free(model->validators[i].type_name);
  }
  free(model->validators);
  for (i = 0; i < model->type_layout_count; i++) {
    size_t j;
    free(model->type_layouts[i].type_name);
    for (j = 0; j < model->type_layouts[i].field_count; j++) {
      free(model->type_layouts[i].fields[j].name);
      free(model->type_layouts[i].fields[j].type_name);
    }
    free(model->type_layouts[i].fields);
  }
  free(model->type_layouts);
  free(model->checks);
  for (i = 0; i < model->frame_count; i++) {
    size_t j;
    free(model->frames[i].owner_name);
    for (j = 0; j < model->frames[i].local_count; j++) {
      free(model->frames[i].locals[j].name);
      free(model->frames[i].locals[j].type_name);
    }
    free(model->frames[i].locals);
  }
  free(model->frames);
  free(model->workers);
  free(model->functions);
  for (i = 0; i < model->dynamic_pool_count; i++) {
    free(model->dynamic_pools[i].name);
    free(model->dynamic_pools[i].element_type_name);
  }
  free(model->dynamic_pools);
  model->kernel = NULL;
  model->validators = NULL;
  model->type_layouts = NULL;
  model->checks = NULL;
  model->frames = NULL;
  model->workers = NULL;
  model->functions = NULL;
  model->dynamic_pools = NULL;
  model->kernel_count = 0;
  model->worker_count = 0;
  model->function_count = 0;
  model->validator_count = 0;
  model->type_layout_count = 0;
  model->check_count = 0;
  model->frame_count = 0;
  model->dynamic_pool_count = 0;
}

static const char *check_kind_name(EParamValidationKind kind) {
  switch (kind) {
    case E_PARAM_PRIMITIVE: return "primitive-bypass";
    case E_PARAM_CUSTOM_VALIDATED: return "validator-worker-dispatch";
    case E_PARAM_CUSTOM_UNVALIDATED: return "warning-no-validator";
  }
  return "unknown";
}

static const char *param_abi_kind_name(EParamAbiKind kind) {
  switch (kind) {
    case E_PARAM_ABI_VALUE: return "value";
    case E_PARAM_ABI_TYPE_REF: return "type-ref";
  }
  return "unknown";
}

void e_semantic_model_dump(FILE *out, const ESemanticModel *model) {
  size_t i;

  fprintf(out, "entry-defaults in_words=%u out_words=%u signal_mail_box_size=%u\n",
          model->default_in_words,
          model->default_out_words,
          model->default_signal_mail_box_size);
  fputs("program-shape\n", out);
  fprintf(out, "  kernel count=%zu\n", model->kernel_count);
  if (model->kernel) {
    fprintf(out, "    first_param=%s %s\n",
            model->kernel->params[0].type.name,
            model->kernel->params[0].name);
  }
  fprintf(out, "  workers count=%zu\n", model->worker_count);
  for (i = 0; i < model->worker_count; i++) {
    fprintf(out, "    worker %s param=%s %s\n",
            model->workers[i]->name,
            model->workers[i]->params[0].type.name,
            model->workers[i]->params[0].name);
    fprintf(out, "      note worker param type maps to underlying operation GHS layout\n");
  }
  fprintf(out, "  functions count=%zu\n", model->function_count);
  for (i = 0; i < model->function_count; i++) {
    fprintf(out, "    function %s\n", model->functions[i]->name);
  }
  fprintf(out, "  dynamics count=%zu\n", model->dynamic_pool_count);
  for (i = 0; i < model->dynamic_pool_count; i++) {
    fprintf(out, "    dynamic %s element=%s",
            model->dynamic_pools[i].name,
            model->dynamic_pools[i].element_type_name);
    if (model->dynamic_pools[i].element_array_len != 0u) {
      fprintf(out, "[%u]", model->dynamic_pools[i].element_array_len);
    }
    fprintf(out, " element_size=%zu min_free=%u max_free=%u grow_by=%u maintenance=round-start replenish_to_min_free use_scope=entire-round\n",
            model->dynamic_pools[i].element_size,
            model->dynamic_pools[i].min_free,
            model->dynamic_pools[i].max_free,
            model->dynamic_pools[i].grow_by);
    fprintf(out, "      header words=%u active_count@%u free_count@%u live_head@%u live_tail@%u free_head@%u\n",
            model->dynamic_pools[i].header_word_count,
            model->dynamic_pools[i].active_count_word,
            model->dynamic_pools[i].free_count_word,
            model->dynamic_pools[i].live_head_word,
            model->dynamic_pools[i].live_tail_word,
            model->dynamic_pools[i].free_head_word);
    fputs("      note new capacity is prepended at free_head, so grow never traverses the free list tail\n", out);
    fputs("      note live_tail is maintained in the main header for O(1) append to the active list\n", out);
  }

  fputs("validator-manifest\n", out);
  if (model->validator_count == 0) {
    fputs("  none\n", out);
  }
  for (i = 0; i < model->validator_count; i++) {
    const EValidatorBinding *binding = &model->validators[i];
    fprintf(out, "  type %s validator_id=0x%08X route=validator_worker\n",
            binding->type_name, binding->validator_id);
    fprintf(out, "    ghs_span=%zu\n", binding->ghs_span);
    fputs("    note compile target: EPA validator entry for type body\n", out);
    fputs("    note runtime route: validator worker dispatches by validator_id\n", out);
    fputs("    note pending: emit EPA function/template and worker manifest blob\n", out);
  }

  fputs("type-ghs-layouts\n", out);
  if (model->type_layout_count == 0) {
    fputs("  none\n", out);
  }
  for (i = 0; i < model->type_layout_count; i++) {
    size_t j;
    const ETypeLayout *layout = &model->type_layouts[i];
    fprintf(out, "  type %s total_size=%zu backing=single-ghs\n",
            layout->type_name, layout->total_size);
    for (j = 0; j < layout->field_count; j++) {
      fprintf(out, "    field %s:%s offset=%zu size=%zu\n",
              layout->fields[j].name, layout->fields[j].type_name,
              layout->fields[j].ghs_offset, layout->fields[j].ghs_size);
    }
  }

  fputs("function-ghs-frames\n", out);
  if (model->frame_count == 0) {
    fputs("  none\n", out);
  }
  for (i = 0; i < model->frame_count; i++) {
    size_t j;
    fprintf(out, "  func %s total_size=%zu params=%zu stack_locals=%zu reserved_reg_words=%u\n",
            model->frames[i].owner_name,
            model->frames[i].total_size,
            model->frames[i].param_size,
            model->frames[i].stack_local_size,
            model->frames[i].reserved_reg_words);
    fprintf(out, "    vm_locals=%u\n", model->frames[i].vm_local_count);
    for (j = 0; j < model->frames[i].local_count; j++) {
      const ELocalBinding *local = &model->frames[i].locals[j];
      fprintf(out, "    local %s %s", local->type_name, local->name);
      if (local->array_len != 0u) fprintf(out, "[%u]", local->array_len);
      fprintf(out, " size=%zu", local->byte_size);
      if (local->storage == E_LOCAL_STACK) {
        fprintf(out, " storage=stack offset=%zu", local->stack_offset);
        if (local->vm_local_words != 0u) fprintf(out, " vm_local=%u", local->vm_local_slot);
      } else if (local->storage == E_LOCAL_STACK_STATIC) {
        fprintf(out, " storage=static vm_local=%u", local->vm_local_slot);
      } else if (local->storage == E_LOCAL_ARENA_SCOPED) {
        fprintf(out, " storage=local-scope-arena");
      } else {
        fprintf(out, " storage=reg r%u", local->reg_index);
        if (local->reg_words == 2u) fprintf(out, ":r%u", local->reg_index + 1u);
      }
      fputc('\n', out);
    }
  }

  fputs("function-validation-plan\n", out);
  if (model->check_count == 0) {
    fputs("  none\n", out);
  }
  for (i = 0; i < model->check_count; i++) {
    const EFunctionParamCheck *check = &model->checks[i];
    const EParam *param = &check->function->params[check->param_index];
    fprintf(out, "  func %s param %s:%s action=%s",
            check->function->name, param->name, param->type.name, check_kind_name(check->kind));
    if (check->validator_id != 0u) {
      fprintf(out, " validator_id=0x%08X", check->validator_id);
    }
    fprintf(out, " abi=%s frame_offset=%zu abi_size=%zu referent_size=%zu",
            param_abi_kind_name(check->abi_kind),
            check->ghs_offset, check->ghs_size, check->referent_size);
    if (check->vm_local_words != 0u) {
      fprintf(out, " vm_local=%u", check->vm_local_slot);
    }
    fputc('\n', out);

    if (check->kind == E_PARAM_CUSTOM_VALIDATED) {
      fputs("    note compile step: inject pre-entry validator dispatch stub\n", out);
      fputs("    note runtime step: validator worker roots into emitted EPA body\n", out);
    } else if (check->kind == E_PARAM_CUSTOM_UNVALIDATED) {
      fputs("    note warning: custom type currently falls through with validation disabled\n", out);
    }
    if (check->abi_kind == E_PARAM_ABI_TYPE_REF) {
      fputs("    note ABI: declared type parameter is passed by reference descriptor, not copied bytes\n", out);
      fputs("    note descriptor contract: identifies stack/frame-backed or local-arena-backed storage\n", out);
    }
  }
}
