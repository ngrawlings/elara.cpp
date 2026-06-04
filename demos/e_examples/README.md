# E Examples

Small E programs for exercising VM and compiler features.

## Multiplication Table AT Demo

`epa/entry.e` builds a 100 by 100 multiplication table through an AT entry:

- 10,000 virtual tasks: one task per table cell.
- 16 real CPU threads: the CPU dispatcher partitions the virtual task ids across these host threads.
- A 10,000-int local initializer is filled in `lbytes` first, then copied into final-size GHS with `ghs_alloc_from_local`.
- The AT body receives `thread_index`, computes row/column/value, and writes the result into a shared GHS table body.

Build the EPA assembly:

```sh
../../libElaraParallelAssembly/build/e/e2epa epa/entry.e build/entry.epaasm
```

## Dynamic Memory Demo

`epa/dynamic_memory.e` exercises the hidden dynamic pool growth path:

- `DynamicCell` objects are allocated through an E `dynamic` pool.
- The pool starts with no backing slots and grows in batches of 4.
- The worker allocates 40 cells, forcing multiple runtime capacity requests under the hood.
- E does not request or resize memory directly; `DYN_ALLOC` triggers the system memory ring when the pool needs more backing capacity.

Build the EPA assembly:

```sh
../../libElaraParallelAssembly/build/e/e2epa epa/dynamic_memory.e build/dynamic_memory.epaasm
```
