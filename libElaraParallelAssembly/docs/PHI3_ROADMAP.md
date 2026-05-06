# EPA + Phi-3 Port — Status, Layouts, and Full Roadmap

This document is a **handoff-quality snapshot** of what has been achieved so far and the **exact next steps** required to complete the Phi‑3 port on EPA with minimal glue.

It is written so a fresh instance can pick up with **no missing context**.

---

## 1) Core principles locked in

### 1.1 Unidirectional pipelines
- Pipelines are **strictly unidirectional**.
- There is **one GHS allocation per pipeline run** (per StepFrame) — no complex sharing.
- Workers behave as **data transformers**.

### 1.2 GHS as the payload backbone
- The **GHS payload is the transformative object** moving through the system.
- Atomic Tasks (AT_) operate **directly on GHS memory**.
- EPA glue should avoid copying: ideally **payload stays in GHS** end-to-end.

### 1.3 Ownership and lifecycle
- GHS is designed for **ownership transfer** between kernel and workers.
- Rule planned: any path that emits through **SIGNAL mailbox** will **free GHS** (may be “sugared” later into a single OUT opcode).
- Final design intent: **verifiable data lifecycle**, suitable for aviation-style audit.

### 1.4 Concurrency model intent
- EPA is designed to **never block** except on **nothing-to-do** states.
- Future: avoid races by restricting where GHS allocation occurs (mostly ingress), and use worker-local scratch pads where appropriate.

---

## 2) Messaging layout and current primitives

### 2.1 Messaging layout (locked)
Inbound:
1. Inbound data enters via kernel (**ENTRY 0**).
2. Kernel allocates GHS handle, copies payload.
3. Kernel transfers ownership to worker with **G_XFER**.
4. Kernel notifies the worker via **ring buffer**.

Outbound:
1. Outbound results leave via per-entry **SIGNAL mailbox** to container.

Notes:
- GHS exists to pass ownership between workers.
- SIGNAL mailbox is used to push data directly to container.

### 2.2 Known previously-fixed bug
- Bug: workers mistakenly had **separate GHS instances**.
- Fix direction: **one GHS instance inside KernelImpl** (global, shared).

---

## 3) Unit test platform achieved

### 3.1 New C unit test platform
- A C-level unit testing runner exists under `./unittests`.
- Tests live under `./unittests/ctests`.
- The Makefile has been iterated to:
  - anchor paths from repo root
  - compile tests + selected core sources
  - avoid linking unrelated OpenGL backends
  - produce `build/unittests/ctest_runner`

### 3.2 Core module tests (PASS)
- Ring buffer tests
- GHS tests (alloc/free/transfer/double-free, handle roundtrip via ring)
- Stack tests

Performance note:
- GHS thrash test ran **10,000,000 iterations** extremely fast (≈185M it/s observed), implying **GHS won’t be a perf bottleneck**.

---

## 4) Atomic Tasks framework achieved

### 4.1 Updated AT definition (locked)
- Atomic Tasks are named `AT_*`.
- AT represents a **large matrix/NN compute operation**.
- AT call signature: **two input registers (R0,R1) form the GHS handle**; AT receives `(ghs, handle)`.
- AT reads/writes **only inside the GHS payload**.

### 4.2 Weights and KV cache rule (locked)
- Static weights: placed in **global read-only memory** (model-loaded).
- KV cache: **part of the payload**, lives entirely in **GHS**.
- Pipeline uses **one GHS allocation** per run.

### 4.3 AT ordering decision (locked)
- Attention path uses **Option A** for readability:
  - `AT_RMSNORM_QKV_ROPE_FUSED`
  - `AT_KV_CACHE_APPEND`
  - `AT_FLASH_ATTN`

---

## 5) Current AT coverage (implemented + unit-tested)

The following ATs are implemented with CPU reference versions and unit tests, and are currently **PASS** in `ctest_runner` output.

### 5.1 Implemented ATs (PASS)
- `AT_EMBED_GATHER`
- `AT_FLASH_ATTN`
- `AT_KV_CACHE_APPEND`
- `AT_LINEAR_O`
- `AT_MLP_FUSED`
- `AT_LM_HEAD_LINEAR`
- `AT_RESIDUAL_ADD`
- `AT_RMSNORM_QKV_ROPE_FUSED`
- `AT_SAMPLE_RNG`
- `AT_TOPK_TOPP_FILTER`

### 5.2 RMSNorm final
- `AT_RMSNORM_FINAL` implementation exists (CPU reference).
- Confirmed desired: **final RMSNorm before LM head**.

