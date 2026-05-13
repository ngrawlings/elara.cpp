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

static int is_primitive_type(const char *name) {
  return strcmp(name, "int") == 0 ||
         strcmp(name, "long") == 0 ||
         strcmp(name, "short") == 0 ||
         strcmp(name, "byte") == 0 ||
         strcmp(name, "char") == 0 ||
         strcmp(name, "float") == 0 ||
         strcmp(name, "double") == 0 ||
         strcmp(name, "void") == 0 ||
         strcmp(name, "z") == 0;
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
  return 0u;
}

static size_t type_ref_size(const ESemanticModel *model, const ETypeRef *type, char err[256]) {
  size_t elem_size = 0u;
  size_t count = (type->array_len == 0u) ? 1u : (size_t)type->array_len;
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
                      unsigned int reserved_reg_words, ELocalBinding *locals,
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
  model->frames[model->frame_count].total_size = param_size + stack_local_size;
  model->frames[model->frame_count].locals = locals;
  model->frames[model->frame_count].local_count = local_count;
  model->frame_count++;
  return 1;
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
        if (!is_custom_type_with_layout(model, top->as.worker.params[0].type.name)) {
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
                                       EFunctionFrame *frame, char err[256]) {
  size_t i;
  if (!stmt) return 1;
  switch (stmt->kind) {
    case E_STMT_BLOCK:
      for (i = 0; i < stmt->as.block.count; i++) {
        if (!collect_local_decls_in_stmt(stmt->as.block.items[i], model, frame, err)) return 0;
      }
      return 1;
    case E_STMT_IF:
      if (!collect_local_decls_in_stmt(stmt->as.if_stmt.then_branch, model, frame, err)) return 0;
      if (!collect_local_decls_in_stmt(stmt->as.if_stmt.else_branch, model, frame, err)) return 0;
      return 1;
    case E_STMT_WHILE:
      return collect_local_decls_in_stmt(stmt->as.while_stmt.body, model, frame, err);
    case E_STMT_FOR:
      if (!collect_local_decls_in_stmt(stmt->as.for_stmt.init, model, frame, err)) return 0;
      return collect_local_decls_in_stmt(stmt->as.for_stmt.body, model, frame, err);
    case E_STMT_SWITCH:
      for (i = 0; i < stmt->as.switch_stmt.case_count; i++) {
        size_t j;
        for (j = 0; j < stmt->as.switch_stmt.cases[i].body.count; j++) {
          if (!collect_local_decls_in_stmt(stmt->as.switch_stmt.cases[i].body.items[j], model, frame, err)) return 0;
        }
      }
      return 1;
    case E_STMT_DECL: {
      size_t byte_size = type_ref_size(model, &stmt->as.decl.type, err);
      if (byte_size == 0u && stmt->as.decl.type.array_len == 0u && primitive_size(stmt->as.decl.type.name) == 0u &&
          !find_type_layout(model, stmt->as.decl.type.name)) {
        return 0;
      }
      if (stmt->as.decl.is_reg) {
        unsigned int reg_words;
        if (stmt->as.decl.is_local) {
          snprintf(err, 256, "decl '%s' cannot be both reg and local", stmt->as.decl.name);
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
      } else {
        push_local_binding(frame, stmt, stmt->as.decl.name, &stmt->as.decl.type, byte_size,
                           E_LOCAL_STACK, frame->stack_local_size, 0u, 0u);
        if (frame->local_count > 0u) {
          ELocalBinding *binding = &frame->locals[frame->local_count - 1u];
          if (stmt->as.decl.type.array_len == 0u && byte_size > 0u && byte_size <= 4u) {
            binding->vm_local_slot = (unsigned int)(frame->param_size / 4u + frame->stack_local_size / 4u);
            binding->vm_local_words = 1u;
          }
        }
        frame->stack_local_size += byte_size;
      }
      return 1;
    }
    case E_STMT_RETURN:
    case E_STMT_EXPR:
    case E_STMT_BREAK:
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
    if (!collect_local_decls_in_stmt(top->as.func.body, model, &frame, err)) {
      return 0;
    }
    push_frame(model, top->as.func.name, &top->as.func, frame.param_size, frame.stack_local_size,
               frame.reserved_reg_words, frame.locals, frame.local_count);
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
    case E_STMT_RAW_EPA:
      return 1;
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
        break;
      case E_TOP_TYPE:
        body = top->as.tdecl.body;
        owner_name = top->as.tdecl.name;
        break;
      case E_TOP_STRUCT:
      case E_TOP_FUNCTION:
      case E_TOP_DECLARE:
        break;
    }

    if (!body) continue;
    if (!collect_local_decls_in_stmt(body, model, &frame, err)) {
      free_temp_frame(&frame);
      return 0;
    }
    push_frame((ESemanticModel*)model, owner_name, NULL, frame.param_size, frame.stack_local_size,
               frame.reserved_reg_words, frame.locals, frame.local_count);
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
  model->kernel = NULL;
  model->validators = NULL;
  model->type_layouts = NULL;
  model->checks = NULL;
  model->frames = NULL;
  model->workers = NULL;
  model->functions = NULL;
  model->kernel_count = 0;
  model->worker_count = 0;
  model->function_count = 0;
  model->validator_count = 0;
  model->type_layout_count = 0;
  model->check_count = 0;
  model->frame_count = 0;
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
    for (j = 0; j < model->frames[i].local_count; j++) {
      const ELocalBinding *local = &model->frames[i].locals[j];
      fprintf(out, "    local %s %s", local->type_name, local->name);
      if (local->array_len != 0u) fprintf(out, "[%u]", local->array_len);
      fprintf(out, " size=%zu", local->byte_size);
      if (local->storage == E_LOCAL_STACK) {
        fprintf(out, " storage=stack offset=%zu", local->stack_offset);
        if (local->vm_local_words != 0u) fprintf(out, " vm_local=%u", local->vm_local_slot);
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
