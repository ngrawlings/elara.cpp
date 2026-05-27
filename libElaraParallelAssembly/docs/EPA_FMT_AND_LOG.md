# EPA Formatting and Logging Semantics

This document defines the current EPA contract for strings, formatting, and logging.

## Canonical String Representation

EPA still treats strings as an explicit pair:

- `r0` = offset
- `r1` = length

No null terminator is implied.

## String Sources

Strings may come from:

- `DATA_BLOCK` constants loaded with `LOAD_CONST`
- formatting operations
- future worker-local or kernel-owned arenas

Kernel identity is not represented as a runtime string in the current cross-kernel routing path. Kernel ids are compiled into manifest uids via `KERNEL_ID` and `ACL_ALLOW`.

## `LOAD_CONST`

For string constants:

- `r0` = offset
- `r1` = length
- `r2` = constant kind

## Formatting

`FMT` conceptually:

- consumes a template string in `(r0, r1)`
- consumes arguments from the stack
- produces a new string reference in `(r0, r1)`

The resulting string is transient worker-local data.

## Logging

`LOG` conceptually:

- consumes a string in `(r0, r1)`
- emits it to the host logging facility
- may degrade to a no-op on constrained targets

Logging must never mutate VM semantics and must never crash execution.

## Current Design Direction

The language/runtime has evolved in one important way around strings:

- `far_signal` no longer depends on building a target kernel id string at runtime
- `kernalId("...")` is hashed by the E compiler
- cross-kernel routing uses the compiled 64-bit uid split across two `u32` values

So string formatting remains a regular data feature, but cross-kernel identity is now manifest-driven rather than string-buffer-driven.