### 5.3 Current ctest runner snapshot (all pass)
- ring_basic
- ghs: alloc/free, xfer ownership, double free, handle roundtrip ring, thrash test, transfer preserves payload
- stack_push_pop
- all AT tests above

---

## 6) Phi‑3 pipeline flow (locked)

### 6.1 Decode step (single token)
1. `AT_EMBED_GATHER`
2. For each transformer layer:
   1. `AT_RMSNORM_QKV_ROPE_FUSED`
   2. `AT_KV_CACHE_APPEND`  ✅ explicit
   3. `AT_FLASH_ATTN`
   4. `AT_LINEAR_O`
   5. `AT_RESIDUAL_ADD` (x += o)
   6. `AT_MLP_FUSED`
   7. `AT_RESIDUAL_ADD` (x += mlp)
3. `AT_RMSNORM_FINAL`
4. `AT_LM_HEAD_LINEAR` → logits
5. `AT_TOPK_TOPP_FILTER`
6. `AT_SAMPLE_RNG` → next_token
7. Emit via SIGNAL (future: sugar into OUT opcode that also frees GHS)

### 6.2 Prefill step (prompt chunk)
Same flow, but with `n_rows = seq_len` and attention in causal prompt mode. KV append appends **seq_len** positions.

---

## 7) What is *not* done yet (the remaining “system work”)

### 7.1 Loader is not implemented yet (next priority)
- Glue without loader is “poking in the dark” (explicitly agreed).
- Requirement: PyTorch naming 1:1, least friction, 4-week core framework.

### 7.2 Chosen loader approach (least friction)
- Use **HuggingFace-style safetensors (+ index JSON)** while preserving **exact PyTorch/HF tensor names** 1:1.
- No `.pt` pickle parsing in C.
- Use mmap where possible; no packing initially.

### 7.3 Phi‑3 step runner / glue not implemented yet
- StepFrame layout exists conceptually but needs a concrete v1 header and runtime.
- EPA opcode bridge to call ATs in the right order still pending.

---

# 8) Roadmap — detailed to the last nut and bolt (4-week target)

The objective is: **Phi‑3 CPU reference runs end-to-end via AT pipeline, then minimal EPA glue, then optional CUDA backends later.**

## Week 1 — Loader foundations (format + manifest + resolver)

### 8.1 Create format reader: safetensors
Files:
- `src/format/safetensors_reader.h`
- `src/format/safetensors_reader.c`

Responsibilities:
- `open(path)` → mmap file, keep fd/mapping
- parse header (JSON block) to locate tensor metadata
- provide lookup:
  - `get_tensor(key)` → {ptr, dtype, shape[], ndim, nbytes}
- error handling: missing key, invalid header, bounds checks

Implementation details:
- Use `mmap` for file mapping.
- Parse the safetensors header JSON minimally (only what you need):
  - tensor name → {dtype, shape, data_offsets}
- Store metadata in a simple hash map or linear array + binary search by key.

Unit tests:
- Add a tiny safetensors fixture generator OR ship a minimal test file.
- Verify:
  - opening file
  - reading known tensor
  - bounds validity

### 8.2 Add shard index resolver (recommended)
Files:
- `src/format/safetensors_index.h`
- `src/format/safetensors_index.c`

Responsibilities:
- Load `model.safetensors.index.json`
- Map key → shard filename
- Provide `find_shard(key)`

Notes:
- Implement a minimal JSON parser or a constrained parser specific to the index format.

### 8.3 Define Phi‑3 weight manifest (contract)
File:
- `src/phi3/phi3_weight_manifest.h`

Responsibilities:
- List required tensor keys exactly (HF naming)
- Define expected dimensional rules and allowed dtypes
- Provide helpers:
  - format key for layer i
  - validate shape matches expected

### 8.4 Create Phi‑3 weight resolver
Files:
- `src/phi3/phi3_weight_resolver.h`
- `src/phi3/phi3_weight_resolver.c`

Responsibilities:
- Discover whether checkpoint is single file or sharded
- Open required shards, cache readers
- Resolve required weights into `TensorView` pointers
- Validate shapes/dtypes
- Derive model config:
  - n_layers, d_model, head_dim, n_heads, n_kv_heads, vocab, intermediate

Outputs:
- `Phi3Weights` struct holding pointers/views

Unit tests:
- Resolver test with a small synthetic checkpoint (tiny dims) OR fixture.

Deliverable end of Week 1:
- A program can print:
  - model config
  - pointer addresses and shapes for each required tensor

---

## Week 2 — StepFrame v1 + CPU step runner

### 8.5 Define StepFrame v1 payload layout
File:
- `src/phi3/phi3_step_frame_v1.h`

