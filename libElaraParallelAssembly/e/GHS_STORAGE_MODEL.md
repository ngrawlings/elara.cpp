# GHS Storage Model For E

This note defines the current storage direction for `E`.

## Rule

For the first usable `E -> EPA` path:

- every operation instance is backed by one singular GHS allocation
- the pipeline lifecycle creates that GHS
- the worker receives a typed view of that GHS
- the worker forwards the same GHS handle through the EPA path
- local scope data lives inside that same GHS block

There is no separate local stack model in `E` at this stage.

## Type Model

A custom `type` is not an object with independent allocation semantics.
It is a named map of offsets on a GHS-backed region.

For workers, this is the important rule:

- a worker does not take raw `GHS`
- a worker takes exactly one custom `type`
- that custom `type` is the named layout associated with the underlying GHS block

Example:

```c
type Packet(Packet header, int z) {
  // validator body
}
```

This means `Packet` is compiled as:

- a validator id / validator entry
- a deterministic GHS layout
- named fields that map to offsets within the `Packet` GHS span

## Function Frame Model

Each function invocation receives a singular operation GHS.

For now:

- parameters are assigned deterministic offsets into that GHS
- primitive parameters consume fixed byte spans
- custom parameters consume the byte span of their type layout
- local variables will later be assigned offsets inside the same GHS

Worker entry is slightly different:

- worker entry receives one typed operation block
- the runtime still passes an underlying GHS handle
- the compiler treats the worker parameter type as the layout/view for that handle

This keeps the model linear and explicit.

## Why This Fits EPA

- one handle travels through the pipeline
- workers do not need ad hoc object allocation for locals
- type identity becomes layout identity plus validator routing
- branch/template lowering can stay independent of storage allocation
- mesh/kernel ownership remains outside the worker

## Immediate Compiler Consequences

- type declarations must produce a GHS field layout map
- function lowering must produce a single frame layout
- variable references later become offset loads/stores into the frame GHS
- validator dispatch carries the same GHS handle and a validator id

## Deferred

- alignment rules
- padding rules
- nested custom type recursion limits
- self-referential custom layouts such as `type Packet(Packet p, ...)`
- local-variable lifetime reuse inside one GHS
- exact EPA instruction sequence for GHS field access
