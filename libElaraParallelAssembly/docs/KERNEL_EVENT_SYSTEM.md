# Kernel Event System & Tiered Execution Model

## Purpose

This document defines the kernel event system used by EPA and its interaction
with a multi-tier execution topology.

The goal is to allow GPU-resident kernels to run autonomously at full speed,
while higher tiers react to kernel output and cognition scales from a single GPU
to WAN clusters.

This is a reactive, event-driven system, not a polling or tick-based scheduler.

---

## Overview: Tiered Topology

Execution is organized into four tiers:

Tier 1 — Individual GPU Kernel  
Tier 2 — Single-Machine Cluster Master  
Tier 3 — LAN Cluster  
Tier 4 — WAN Cluster  

Each tier executes independently and communicates only via explicit event payloads.
All interaction is neighbor-to-neighbor.

---

## Tier Responsibilities

### Tier 1: GPU Kernel (EPA Kernel)

- Runs fully on GPU (CUDA)
- Schedules and executes workers (experts)
- Maintains local execution state
- Produces compressed semantic outputs
- Requests resources or data when needed
- Never blocks on IO or networking

Latency focus: nanoseconds

---

### Tier 2: Single-Machine Cluster Master

- Owns one or more GPUs
- Reacts to kernel events
- Decides next kernel invocation parameters
- Routes data between GPUs
- Manages local caching and promotion
- Interfaces with storage or Tier 3

Latency focus: microseconds to milliseconds

---

### Tier 3: LAN Cluster

- Aggregates multiple machines
- Maintains broader context
- Performs cross-node synthesis
- Scores and caches results
- Publishes refined representations upward

---

### Tier 4: WAN Cluster

- Global coordination
- Long-horizon synthesis
- Large-scale knowledge persistence
- Very high capacity, high latency storage

---

## Kernel Event System (Tier 1 → Tier 2)

Each CUDA kernel invocation returns exactly one shared structure describing its
outcome and future needs.

This structure is the only contract between Tier 1 and Tier 2.

### Kernel Event Record (Conceptual)

- Event kind (RUN, IDLE, NEED_DATA, NEED_SHARD, HALT, etc.)
- Desired thread count for next invocation
- Scheduling and priority hints
- Outgoing compressed payloads for Tier 2

The record is declarative and bounded in size.

---

## Run-to-Event Execution Model

A kernel invocation may execute one instruction or millions.
Execution continues until an event boundary is reached.

Event boundaries include:
- External data required
- Shard loading required
- No runnable workers
- Explicit halt request

There is no fixed tick rate.

---

## Shared State Across Kernel Invocations

The kernel maintains persistent shared state including:
- Worker instruction pointers
- Registers and stacks
- Local heaps
- Routing queues
- Execution metadata

This enables deterministic pause and resume across kernel launches.

---

## Data Flow Between Tiers

### Upward Flow (Refinement → Generalization)

Lower tiers emit refinement payloads:
- Compressed semantic representations
- Partial conclusions
- Local scores

Higher tiers aggregate, cache, score, and generalize these outputs.

---

### Downward Flow (Generalization → Refinement)

Higher tiers emit:
- Abstract goals
- Generalized hypotheses
- Requests for detail

Lower tiers refine these inputs and return scored results upward.

---

## Thought Nucleation & Scoring

Each refinement output includes a relevance score.
Higher tiers cache and track score evolution to decide further investment,
promotion, or eviction.

---

## Sleep & Reactivation

If a kernel has no runnable work:
- It emits an IDLE event
- Control returns to the host
- GPU enters sleep

Reactivation occurs only upon new events or data arrival.
There is no busy waiting.

---

## Core Invariants

1. Kernels never block on external resources  
2. All tier transitions are event-driven  
3. All payloads are compressed representations  
4. No implicit shared memory across tiers  
5. Resume occurs only at instruction boundaries  
6. Higher tiers generalize, lower tiers refine  

---

End of document.