# 🌌 Elara Stack Overview

This document defines the architectural separation and responsibilities
of the Elara ecosystem.

It is a design contract, not an implementation detail.

---

## 1. High-Level Architecture

Elara is composed of two primary layers:

ElaraScript (Host)
- Orchestration
- Services
- Persistence
- Scheduling
- Networking
- Tooling

EPA (GPU-Resident Kernel)
- Massive Parallelism
- Determinism
- AI Workers
- Message Passing

---

## 2. EPA: GPU-Resident AI Kernel

EPA is a microkernel / ISA designed to live 100% on the GPU.

It is optimized for:
- AI workloads
- massive parallelism
- deterministic execution
- minimal dependency scope

EPA does not provide:
- threads
- mutexes
- shared memory mutation
- syscalls
- POSIX abstractions

---

## 3. Execution Model

Kernel:
- Owns global state
- Schedules workers
- Performs coordination
- Declares a stable kernel identity via the E-level `kernalId("...")` contract

Workers:
- Isolated compute units
- No shared mutable state
- Communicate via ring buffers and SYNC

This is an actor-style concurrency model.

---

## 4. Memory Model

EPA defines explicit memory classes:

Registers (Worker)
- Ephemeral temporary values

Local Words (Worker)
- Variables and persistent state

Local Byte Heap (Worker)
- Strings, formatting buffers, temporary data

Global Handles (Kernel)
- Shared resources with explicit ownership

DATA_BLOCK (Program)
- Immutable constants

No implicit allocation exists.

---

## 5. Service Protocols

Formatting, logging, serialization, crypto, tensor helpers, and similar
high-level services are not slim-core opcodes by default.

They should be implemented:
- as common E modules
- as EPA library functions using `EPA { ... }` blocks
- as AT requests when parallel execution is appropriate
- as standardized ingress/egress protocols

They never mutate program semantics.

---

## 6. ElaraScript: Host Layer

ElaraScript runs on the CPU / host environment.

Responsibilities:
- EPA blob construction
- Worker scheduling
- Global state management
- Decentralized services
- IO, persistence, networking
- Debugging and tooling

EPA never depends on ElaraScript at runtime.

---

## 7. Design Invariants

1. No traditional threading primitives in EPA
2. Workers never mutate shared state
3. Synchronization is explicit
4. EPA is GPU-first
5. ElaraScript owns host concerns
6. Execution is deterministic
7. Cross-kernel routing is identity- and ACL-driven

---

## 8. Long-Term Vision

- EPA kernels run fully on GPU
- Future mapping to custom silicon
- ElaraScript orchestrates distributed AI systems
- AI compute becomes reproducible and inspectable

## Current E/EPA Evolution

The language/runtime boundary has recently become more explicit:

- E kernels must declare `kernalId("...");`
- E may declare `acl { "remote.kernel" -> local_worker; }`
- EPA receives those as `KERNEL_ID` and `ACL_ALLOW` manifest records
- cross-kernel `far_signal` no longer depends on runtime string assembly

End of document.
