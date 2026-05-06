---

## 3️⃣ `EPA_FMT_AND_LOG.md`

```markdown
# 🖨️ EPA Formatting and Logging Semantics

This document defines how **strings, formatting, and logging**
are expected to behave in EPA.

This is a **semantic contract**; opcodes may be implemented incrementally.

---

## 1. Canonical String Representation

All strings are represented as:

(offset, length)

yaml
Copy code

Where:
- `offset` is an absolute offset into the program blob
- `length` is the number of bytes

No null-termination is assumed.

---

## 2. String Sources

Strings may originate from:
- `DATA_BLOCK` constants (`.CONST_STR`)
- formatting operations (`FMT`)
- future kernel-owned arenas

Workers **must not store transient strings in globals**.

---

## 3. LOAD_CONST for Strings

LOAD_CONST <id>

markdown
Copy code

Registers:
- `r0` → offset
- `r1` → length
- `r2` → kind == STR

---

## 4. Formatting (`FMT`)

### Conceptual Behavior
`FMT`:
- consumes a template string `(r0,r1)`
- consumes N arguments from stack
- produces a **new string reference**

The resulting string:
- lives in worker-local transient memory
- is valid until worker halts or yields

### Return Convention
r0 = offset
r1 = length

yaml
Copy code

---

## 5. Logging (`LOG`)

### Conceptual Behavior
`LOG`:
- consumes a string `(r0,r1)`
- emits it to the host logging facility
- may be a NOOP on some platforms

Logging:
- must never mutate VM state
- must never trap
- must not require string persistence

---

## 6. Failure Semantics

If:
- `FMT` receives a non-string template
- formatting fails
- logging backend is unavailable

Then:
- result is empty string or NOOP
- execution continues

Logging must **never crash the VM**.

---

## 7. Design Intent

- Logging is optional, not structural
- Formatting is deterministic
- Strings are explicit, not magical
- Works on GPU, headless, or embedded targets

---

_End of EPA Formatting and Logging Semantics_