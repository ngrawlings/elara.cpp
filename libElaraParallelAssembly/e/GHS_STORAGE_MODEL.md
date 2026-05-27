# E Storage Model

This note defines the current storage model for `E`.

The current language also requires each kernel to declare:

- `kernalId("...");` inside the kernel block
- an optional `acl { ... }` whitelist for cross-kernel ingress

See [CURRENT_E_LANGUAGE.md](/home/nyhl/workspace/elara.cpp/libElaraParallelAssembly/e/CURRENT_E_LANGUAGE.md) for the current identity and routing rules.

It is important because `E` is strict on purpose. If storage intent is vague, worker behavior becomes unstable, payload ownership becomes ambiguous, and the debugger becomes much harder to trust.

The current model has three explicit storage classes:

- `reg`
- default scalar locals
- `local`
- `static`

`static` is the important addition for consistent worker state. It exists so worker state that must survive wake cycles has an explicit, durable place to live.

## Core Rule

Use each storage class for one job only:

- `reg`
  - tiny fast scalar scratch
  - short-lived
  - intentionally limited

- default scalar locals
  - ordinary scalar function/worker locals
  - VM-local word storage
  - convenient for values that do not need typed object semantics

- `local`
  - typed payload/object staging
  - local byte arena backed
  - best for outbound typed payload construction and transient typed work objects

- `static`
  - persistent worker state
  - initialized once before the worker wait loop
  - survives across wake cycles
  - should be the default choice for stateful worker memory that must remain consistent over time

If a worker needs memory to remember where it is between activations, that memory should usually be `static`.

That point is important enough to say plainly:

- do not fake persistent worker state with ad hoc ingress assumptions
- do not rebuild stable worker state every wake unless the design truly calls for it
- use `static` when the worker has identity and continuity

## What `static` Is

`static` in `E` is not process-global state and it is not kernel-global shared state.

It is:

- persistent per worker instance
- private to that worker instance
- available only inside workers
- zero-initialized once before the worker wait loop

So this:

```c
worker player_avatar(KeyInput input) {
  static int x;
  static int y;
  static int facing;
}
```

means:

- `x`, `y`, and `facing` belong to that worker
- they persist across `WAIT_FOR_DATA` wake cycles
- they are the correct place for consistent worker-owned gameplay state

This is the intended role of `static`.

## What `static` Is Not

`static` is not:

- a substitute for `local` typed payload staging
- a shared inter-worker mailbox
- a cross-kernel state bus
- host-owned persistent state

If you need to send a typed payload outward, use `local` typed objects and explicit signaling.

If you need a worker to remember its own stable state, use `static`.

Do not blur those two jobs.

## Worker Parameter Model

A worker still receives one typed ingress object.

- the runtime carries the underlying GHS handle
- the worker parameter type is the typed view over that ingress

That inbound typed view is not the same thing as persistent worker state.

Inbound GHS:

- represents what arrived this wake
- should be treated as the current ingress payload

`static`:

- represents what the worker already knows and retains

That separation is healthy and should stay explicit.

## `local` Versus `static`

This distinction matters for engine work.

Use `local` when you are building a typed payload or temporary typed work object:

```c
kernel(VM vm) {
  kernalId("player.input");
}

acl {
  "gameplay.rules" -> player_input;
}

worker player_input(KeyInput input) {
  local PlayerIntent intent;
  intent.move_x = input.key_code;
  far_signal("gameplay.rules", intent);
}
```

Use `static` when you are maintaining worker-owned state:

```c
kernel(VM vm) {
  kernalId("player.avatar");
}

worker player_avatar(KeyInput input) {
  static int x;
  static int z;
  static int health;

  if (input.key_code == 65361) {
    x = x - 24;
  }
}
```

That is the intended split:

- `local` for typed staging and transfer
- `static` for continuity

## Declared-Type `static`

`static` may also be a declared type:

```c
type Pose(int x, int y) {
  return x;
}

worker scene_worker(Input input) {
  static Pose pose;
}
```

Semantics:

- the declared-type storage is allocated once from the worker local byte heap
- the reference is stored in persistent worker VM locals
- the allocation is zero-filled once before the worker loop
- the typed object then persists for the lifetime of the worker instance

This is useful when a worker needs a persistent structured state object rather than a few scalar slots.

## Restrictions

Current restrictions are intentional:

- `static` is only allowed inside a worker
- `static` cannot be combined with `reg`
- `static` cannot be combined with `local`
- `static` cannot be an array
- `static` cannot currently have an initializer
- statics are zero-initialized

Those restrictions keep the semantics tight while the language/runtime matures.

## Why `static` Matters

Without `static`, stateful workers tend to decay into one of two bad patterns:

- rebuilding state every wake from ingress
- hiding persistent assumptions in ad hoc runtime behavior

Both are bad for:

- correctness
- readability
- debugging
- generated templates
- future GPU backend predictability

`static` gives the language an explicit place for durable worker identity.

That is why it matters.

For EPA specifically, this improves:

- consistent worker state across wake cycles
- clearer separation between ingress payload and owned state
- easier debugger interpretation
- better template generation for stateful worker patterns
- cleaner future GPU/device execution semantics

## Practical Guidance

When writing `E`, ask:

1. Is this value just a short-lived scalar?
   Use default scalar local or `reg`.

2. Am I constructing a typed outbound payload or temporary typed object?
   Use `local`.

3. Must this value still exist and still mean the same thing on the next worker wake?
   Use `static`.

That third question is the one to remember.

If the answer is yes, `static` is probably the correct storage class.
