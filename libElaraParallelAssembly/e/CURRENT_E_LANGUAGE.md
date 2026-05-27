# Current E Language Surface

This note captures the current `E` language rules that matter when writing code by hand, generating templates, or wiring the IDE debugger.

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
