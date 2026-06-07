# Current E Language Surface

This note captures the current `E` language rules that matter when writing code by hand, generating templates, or wiring the IDE debugger.

## EPA Slim-Core Opcode Boundary

EPA v1 core opcodes are single-byte values below `128`. Bit 7 is reserved for
the future extended/full-core opcode space.

The final slim-core slots `111..127` are assigned to F32 arithmetic. F32 values
are IEEE-754 binary32 bit patterns carried through the existing 32-bit
stack/register/local/GHS paths:

- `PUSH_F32`
- `ADD_F32`, `SUB_F32`, `MUL_F32`, `DIV_F32`
- `NEG_F32`, `SQRT_F32`
- `LT_F32`, `LE_F32`, `GT_F32`, `GE_F32`, `EQ_F32`, `NE_F32`
- `I32_TO_F32`, `F32_TO_I32`, `U32_TO_F32`, `F32_TO_U32`

The current E expression emitter is still integer-first; these opcodes are
available at EPA assembly/VM level and will become the target for typed E float
expressions when that language layer is expanded.

Slots `108..109` are byte rotate operations:

- `ROL_BYTE`
- `ROR_BYTE`

Both pop `(value, count)` from the stack, rotate the low byte by `count & 7`,
and push the rotated byte as a normal `u32`. E wrappers live in
`common/bytes.em` as `byte_rol(value, count)` and `byte_ror(value, count)`.

Bitwise integer operations are slim-core opcodes:

- `AND_I32`
- `OR_I32`
- `XOR_I32`
- `NOT_I32`

Until E expression syntax grows `&`, `|`, `^`, and `~`, use the wrappers in
`common/bytes.em`: `bit_and_i32`, `bit_or_i32`, `bit_xor_i32`,
`bit_not_i32`, plus byte-masked variants `byte_and`, `byte_or`, `byte_xor`,
and `byte_not`.

Formatting and logging are not core opcodes. Use:

```c
#include "common/egress.em"
```

That module defines `fmt_len(...)`, `fmt_into(...)`, and `log(...)` as ordinary
E functions with `EPA { ... }` implementation blocks. The egress frame protocol
can evolve there without changing the slim-core ISA.

## Variadic E Functions

E supports variadic functions as a language convention, not an EPA opcode:

```c
function int size(int format_off, int format_len, ...) {
  int argc = vararg_count();
  int first = vararg_i32(0);
  return format_len + argc + first;
}
```

Rules:

- `...` is only valid at the end of an ordinary `function` parameter list
- fixed parameters are passed through the normal function ABI
- extra arguments are packed by the caller into a local-byte `u32[]` block
- the callee receives hidden `vararg_off` and `vararg_count` frame locals
- `vararg_count()` returns the number of extra arguments
- `vararg_i32(index)` reads one packed `u32/i32` argument

The vararg block is an E compiler convention over existing local-byte memory.
EPA only sees ordinary `L_ALLOC`, `RLB_MOV4`, `LBR_MOV4`, and `CALL`
instructions.

## Unicode String Convention

E does not return strings directly. String-like values are explicit UTF-8 byte
spans:

```c
(offset, length)
```

Common string helpers live in:

```c
#include "common/string.em"
```

Formatting follows a two-pass/reference pattern:

- call a sizing function such as `unicode_format_len(...)`
- allocate caller-owned local bytes of that final size
- call an `_into(...)` function with `(out_off, out_cap, ...)`

Lengths are byte lengths. UTF-8 codepoint-aware behavior belongs in the common
module EPA bodies, not in the slim-core ISA.

## Kernel Identity

Every kernel must declare exactly one kernel identity inside the kernel block:

```c
kernel(VM vm) {
  kernalId("gameplay.rules");
}
```

Rules:

- `kernalId("...");` is required
- it is only valid inside the kernel block
- it may only appear once
- the string is hashed with FNV-1a and compiled into EPA as a 64-bit uid split into two `u32` values

Today, load-time duplicate uid collisions are hard failures.

## Kernel ACL

Cross-kernel ingress is controlled by a kernel-local ACL whitelist:

```c
acl {
  "scene.input" -> route_input;
  "player.avatar" -> apply_avatar_update;
}
```

Meaning:

