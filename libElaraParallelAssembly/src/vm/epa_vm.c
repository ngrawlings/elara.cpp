#include "epa_vm.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t align_up_u32(uint32_t x, uint32_t a) {
  if (a == 0) return x;
  uint32_t m = a - 1u;
  return (x + m) & ~m;
}

int epa_vm_init_with_lbytes(EpaVM *vm, uint32_t lbytes_cap) {
  if (!vm) return -1;
  memset(vm, 0, sizeof(*vm));

  // Stack is fixed-size in your current implementation.
  // (No heap alloc, so safe for massive thread counts.)
  epa_stack_init(&vm->stack);

  if (lbytes_cap) {
    vm->lbytes = (uint8_t*)malloc((size_t)lbytes_cap);
    if (!vm->lbytes) {
      // Keep VM in a safe state
      memset(vm, 0, sizeof(*vm));
      return -1;
    }
    vm->lbytes_cap = lbytes_cap;
    vm->lbytes_top = 0;
  }

  // Start clean.
  epa_vm_reset(vm);
  return 0;
}

int epa_vm_init(EpaVM *vm) {
  return epa_vm_init_with_lbytes(vm, EPA_VM_LBYTES_DEFAULT_CAP);
}

void epa_vm_free(EpaVM *vm) {
  if (!vm) return;
  if (vm->lbytes) {
    free(vm->lbytes);
    vm->lbytes = NULL;
  }
  vm->lbytes_cap = 0;
  vm->lbytes_top = 0;

  // stack is not heap-owned; just clear
  memset(&vm->stack, 0, sizeof(vm->stack));
  memset(vm->locals, 0, sizeof(vm->locals));
  memset(vm->csc, 0, sizeof(vm->csc));
  memset(&vm->eip, 0, sizeof(vm->eip));
  vm->yielded = 0;
  vm->yield_policy = 0;
}

void epa_vm_reset(EpaVM *vm) {
  if (!vm) return;

  // EIP/csc reset so a new entry starts deterministic
  memset(&vm->eip, 0, sizeof(vm->eip));
  memset(vm->csc, 0, sizeof(vm->csc));

  // Clear stack contents + top (depends on your stack struct layout;
  // safest is to call stack_init again)
  epa_stack_init(&vm->stack);

  // Locals cleared
  memset(vm->locals, 0, sizeof(vm->locals));

  // Arena resets (memory remains allocated)
  vm->lbytes_top = 0;
  vm->lscope_depth = 0;
  memset(vm->lscope_marks, 0, sizeof(vm->lscope_marks));

  vm->yielded = 0;
  vm->yield_policy = 0;
}

void *epa_vm_lbytes_alloc(EpaVM *vm, uint32_t nbytes, uint32_t align, char err[256]) {
  if (err) err[0] = 0;
  if (!vm || !nbytes) {
    if (err) snprintf(err, 256, "epa_vm_lbytes_alloc: invalid args");
    return NULL;
  }
  if (!vm->lbytes || vm->lbytes_cap == 0) {
    if (err) snprintf(err, 256, "epa_vm_lbytes_alloc: no lbytes arena");
    return NULL;
  }

  uint32_t a = (align == 0) ? 4u : align;
  uint32_t at = align_up_u32(vm->lbytes_top, a);

  if (at > vm->lbytes_cap || nbytes > (vm->lbytes_cap - at)) {
    if (err) snprintf(err, 256, "epa_vm_lbytes_alloc: OOM (need %u, have %u)",
                      (unsigned)nbytes, (unsigned)(vm->lbytes_cap - at));
    return NULL;
  }

  void *p = vm->lbytes + at;
  vm->lbytes_top = at + nbytes;
  return p;
}
