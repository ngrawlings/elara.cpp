#pragma once
#include <stdint.h>
#include <stddef.h>

/*
EPA ASM COMPILER (epa_asm_compiler.c) — MAINTENANCE / EXTENSION GUIDE

Design rule
  Keep the assembler ~95% descriptor-driven (data, not code).
  Only add helpers for the small set of mnemonics whose semantics cannot be
  expressed as "opcode + fixed packing of typed operands".

How compilation works
  1) Read the assembly file line-by-line.
  2) Strip comments and trim whitespace.
  3) Tokenize by whitespace (up to a small fixed limit).
  4) If the first token ends with ':', record it as a label at the current PC,
     then continue parsing the remaining tokens as an instruction on the same line.
  5) Dispatch the mnemonic:
       - If it is one of the small "helper" mnemonics, run the helper.
       - Otherwise look it up in the descriptor table (kDesc[]) and emit using
         the declared operand packing schema.
  6) After pass 1 completes, patch label fixups (e.g., rel32 jumps) in pass 2.
     Jump rel32 values are relative to the NEXT PC (pc_after_immediate).

Descriptor-driven emission
  The descriptor table in epa_asm_compiler.c maps:
    mnemonic string -> opcode (u16) -> operand kinds (ArgKind[])
  Generic emission parses each operand according to its ArgKind and writes
  little-endian bytes to the output buffer in the declared order.

Adding a new opcode (typical case; no helper required)
  1) Add the opcode value and name in opcode_def.h (and VM side implementation).
  2) Add ONE descriptor entry to kDesc[] in epa_asm_compiler.c:
       { "MNEMONIC", EPA_OP_..., argc, { AK_... , AK_... }, NULL }
  3) Ensure the VM expects the same operand order and widths.
  4) Add or update a unit test (.epaasm) to lock in the encoding.

When a helper IS justified (rare; keep it small)
  Add a helper only if at least one is true:
    - The operand can be a label OR an immediate and needs a fixup table
      (e.g., JMP/JZ/JNZ/JLZ/JGZ rel32 and label targets).
    - The opcode changes based on operand type or shape (e.g., PUSH Rn vs PUSH imm32).
    - The instruction has optional operands or symbolic operands that are nicer
      in ASM (e.g., YIELD soft|hard), and you do not want separate mnemonics.

House rules
  - Prefer adding a new ArgKind over adding bespoke opcode logic, unless the
    semantics truly require it.
  - Helpers must produce consistent, line-numbered error messages.
  - Keep the ASM surface syntax stable and AI-friendly.

*/

#ifndef EPA_MAX_ERR
#define EPA_MAX_ERR 256
#endif

// Compile a text assembly file into a binary EPA blob.
// Returns malloc'd buffer (caller frees) or NULL on error.
uint8_t *epa_asm_compile_file(const char *path, size_t *out_len, char err[EPA_MAX_ERR]);

// Compile an in-memory epaasm string into a binary EPA blob.
// Returns malloc'd buffer (caller frees) or NULL on error.
uint8_t *epa_asm_compile_src(const char *src, size_t *out_len, char err[EPA_MAX_ERR]);

// Return total instruction byte size (opcode + params) for an assembly-text mnemonic.
// first_arg: first operand token (may be NULL). Needed to disambiguate PUSH Rn vs PUSH imm.
// Returns -1 for unknown mnemonics, -2 for variable-length instructions.
int epa_asm_instr_total_bytes(const char *mnemonic, const char *first_arg);
