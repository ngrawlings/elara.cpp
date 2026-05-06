# 🧠 EPA Memory Model

This document defines how **memory, registers, locals, globals, and lifetimes**
work in the EPA virtual machine.

This is a **semantic contract**, not an implementation detail.

---

## 1. Storage Classes Overview

EPA defines **three distinct storage classes**:

| Class | Scope | Lifetime | Mutability |
|-----|------|---------|-----------|
| Registers | Per worker | Ephemeral | Mutable |
| Locals | Per worker | Persistent | Mutable |
| Globals | Kernel-owned | Program lifetime | Controlled |

---

## 2. Registers

### Description
Registers (`r0..rN`) are **fast, ephemeral storage** used for:
- temporary values
- expression evaluation
- calling conventions
- opcode input/output

### Rules
- Registers **may be clobbered** by:
  - `YIELD`
  - `SYNC`
  - `ENTRY_EXEC`
- Code must **not assume register values persist** across scheduling boundaries.
- Registers are **never shared** between workers.

### Best Practice
> Use registers for *temps*, locals for *variables*.

---

## 3. Local Memory (Locals)

### Description
Locals are **per-worker memory slots** indexed by `u8`.

They represent:
- variables
- persistent state
- structured values

### Access
- `STORE_L idx` — store top-of-stack into local slot
- `LOAD_L idx` — load local slot onto stack

### Properties
- Locals **persist across control flow**
- Locals **survive YIELD and SYNC**
- Locals are **not visible to other workers**
- Local slot indices are stable for the lifetime of the worker

### Structured Values
Some values occupy **multiple local slots**:

| Value | Representation |
|-----|----------------|
| String | `(offset, length)` → 2 slots |
| 64-bit integer | `(low32, high32)` → 2 slots |

---

## 4. Global State

### Ownership Model
All global state is **kernel-owned**.

Workers:
- ❌ cannot directly mutate global state
- ✅ communicate via OUT queues and SYNC

Kernel:
- ✅ reads messages
- ✅ mutates global state
- ✅ schedules workers

This enforces a **deterministic actor model**.

---

## 5. Global Handle Space

Global handles represent **shared, zero-copy resources**.

Accessed via:
- `G_ALLOC`
- `G_FREE`
- `G_XFER`
- `G_RESIZE`
- `G_PTR` (host only)
- `G_META`

### Handle Representation
A handle is represented as:

(idx, generation)

This prevents stale-handle reuse.

---

## 6. Constants (DATA_BLOCK)

Constants are:
- immutable
- stored in the blob
- loaded at program load time

Accessed via:

LOAD_CONST id

Registers after load:
- `r0,r1` → value or `(offset,len)`
- `r2` → constant kind
- `r3` → flags (reserved)

Constants **must not be modified**.

---

## 7. Lifetime Summary

| Storage | Survives Yield | Shared | Mutable |
|------|---------------|--------|---------|
| Registers | ❌ | ❌ | ✅ |
| Locals | ✅ | ❌ | ✅ |
| Globals | ✅ | ✅ | Kernel-only |
| Constants | ✅ | ✅ | ❌ |

---

## 8. Design Intent

- Deterministic execution
- GPU / JIT friendly
- Human-readable assembly
- AI-safe semantics
- No hidden side effects

---

_End of EPA Memory Model_
