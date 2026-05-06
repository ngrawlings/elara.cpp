#include "epa_stack.h"
#include <stdlib.h>

void epa_stack_init(EpaStack *stack) {
	stack->sp = 0;
	stack->cap = 4096;
	stack->words = (uint32_t*)calloc(4096, sizeof(uint32_t));
}

void epa_stack_free(EpaStack *stack) {
	free(stack->words);
}

int epa_stack_push(EpaStack *st, uint32_t v) {
  if (!st || st->sp >= st->cap) return 0;
  st->words[st->sp++] = v;
  return 1;
}

int epa_stack_pop(EpaStack *st, uint32_t *out) {
  if (!st || st->sp == 0) return 0;
  *out = st->words[--st->sp];
  return 1;
}

int epa_stack_peek(const EpaStack *st, uint32_t *out) {
  if (!st || st->sp == 0) return 0;
  *out = st->words[st->sp - 1];
  return 1;
}
