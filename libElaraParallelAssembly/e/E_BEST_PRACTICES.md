# E Best Practices

This note captures practical guidance for writing `E` well with the current
language and runtime surface.

It is not a full language reference. The goal here is to make good design
choices repeatable, especially around storage, dynamics, byte blobs,
serialization, and worker boundaries.

See also:

- [CURRENT_E_LANGUAGE.md](/home/nyhl/workspace/elara.cpp/libElaraParallelAssembly/e/CURRENT_E_LANGUAGE.md)
- [GHS_STORAGE_MODEL.md](/home/nyhl/workspace/elara.cpp/libElaraParallelAssembly/e/GHS_STORAGE_MODEL.md)

## Core Principle

`E` works best when memory ownership is obvious and transfer is explicit.

That means:

- keep state local to the worker that owns it
- treat worker boundaries as serialization boundaries
- prefer narrow primitives that compose over broad magical features
- hide byte-layout details behind helper functions

Recent additions such as `%`, unary operators, dynamic slices, `local_load_i32`,
and dynamic serialization are useful because they make encapsulation easier
without turning `E` into a pointer language.

## Use The Storage Classes For One Job Each

- use scalar locals for ordinary `int` values and loop counters
- use `local` for typed staging objects and local byte-array work buffers
- use `static` for worker-owned persistent state
- use `dynamic` for worker-owned growable collections

Do not blur these jobs.

Bad pattern:

```c
worker cache_worker(CacheIngress input) {
  local CacheState state;
}
```

If `state` must survive wake cycles, it should be `static`, not `local`.

Good pattern:

```c
worker cache_worker(CacheIngress input) {
  static int root_id;
  static int entry_count;
}
```

## Prefer Worker Ownership Over Shared Memory

If something feels "global", it should usually be a dedicated worker rather
than a globally reachable object.

A native hashmap is the canonical example:

- one worker owns the hashmap dynamics
- other workers send `put`, `get`, `remove`, or `snapshot` requests
- replies come back as normal ingress payloads

This is the right model for `E` because it preserves:

- memory isolation
- explicit ownership
- deterministic mutation points
- easier debugging

## Use Dynamics As Worker-Local Data Structures

`dynamic` pools are strong for:

- growable fixed-size element arrays
- object pools
- trie nodes
- byte block stores

Treat them as worker-local implementation details.

Do not design around sharing a live dynamic pool across workers. Instead:

1. serialize it to a local blob
2. move the blob in an ingress payload
3. restore into the receiving worker's own dynamic state

## Prefer Blob Transfer At Boundaries

When data must cross a worker boundary, prefer a payload that contains:

- a `size`
- a trailing `byte[]` blob when variable-size ingress is required
- optional kind/version fields

Example:

```c
type SnapshotIngress(int kind, int size, byte[] data) {
  return kind;
}
```

This pattern works well because:

- the sender controls exactly what bytes are valid
- the receiver can stage the blob into an owned local copy
- the transfer does not leak internal dynamic memory across workers

Rules for flexible tails:

- only `byte[]` is allowed
- it must be the final field of the declared type
- the declared type must be instantiated as `local`
- treat the `size` field as the logical byte count; the local backing may be padded for alignment

Recommended pattern:

```c
type SnapshotIngress(int kind, int size, byte[] data) {
  return kind;
}

worker restore_worker(SnapshotIngress msg) {
  local SnapshotIngress staged;
  staged.kind = msg.kind;
  staged.size = msg.size;
  staged.data = msg.data;
}
```

That copy keeps the worker boundary explicit: ingress bytes are staged into
the current frame's local storage before higher-level restore logic runs.

## Use Dynamic Serialization As The Boundary Mechanism

The current generic tools are:

- `dynamic_serialized_size(pool)`
- `dynamic_serialize(pool, blob, start)`
- `dynamic_restore(pool, blob, start)`

Use them for generic pool snapshots.

Use higher-level wrappers for complex structures such as hashmaps, where the
serialized form also needs metadata such as:

- version
- root ordinal
- logical count
- offsets or lengths of subordinate pool snapshots

Guideline:

- keep the generic format simple
- keep structure-specific headers in the stdlib module that owns the structure

## Design Structured Accessors Over Raw Byte Arithmetic

`local_load_i32(local_ref, byte_offset)` and
`local_store_i32(local_ref, byte_offset, value)` are not a replacement for
arbitrary pointers.

They are a controlled way to:

- read structured fields from local byte spans
- write structured fields into local byte spans
- implement containers without exposing layout details everywhere

Best practice:

- use these primitives inside helper functions
- do not spread raw offsets throughout user code

Good:

```c
function int hashmap_u64_value_get(int node_id) {
  local byte[1028] node;
  node = hashmap_u64_nodes[node_id:node_id + 1];
  return local_load_i32(node, 1024);
}
```

