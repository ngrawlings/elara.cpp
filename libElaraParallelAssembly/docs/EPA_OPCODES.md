# 📘 EPA Opcode Reference

This document defines the **semantic contract** of EPA opcodes.  
It is authoritative for EPAASM authors (human or AI).

---

## Core / Framing

| Opcode | Description |
|------|-------------|
| `NOOP` | No operation |
| `SET_MODE` | Set VM execution flags |
| `END` | End current block |
| `DATA_BLOCK` | Constant pool (strings, numbers, bytes); load-time only |

---

## Control Flow

| Opcode | Description |
|------|-------------|
| `JMP` | Unconditional relative jump |
| `JZ` | Jump if zero |
| `JNZ` | Jump if non-zero |
| `JLZ` | Jump if less than zero |
| `JGZ` | Jump if greater than zero |
| `YIELD` | Yield execution |

---

## Kernel / Worker Scheduling

| Opcode | Description |
|------|-------------|
| `ENTRY_START` | Begin kernel or worker definition |
| `ENTRY_END` | End entry definition |
| `ENTRY_EXEC` | Execute worker |
| `ENTRY_HALT` | Halt worker |
| `SYNC` | Worker → kernel signal |
| `WAIT_ON_SYNC` | Kernel blocks until SYNC |

---

## Stack & Arithmetic

| Opcode | Description |
|------|-------------|
| `PUSH_I32` | Push immediate 32-bit integer |
| `PUSH_R` | Push register value |
| `POP_R` | Pop stack value into register |
| `ADD_I32` | Integer addition |
| `SUB_I32` | Integer subtraction |
| `MUL_I32` | Integer multiplication |
| `LT_I32` | Less-than comparison |

---

## Locals & Registers

| Opcode | Description |
|------|-------------|
| `STORE_L` | Store stack value into local slot |
| `LOAD_L` | Load local slot onto stack |
| `MV` | Copy register value |
| `SET_R` | Set register to immediate value |
| `INC` | Increment register |
| `DEC` | Decrement register |

---

## Constants

| Opcode | Description |
|------|-------------|
| `LOAD_CONST` | Load constant by ID into registers |

**Register convention for `LOAD_CONST`:**
- `r0`, `r1` → value or `(offset, length)`
- `r2` → constant kind
- `r3` → flags (reserved)

---

## Functions

| Opcode | Description |
|------|-------------|
| `FUNC_START` | Begin function definition |
| `FUNC_END` | End function definition |
| `CALL` | Call function by ID |
| `RET` | Return from function |

---

## Data Transfer / Messaging

| Opcode | Description |
|------|-------------|
| `KERNEL_TRX_IN_L` | Kernel pulls worker local data |
| `KERNEL_TRX_OUT_L` | Kernel pushes data to worker |
| `WORKER_TRX_IN_L` | Worker pulls kernel data |
| `WORKER_TRX_OUT_L` | Worker pushes data to kernel |
| `WORKER_TRX` | Local-to-local transfer |

---

## Global Handle Space

| Opcode | Description |
|------|-------------|
| `G_ALLOC` | Allocate global handle |
| `G_FREE` | Free global handle |
| `G_XFER` | Transfer ownership |
| `G_RESIZE` | Resize handle |
| `G_PTR` | Get host pointer (host only) |
| `G_META` | Query handle metadata |

---

## Debug & Faults

| Opcode | Description |
|------|-------------|
| `BREAK` | Debug breakpoint |
| `TRAP` | Fatal VM trap |
| `EXCEPT` | Raise exception |

---

## GPU Semantic Layer

EPA provides a **portable GPU abstraction layer**.  
Backends may map these opcodes to OpenGL, Vulkan, CUDA, Metal, or emulate them.

### Resource Lifecycle
- `CLEAR`
- `VIEWPORT`
- `DRAW`
- `FRAME`

### Buffers
- `GPU_BUF_CREATE`
- `GPU_BUF_BIND`
- `GPU_BUF_BIND_BASE`
- `GPU_BUF_SUBDATA`

### Vertex Layout
- `GPU_VTXLAY_CREATE`
- `GPU_VTXLAY_BIND`
- `GPU_VTX_VBO`
- `GPU_VTX_EBO`
- `GPU_VTX_ATTR_FMT`
- `GPU_VTX_ATTR_EN`

### Textures & Samplers
- `GPU_TEX_CREATE`
- `GPU_TEX_BIND`
- `GPU_SAMP_CREATE`
- `GPU_SAMP_PARAM`
- `GPU_TEX_SUBIMG`

### Shaders & Programs
- `GPU_SHD_LOAD`
- `GPU_PROG_LINKVF`
- `GPU_PROG_USE`
- `GPU_PROGRAM_LINK_COMP`

### Render Targets
- `GPU_RT_CREATE`
- `GPU_RT_BIND`
- `GPU_RT_ATTACH`
- `GPU_RT_BLIT`

### State Control
- `GPU_SET_BLEND`
- `GPU_SET_DEPTH`
- `GPU_SET_CULL`
- `GPU_SET_SCISSOR`
- `GPU_SET_CMASK`
- `GPU_SET_DBias`

### Draw Variants
- `GPU_DRAW_IDX`
- `GPU_DRAW_INST`
- `GPU_DRAW_IDXI`

### Compute & Sync
- `GPU_DISPATCH`
- `GPU_BARRIER`

### Debug & Profiling
- `GPU_DBG_LABEL`
- `GPU_Q_BEGIN`
- `GPU_Q_END`
- `GPU_Q_RESULT64`

### Fences & Presentation
- `GPU_FENCE_INS`
- `GPU_FENCE_WAIT`
- `GPU_FENCE_DEL`
- `GPU_PRESENT`

---

## Design Principles

- Stack-based execution model
- Explicit kernel/worker scheduling
- Deterministic message passing
- Load-time constants via `DATA_BLOCK`
- No implicit shared-state mutation from workers

---

## Notes for AI / Code Generation

- Prefer locals for persistent values
- Treat `(r0, r1)` as the canonical pair
- Do not assume registers survive `YIELD` or `SYNC`
- Constants must be declared via `.CONST_*` directives

---

_End of EPA opcode reference_
