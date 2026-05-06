#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint32_t *words;
  size_t sp;
  size_t cap;
} EpaStack;

// Stack helpers
void epa_stack_init(EpaStack *stack);
void epa_stack_free(EpaStack *stack);
int epa_stack_push(EpaStack *st, uint32_t v);
int epa_stack_pop(EpaStack *st, uint32_t *out);
int epa_stack_peek(const EpaStack *st, uint32_t *out);