Responsibilities:
- Define a packed header with:
  - magic/version
  - mode (prefill/decode)
  - dims (d_model, heads, head_dim, etc.)
  - token(s) input area offsets
  - x, scratch, qkv buffers, kv cache offsets
  - logits/probs output offsets
  - rng state offset
  - layer loop bookkeeping offsets

Rule:
- All AT inputs/outputs are offsets inside this single payload.

### 8.6 Create StepFrame builder
Files:
- `src/phi3/phi3_step_frame_build.c/.h`

Responsibilities:
- Given model config + requested seq_len:
  - compute required bytes
  - allocate GHS (owner = kernel)
  - fill header and zero required areas
  - set initial token ids

### 8.7 Implement Phi‑3 CPU step runner calling ATs
Files:
- `src/phi3/phi3_step_runner.c/.h`

Responsibilities:
- Execute flow:
  - embed
  - layer loop with explicit kv append
  - final rmsnorm
  - lm head
  - filter
  - sample
- Provide two entrypoints:
  - `phi3_prefill_run(ghs, h, weights)`
  - `phi3_decode_run(ghs, h, weights)`

Notes:
- Runner calls AT functions directly (CPU reference) initially.
- Weights are passed via global readonly table or `Phi3Weights*`.

Unit tests:
- Tiny model sanity test:
  - deterministic weights
  - known tokens
  - verify output token matches expected

Deliverable end of Week 2:
- CPU-only Phi‑3 tiny model can run prefill+decode.

---

## Week 3 — Parity + KV correctness + performance sanity

### 8.8 KV correctness tests
- Create tests to ensure:
  - `AT_KV_CACHE_APPEND` appends at correct position
  - cache grows correctly across steps
  - attention reads correct cache range

### 8.9 Prefill then decode integration tests
- Feed prompt of length N
- Sample next token
- Continue decode for M steps
- Verify:
  - no memory overwrite
  - deterministic behavior under fixed RNG seed

### 8.10 Shape/stride robustness
- Ensure loader supports:
  - fp16/bf16/fp32 (convert as needed or restrict v1)
  - contiguous assumptions clearly documented

Deliverable end of Week 3:
- CPU Phi‑3 pipeline is stable and test-covered.

---

## Week 4 — EPA glue (minimal) + pipeline integration

### 8.11 Implement an EPA worker pipeline wrapper
Files:
- `src/phi3/phi3_epa_worker.c/.h`

Responsibilities:
- Worker receives a GHS handle via ingress ring
- Worker runs `phi3_*_run` step runner
- Worker emits next token via SIGNAL mailbox
- Worker frees GHS per rule (or via OUT helper)

### 8.12 Implement opcode bridge (minimal)
- Add opcode(s) that route to:
  - StepFrame build
  - invoke phi3 worker
- Keep opcodes small; AT heavy lifting stays inside AT subsystem.

### 8.13 Integrate WAIT_NEXT paradigm
- Middleman workers never talk to kernel directly.
- Define a routing header format in the payload:
  - next worker id
  - offsets mapping
  - ready flags

Deliverable end of Week 4:
- EPA can run Phi‑3 step pipeline end-to-end, using the same AT contract.

---

# 9) Known future enhancements (explicitly deferred)

- CUDA implementations of AT_* (behind same interfaces)
- Weight packing / fusion for hardware layout
- OUT opcode sugar: SIGNAL + GHS free bundled
- Multi-expert container format (experts as separate files for now)
- Bank emulator (3-bank RAID/swap model) + later VHDL modelling
- Elara silicon lane-junction switcher concepts (not designed now; notes only)

---

# 10) “Resume checklist” for a new instance

When resuming work:
1. Confirm repository layout:
   - `src/memory/*` for GHS/ring/stack
   - `src/atomic_tasks/*` for AT implementations
   - `unittests/ctests/*` for unit tests
2. Confirm `ctest_runner` passes.
3. Start loader implementation:
   - `safetensors_reader`
   - (optional) `safetensors_index`
   - `phi3_weight_manifest`
   - `phi3_weight_resolver`
4. Create StepFrame v1 header and builder.
5. Implement phi3 step runner calling ATs.
6. Only then do EPA opcode glue.

---

## Appendix A — AT ordering (explicit Option A)
- `AT_RMSNORM_QKV_ROPE_FUSED`
- `AT_KV_CACHE_APPEND`
- `AT_FLASH_ATTN`

## Appendix B — Design guardrails
- Avoid linking OpenGL/GLFW in unit tests.
- Keep ATs deterministic and payload-only.
- Keep payload headers versioned and self-describing.

---

End of snapshot.