Bad:

```c
int x = local_load_i32(blob, 412);
int y = local_load_i32(blob, 416);
```

If an offset matters, give it a named helper.

## Use Slices For Copying, Not Aliasing

The current dynamic slice model is intentionally copy-oriented:

- `pool[start:end]` copies bytes from a dynamic pool into local frame-backed storage
- it does not create a shared view into dynamic memory

That is good.

Use slices when you want:

- a local work buffer
- a local snapshot of one or more dynamic elements
- temporary manipulation without mutating the dynamic until you explicitly store back

Do not think of slices as borrowed references.

## Prefer Fixed-Size Internal Elements

`dynamic` pools are strongest when element size is fixed and known.

Examples:

- `byte[64]` value blocks
- `byte[1028]` trie nodes
- declared-type records with stable field layout

If you need variable-size logical payloads:

- store them inside a fixed-size block up to a limit, or
- split them into multiple fixed-size structures and keep the composition in a higher-level module

This keeps serialization, iteration, and restoration straightforward.

## Keep Hashmaps Service-Oriented

A native hashmap should usually be:

- a worker-owned `dynamic` node pool
- a worker-owned `dynamic` byte-value pool
- a message protocol for access

Do not expose the internal ordinals as a public distributed API.

Ordinals are an internal storage detail of the owning worker.

Expose:

- `put`
- `get`
- `contains`
- `remove`
- `snapshot`

Avoid exposing:

- raw node ordinals
- assumptions about internal trie layout
- assumptions about dynamic pool growth/shrink behavior

## Match Key Width To The Real Addressing Domain

If the true key space is 64-bit, use an 8-step bytewise trie, not a 32-step
256-bit trie.

Guideline:

- do not model more key width than the real system can address
- keep the key representation honest
- rename APIs when the representation changes

Good:

- `HashMapKey64`
- `HashMap64`
- `hashmap_u64_*`

Bad:

- keeping `u256` names for an 8-byte key path

## Let Primitives Improve Encapsulation

A primitive belongs in `E` when it makes library boundaries easier to maintain.

Good primitives:

- make container internals easier to hide
- reduce repeated raw EPA blocks
- improve local reasoning
- preserve explicit storage/lifetime rules

Suspicious primitives:

- bypass ownership
- make arbitrary memory reach-through easy
- create multiple overlapping ways to model the same data

The test is simple:

- if the primitive helps write a better module boundary, keep it
- if it encourages every caller to poke inside every structure, be careful

## Version Serialized Formats

Every non-trivial serialized blob should carry a version field.

Even if the first version is simple, plan for:

- layout changes
- count/header additions
- alternate value encodings

At minimum:

- version
- payload sizes
- structure-specific metadata

This makes later evolution much less painful.

## Keep Worker Boundaries Typed, Even If Payloads Are Blobs

Blob transfer should still be wrapped in declared types.

That keeps:

- routing explicit
- ingress meaning explicit
- future extension straightforward

Example:

```c
type HashMapSnapshotIngress(int version, int size, byte[4096] data) {
  return version;
}
```

This is better than treating every transfer as an untyped generic byte message.

## Prefer Small Stdlib Wrappers Over Repeated EPA Blocks

If a pattern appears more than once, move it into:

- `common/bytes.em`
- `common/hashmap.em`
- another focused stdlib module

This is especially true for:

- byte field access
- snapshot headers
- map node accessors
- serialization and restore helpers

The language feels easier when the low-level primitives are flexible, but that
ease only scales if common patterns are wrapped quickly.

## Practical Rules

- use `%` and unary operators normally; do not add new opcodes just because syntax became nicer
- use bit/byte helpers from `common/bytes.em` before dropping to raw `EPA { ... }`
- prefer named helper functions over open-coded byte offsets
- use slices to copy dynamic content into local buffers
- use serialization to cross worker boundaries
- keep dynamic pools private to the worker that owns them
- model global services as dedicated workers
- keep blobs versioned
- keep APIs honest about key width and structure shape

## Current Limits To Remember

Design within the current system, not the imagined one.

Important current constraints:

- dynamic pools are fixed-element-size collections
- slices are copy-based, not borrowed views
- worker boundaries should be treated as data-copy boundaries
- compile success is not the same as runtime proof; add VM tests for new storage patterns
- fixed-size byte payload fields are currently the most comfortable ingress transport shape

Those are not weaknesses by themselves. They are part of what keeps `E`
predictable.

## Bottom Line

Write `E` as a language of:

- explicit owners
- local byte workspaces
- worker-owned dynamic structures
- typed ingress protocols
- blob-based transfer when state must move

If a feature makes encapsulation easier while preserving those rules, it is
probably a good fit for `E`.