- the left side is a remote kernel identity string
- the right side is a local worker name
- the compiler hashes the remote kernel string to the same 64-bit uid form used by `kernalId`
- EPA emits `ACL_ALLOW <lo> <hi> <local_wid>` manifest records

If a kernel omits `acl { ... }`, the compiler emits a warning.

## Worker Execution Pool

Workers are loaded as entry definitions, but the kernel controls when they enter
or leave execution:

```c
kernel(VM vm) {
  kernalId("scene.runtime");
  start_worker(scene_compile);
  stop_worker(scene_compile);
}
```

Rules:

- `start_worker(...)` emits `ENTRY_EXEC`
- `stop_worker(...)` emits `ENTRY_HALT`
- `retire_worker(...)` emits `ENTRY_RETIRE`
- `start_worker(...)` and `stop_worker(...)` are kernel-only
- the argument may be a worker name or numeric worker id
- `worker_start(...)` and `worker_stop(...)` are accepted aliases
- worker static state and queues remain allocated; this controls scheduler eligibility

`retire_worker(...)` is the permanent form for init-only workers. From the
kernel it accepts a worker name or id. From inside a worker, call it with no
arguments:

```c
worker init_assets(AssetSeed seed) {
  static AssetBlock block;

  static {
    rgm_publish("assets.main", block);
    retire_worker();
  }
}
```

Retired workers are removed from the scheduler pool. Future `ENTRY_EXEC`
requests for that worker are ignored for the current program load.

Privileged workers may mutate dynamic ACL routes at runtime:

```c
worker security_root(SecurityRequest request) {
  grant_kernel_route("elara.os.compositor", "elara.app.notes", 1);
  revoke_kernel_route("elara.os.compositor", "elara.app.notes", 1);
  revoke_kernel_routes("elara.os.compositor", "elara.app.notes");
}
```

These emit `ACL 1`, `ACL 2`, and `ACL 3`. `PID 1` returns the current PID;
`PID 2` retires the PID in `r0`. The VM host only accepts privileged external
ACL/PID mutations from authorized workers. Dynamic ACL routes are stored on the
target kernel and are checked alongside manifest `acl { ... }` routes.

`retire_kernel("kernel.id")` unloads an entire kernel by identity. The target
kernel is removed from the global kernel registry, all of its workers are
retired, its ingress/system queues are cleared, and its runtime status becomes
`unloaded`. The kernel object remains owned by the host/module until normal
destruction, so a running kernel can safely retire itself:

```c
kernel(VM vm) {
  kernalId("elara.boot");
  start_worker(init_services);
  retire_kernel("elara.boot");
}
```

## `far_signal`

`far_signal` now targets kernels by identity string at compile time:

```c
worker route_input(PlayerIntent intent) {
  local SceneEvent outbound;
  far_signal("scene.runtime", outbound);
}
```

Rules:

- `far_signal` is only valid inside workers
- the target kernel must be a string literal
- the payload must be a local declared custom-type value
- the compiler hashes the target string and emits the uid as two `u32` immediates in EPA register setup

## VM State Built-ins

Workers can inspect their own VM identity and the provenance of the most recent
ingress frame:

```c
int my_kernel_lo = current_kernel_uid_low();
int my_kernel_hi = current_kernel_uid_high();
int my_worker = current_worker_id();

int sender_kernel_lo = ingress_source_kernel_uid_low();
int sender_kernel_hi = ingress_source_kernel_uid_high();
int sender_worker = ingress_source_worker_id();
```

The shorter aliases `source_kernel_uid_low()`, `source_kernel_uid_high()`, and
`source_worker_id()` are also accepted. Host ingress reports all bits set for
both source kernel and source worker, which is the VM form of `-1/-1`.

The old pattern of filling a byte buffer with a target kernel id string is no longer the current path.

## EPA Manifest Records

The E compiler now emits kernel metadata at the top of EPA assembly:

```asm
KERNEL_ID <lo:u32> <hi:u32>
ACL_ALLOW <remote_lo:u32> <remote_hi:u32> <local_wid:u32>
```

These are program manifest records consumed at load time by the EPA runtime.

## Debugging Consequence

Because kernel identity and ACL are explicit and compiled into the manifest:

- debugger sessions can reason about kernels by stable uid
- cross-kernel delivery can fail loudly on missing identity or ACL mismatch
- generated IDE stubs must include `kernalId("...");` even before real ids are chosen
