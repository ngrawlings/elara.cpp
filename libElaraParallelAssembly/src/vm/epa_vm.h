#ifndef EPA_VM_H
#define EPA_VM_H

#include <stdint.h>
#include <stddef.h>

#include "../memory/epa_ghs.h"
#include "../memory/epa_stack.h"

// Common State Control (flow-owned register bank)
#ifndef EPA_CSC_SLOTS
#define EPA_CSC_SLOTS 4
#endif

#define EPA_VM_REGS_MAX 4
#define EPA_VM_STACK_MAX 256
#define EPA_VM_LOCALS_MAX 32
#define EPA_VM_LSCOPE_MAX 32

#ifndef EPA_VM_LBYTES_DEFAULT_CAP
#define EPA_VM_LBYTES_DEFAULT_CAP (16u * 1024u)
#endif

// EIP that Flow owns (same struct you described)
typedef enum { EPA_BLOCK_ENTRY = 0, EPA_BLOCK_FUNC = 1 } EpaBlockType;
typedef struct {
  uint8_t  block_type;   // entry or func
  uint32_t block_id;     // slot or func index
  uint32_t rel_pc;       // byte offset inside that block
} EpaEip;

typedef struct {
  // Flow-owned execution pointer (relative to descriptor)
  EpaEip eip;

  // Common State Control (flow-owned register bank). r0..r3.
  uint32_t csc[EPA_CSC_SLOTS];

  //size_t pc;                         // program counter (byte offset)
  EpaStack stack;

  int32_t locals[EPA_VM_LOCALS_MAX];

  // Byte-addressable local arena for transient buffers (FMT, serialization, temp arrays, etc.)
  uint8_t  *lbytes;      // base pointer (owned by worker)
  uint32_t  lbytes_cap;  // capacity in bytes
  uint32_t  lbytes_top;  // bump allocator head in bytes
  uint32_t  lscope_marks[EPA_VM_LSCOPE_MAX];
  uint32_t  lscope_depth;

  uint8_t yielded;                   // set by YIELD opcode
  uint8_t yield_policy;              // SOFT / HARD
} EpaVM;

int epa_vm_init(EpaVM *vm);
void epa_vm_free(EpaVM *vm);
void epa_vm_reset(EpaVM *vm);


// Explicit init for threadpool threads / special cases
int  epa_vm_init_with_lbytes(EpaVM *vm, uint32_t lbytes_cap);

// Optional helper: bump allocate from lbytes
void *epa_vm_lbytes_alloc(EpaVM *vm, uint32_t nbytes, uint32_t align, char err[256]);

#endif
