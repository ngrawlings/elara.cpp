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

## Privileged Worker Demo

`epa/privileged_workers.e` exercises sealed boot-time privilege grants:

- The kernel `static {}` block grants `security_root` privilege level `100`.
- The compiler emits `ENTRY_PRIVILEGE` followed by `PRIVILEGE_LOCK`.
- Normal kernel execution starts the privileged worker only after the privilege table is sealed.

Build the EPA assembly:

```sh
../../libElaraParallelAssembly/build/e/e2epa epa/privileged_workers.e build/privileged_workers.epaasm
```

The executable test fixture lives at `tests/privileged_workers/epa/entry.e` so the
privilege grant is performed by the first loaded entry kernel's `wid=0` static
block, matching the OS boot rule.

## Dynamic Whitelist Demo

`epa/dynamic_whitelist.e` exercises dynamic ACL mutation from a privileged worker:

- The kernel grants `security_root` privilege level `100` during the sealed static phase.
- `security_root` emits grant/revoke calls for `e.examples.dynamic_whitelist.app` into the target kernel.
- `epa/dynamic_whitelist_target.e` is the closed target kernel whose static ACL does not admit the app.
- `epa/dynamic_whitelist_app.e` is the app kernel that becomes reachable only after a dynamic grant.
- The generated EPA includes `ACL 1`, `ACL 2`, and `ACL 3`.

Build the EPA assembly:

```sh
../../libElaraParallelAssembly/build/e/e2epa epa/dynamic_whitelist.e build/dynamic_whitelist.epaasm
../../libElaraParallelAssembly/build/e/e2epa epa/dynamic_whitelist_target.e build/dynamic_whitelist_target.epaasm
../../libElaraParallelAssembly/build/e/e2epa epa/dynamic_whitelist_app.e build/dynamic_whitelist_app.epaasm
```

Run the CPU privilege and whitelist checks:

```sh
bash tests/run_privilege_acl_tests.sh
```

## Process Cluster PID Demo

`tests/process_cluster/epa` contains a tiny two-kernel app bundle used to test
PID-prefixed child kernel clusters. The runtime loads the same EPA bundle under
multiple requested PID handles, derives unique global kernel UIDs for each PID,
and allows a child to retire itself while requiring privilege to retire another
PID.

Run the CPU process cluster checks:

```sh
bash tests/run_process_cluster_tests.sh
```
