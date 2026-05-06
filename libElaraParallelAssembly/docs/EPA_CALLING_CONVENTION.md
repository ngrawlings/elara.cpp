# 📞 EPA Calling Convention

This document defines how **functions, arguments, returns, and values**
are passed in EPA.

The goal is **simplicity, predictability, and composability**.

---

## 1. General Principles

- Stack-based argument passing
- Registers for return values
- No implicit state sharing
- Explicit frame sizing

---

## 2. Function Definition

Functions are defined using:

FUNC_START <func_id> <frame_words>
...
FUNC_END


Where:
- `func_id` is a unique function identifier
- `frame_words` defines local frame size

---

## 3. Calling a Function

CALL <func_id>


### Caller Responsibilities
- Push arguments onto the stack (in order)
- Assume registers may be clobbered

### Callee Responsibilities
- Pop arguments as needed
- Use locals for persistent state
- Return values via registers

---

## 4. Return Values

### Scalar Returns
- `r0` — primary return value

### Pair Returns (Canonical)
Used for:
- strings
- 64-bit values
- structured values

r0 = low / offset
r1 = high / length


This convention is **universal** across EPA.

---

## 5. Register Volatility

| Register | Guaranteed |
|--------|------------|
| r0–r3 | ❌ volatile |
| locals | ✅ stable |

Never rely on register contents across:
- `CALL`
- `RET`
- `YIELD`
- `SYNC`

---

## 6. Example

```asm
PUSH_I32 10
PUSH_I32 20
CALL 42
; result now in r0

