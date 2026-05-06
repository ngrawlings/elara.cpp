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

static int is_primitive_type(const char *name) {
  return strcmp(name, "int") == 0 ||
         strcmp(name, "long") == 0 ||
         strcmp(name, "short") == 0 ||
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
                      EParamValidationKind kind, unsigned int validator_id,
                      size_t ghs_offset, size_t ghs_size) {
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
  model->checks[model->check_count].validator_id = validator_id;
  model->checks[model->check_count].ghs_offset = ghs_offset;
  model->checks[model->check_count].ghs_size = ghs_size;
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

static int push_frame(ESemanticModel *model, const EFunction *fn, size_t total_size) {
  EFunctionFrame *next;
  next = (EFunctionFrame*)realloc(model->frames, sizeof(EFunctionFrame) * (model->frame_count + 1u));
  if (!next) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  model->frames = next;
  model->frames[model->frame_count].function = fn;
  model->frames[model->frame_count].total_size = total_size;
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

static int type_span_for_name(const ESemanticModel *model, const char *name, size_t *out_size, char err[256]) {
  const ETypeLayout *layout;
  if (is_primitive_type(name)) {
    *out_size = primitive_size(name);
    return 1;
  }
  layout = find_type_layout(model, name);
  if (!layout) {
    snprintf(err, 256, "missing GHS layout for type '%s'", name);
    return 0;
  }
  *out_size = layout->total_size;
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
      if (!type_span_for_name(model, top->as.tdecl.params[j].type.name, &field_size, err)) {
        return 0;
      }
      next_offset += field_size;
    }

    push_type_layout(model, &top->as.tdecl, next_offset);
    layout = &model->type_layouts[model->type_layout_count - 1u];

    next_offset = 0u;
    for (j = 0; j < top->as.tdecl.param_count; j++) {
      size_t field_size;
      field_size = is_primitive_type(top->as.tdecl.params[j].type.name)
        ? primitive_size(top->as.tdecl.params[j].type.name)
        : find_type_layout(model, top->as.tdecl.params[j].type.name)->total_size;
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

static int collect_function_checks(const EProgram *program, ESemanticModel *model) {
  size_t i;
  for (i = 0; i < program->count; i++) {
    const ETopDecl *top = &program->items[i];
    size_t j;
    size_t next_offset = 0u;
    if (top->kind != E_TOP_FUNCTION) continue;
    for (j = 0; j < top->as.func.param_count; j++) {
      const EParam *param = &top->as.func.params[j];
      size_t field_size = 0u;

      if (is_primitive_type(param->type.name)) {
        field_size = primitive_size(param->type.name);
      } else {
        const ETypeLayout *layout = find_type_layout(model, param->type.name);
        if (layout) field_size = layout->total_size;
      }

      if (is_primitive_type(param->type.name)) {
        push_check(model, &top->as.func, j, E_PARAM_PRIMITIVE, 0u, next_offset, field_size);
      } else {
        const EValidatorBinding *binding = find_validator(model, param->type.name);
        if (binding) {
          push_check(model, &top->as.func, j, E_PARAM_CUSTOM_VALIDATED,
                     binding->validator_id, next_offset, field_size);
        } else {
          push_check(model, &top->as.func, j, E_PARAM_CUSTOM_UNVALIDATED, 0u, next_offset, field_size);
        }
      }
      next_offset += field_size;
    }
    push_frame(model, &top->as.func, next_offset);
  }
  return 1;
}

int e_build_semantic_model(const EProgram *program, ESemanticModel *out_model, char err[256]) {
  memset(out_model, 0, sizeof(*out_model));
  if (err) err[0] = 0;

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

  if (!collect_function_checks(program, out_model)) {
    e_semantic_model_free(out_model);
    snprintf(err, 256, "failed to collect function parameter validation plan");
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

void e_semantic_model_dump(FILE *out, const ESemanticModel *model) {
  size_t i;

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
    fprintf(out, "  func %s total_size=%zu backing=single-ghs\n",
            model->frames[i].function->name, model->frames[i].total_size);
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
    fprintf(out, " ghs_offset=%zu ghs_size=%zu",
            check->ghs_offset, check->ghs_size);
    fputc('\n', out);

    if (check->kind == E_PARAM_CUSTOM_VALIDATED) {
      fputs("    note compile step: inject pre-entry validator dispatch stub\n", out);
      fputs("    note runtime step: validator worker roots into emitted EPA body\n", out);
    } else if (check->kind == E_PARAM_CUSTOM_UNVALIDATED) {
      fputs("    note warning: custom type currently falls through with validation disabled\n", out);
    }
  }
}
