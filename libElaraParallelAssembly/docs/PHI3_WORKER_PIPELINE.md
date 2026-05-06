# EPA Worker Assembly + AtomicTasks Blueprint (Phi-3 decode-oriented)
*Draft / living document — intended to be amended as EPA primitives harden.*

## 0. Purpose

This document describes a **proposed** end-to-end execution pipeline for running a Phi‑3–style decoder-only transformer on EPA, focusing on:

- **Worker assembly responsibilities** (EPA op-blocks)
- The role and ABI of **WAIT_NEXT / NEXT / OUT**
- **GHS packet** conventions (payload + padding/scratch)
- A catalog of **AtomicTasks** (CUDA subroutines) and what each does
- How the pipeline can support **multi-token parallelism** later

**Design constraints (current EPA direction):**
- **No blocking** in workers other than “nothing to do” waiting (`WAIT_NEXT`).
- **Kernel allocates GHS only at ingress**; workers do not allocate.
- **Single global GHS** owned by the kernel implementation; workers reference it.
- **Ownership is explicit** and moves with `NEXT` (or kernel ingress delivery).
- **OUT** should eventually free GHS automatically (sugared rule).

---

## 1. Glossary

- **GHS**: Global Handle Space; global allocation/ownership of payload buffers.
- **GHS handle**: 64-bit `(gen<<32)|idx` passed across workers via ring.
- **Packet**: A GHS-resident buffer holding (a) header + (b) tensors + (c) scratch.
- **Routing header**: Variable-length envelope used to multiplex “logical workers”.
- **AtomicTask**: A GPU/CUDA subroutine launched/managed by EPA that performs
  compute-heavy tensor ops (GEMMs, fused kernels, FlashAttention, etc.).
- **Worker op-block**: A small, maintainable EPA assembly block orchestrating
  tasks and moving the packet to the next stage.

---

## 2. High-level pipeline for Phi‑3 (decode)

### 2.1 Decode-step pipeline (conceptual)

1. **Ingress** (kernel): allocate packet in GHS, copy in request/tokens, set owner,
   push ring entry → wake worker.
2. **Worker Stage 0**: Embed + initial pack.
3. **Layer Groups**: repeated blocks of Attention then MLP (grouped for amortization).
4. **Final Norm**
5. **Logits**
6. **Sampler** (optional GPU or CPU; for now can emit logits/token ids)
7. **OUT**: emit results to SIGNAL mailbox and free packet (sugared).

### 2.2 Two scheduling patterns (choose later)

**Pattern A (monolithic per request):**
- One worker loops over all layers using AtomicTasks, minimal inter-worker routing.

**Pattern B (pipeline / layer-groups):**
- Layers divided into groups; each group implemented as a stage:
  - `ATTN_GROUP(i)` then `MLP_GROUP(i)` then `ATTN_GROUP(i+1)` ...
- `NEXT` routes to the next physical worker (or hashed dispatch) and fills offsets.

This document assumes **Pattern B** because it matches the emerging WAIT_NEXT/NEXT
design and allows future parallelism experiments.

---

## 3. Packet (GHS) layout and capacity rules

### 3.1 Invariants

Each packet has:
- `payload_len`: valid bytes of current “meaningful” data (header + tensors)
- `cap_len`: total allocated capacity (payload_len + padding/scratch)
- Scratch / growth region is `[payload_len, cap_len)`

Workers:
- May *read* meaningful input within `[0, payload_len)`
- May use scratch within `[payload_len, cap_len)` as workspace
- Must never write beyond `cap_len`

### 3.2 Proposed packet sections (logical)

```
+-----------------------------+  offset 0
| Routing Header (var words)  |
+-----------------------------+  hdr_end (aligned)
| Packet Descriptor (fixed)   |  e.g., dims, dtypes, offsets, kv handles
+-----------------------------+
| Tensor Regions              |
| - X (activations)           |
| - optional QKV / temp       |
| - optional logits buffer    |
+-----------------------------+
| Scratch / Workspace         |  [payload_len, cap_len)
+-----------------------------+
```

The precise schema will be designed later; the key is the **offset-driven model**.

### 3.3 GHS meta
Recommended meta fields in GHS:
- `owner`
- `flags` (IN_USE, etc.)
- `payload_len`
- `cap_len`
- `type` (BYTES / etc.)
- optionally generation stored in handle only (current design)

---

## 4. Ring message + WAIT_NEXT ABI

### 4.1 Goal
When a worker wakes, it should already have:
- the **handle** and key offsets/lengths in registers (no parsing overhead).

### 4.2 Suggested register ABI (draft)
A “wide” variant of WAIT fills r0–r7.

- `r0` = handle_lo (idx)
- `r1` = handle_hi (gen)
- `r2` = payload_off (bytes)  (often 0)
- `r3` = payload_len (bytes)
- `r4` = cap_len (bytes)
- `r5` = hdr_len_words (or bytes)
- `r6` = route_key (logical worker / stream id)
- `r7` = subtype / pipeline id

**Variants (optional):**
- `WAIT_NEXT4` fills r0–r3
- `WAIT_NEXT8` fills r0–r7

### 4.3 NEXT responsibilities (kernel-free)
`NEXT` (or a helper called by `NEXT`) must:
- set GHS ownership to the **target physical worker**
- write/update routing header (variable-length) as needed
- compute and write next-stage offsets/len/cap mapping
- enqueue ring entry to target worker and wake it if idle
- return “would block” if target queue is full (no blocking)

---

## 5. OUT (sugared) rule

### 5.1 Contract
`OUT_GHS` (future sugar) does:
1. Copy requested bytes (or words) from packet to SIGNAL mailbox
2. Publish mailbox count/index safely
3. **Free GHS handle** (always) on success

If direct `SM_PUT` is used, it is considered “unusual code”; OUT_GHS is the norm.

---

## 6. Worker assembly: stage templates

Below, “EPAASM” is illustrative pseudocode (exact mnemonics may differ). The intent
is to keep each block **small**: validate → set params → launch tasks → NEXT/OUT.

### 6.1 Common prologue for any worker stage

```asm
STAGE_LOOP:
  ; Wait for next packet (worker-only wait)
  WAIT_NEXT8              ; fills r0..r7 (handle lo/hi, off/len/cap, route/subtype)

  ; Optional: validate meta (owner, bounds)
  ; GHS_VALIDATE r0 r1 -> (ok? or trap)
```

### 6.2 Stage pattern: compute then NEXT

```asm
  ; Launch one or more AtomicTasks (CUDA)
  ; TASK_LAUNCH task_id, param_ptr_off, param_len, scratch_off, scratch_len ...
  ATOMIC_TASK <TASK_ID>

  ; Handoff packet to next stage
  NEXT <target_spec>      ; sets owner, writes routing header, maps offsets, wakes target
  JMP STAGE_LOOP
```

### 6.3 Terminal stage pattern: OUT then done

```asm
  OUT_GHS <handle> <off> <len>   ; copies to SIGNAL + frees handle
  JMP STAGE_LOOP
```

---

## 7. AtomicTasks catalog (CUDA subroutines)

AtomicTasks are the performance core. EPA assembly should only:
- select which task(s) to run
- provide parameter blocks and offsets
- enforce bounds and ownership rules

### 7.1 Task parameter block conventions (draft)
Each task reads a small parameter struct from the packet descriptor region, e.g.:

- pointers/offsets for input/output tensors in GHS packet
- shapes (B, T, d, heads, head_dim)
- dtype codes
- stride / layout flags
- kv-cache handle(s) and kv offsets
- scratch offsets

This makes tasks portable and keeps worker code short.

---

## 8. Phi‑3 stages and tasks (proposed)

### Stage 0: Embedding + initial pack
**Inputs:** token ids, positions  
**Outputs:** X activations tensor

**AtomicTasks:**
- `AT_EMBED_GATHER`
  - Reads token IDs
  - Writes X: `[B, T, d]` (or `[B, 1, d]` for decode)
- (optional) `AT_POS_ENCODE_PREP`
  - Precompute RoPE indices/angles or store position base in descriptor

---

## Layer Group Stages (repeat)

Each layer in Phi‑3 typically has:
- RMSNorm
- Attention (QKV proj + RoPE + KV cache + attention + output proj)
- Residual add
- RMSNorm
- MLP (up proj + activation + down proj)
- Residual add

We split into **Attention half** and **MLP half** per layer-group.

### Stage 1A: ATTN_GROUP(i)
For each layer in group:

**AtomicTasks per layer (suggested fusions):**
1. `AT_RMSNORM_QKV_ROPE_FUSED`
   - Input: X
   - Output: Q, K, V (maybe packed)
   - Applies RMSNorm
   - Applies RoPE on Q and K
2. `AT_KV_CACHE_APPEND`
   - Writes K,V into KV cache (handle(s) + offset)
   - Updates cache write cursor (in descriptor, not in GHS allocator)
3. `AT_FLASH_ATTN`
   - Input: Q + cached K,V
   - Output: AttnOut
4. `AT_LINEAR_O`
   - AttnOut → Oproj
5. `AT_RESIDUAL_ADD`
   - X = X + Oproj

**Handoff:** `NEXT` to `MLP_GROUP(i)`

---

### Stage 1B: MLP_GROUP(i)
For each layer in group:

**AtomicTasks:**
1. `AT_RMSNORM`
2. `AT_MLP_FUSED`
   - up proj + activation + down proj
3. `AT_RESIDUAL_ADD`
   - X = X + mlp_out

**Handoff:**
- if more layer-groups remain: `NEXT` to `ATTN_GROUP(i+1)`
- else: `NEXT` to `FINAL_NORM`

---

### Stage 2: FINAL_NORM
**AtomicTasks:**
- `AT_RMSNORM` (final)

**Handoff:** `NEXT` to `LOGITS`

---

### Stage 3: LOGITS
**AtomicTasks:**
- `AT_LM_HEAD_LINEAR`
- (optional) `AT_SOFTCAP_OR_SCALE`

**Handoff options:**
- `NEXT` to `SAMPLER` (GPU sampling)
- or `OUT_GHS` logits to container/host sampling

---

### Stage 4: SAMPLER (optional GPU)
**AtomicTasks:**
- `AT_TOPK_TOPP_FILTER`
- `AT_SAMPLE_RNG`

**Handoff:**
- `OUT_GHS` token id(s)
- or `NEXT` back to Stage 0 for decode loop

---

## 9. Multi-token parallelism experiments (future)
- Chunked decode (T>1) with causal masking within chunk
- Speculative decode: propose + verify pipelines using route multiplexing

---

## 10. Verification hooks (avionics-friendly)
- Workers never allocate; only kernel ingress allocates.
- OUT_GHS always frees handle.
- NEXT transfers ownership before wake.
- WAIT_NEXT only used in worker entries.

Debug asserts:
- validate handle + bounds before task launches
- leak checks: outstanding handles must return to zero after completion

---

## Appendix A: Suggested task IDs (placeholder)
- 100: AT_EMBED_GATHER
- 110: AT_RMSNORM_QKV_ROPE_FUSED
- 120: AT_KV_CACHE_APPEND
- 130: AT_FLASH_ATTN
- 140: AT_LINEAR_O
- 150: AT_RESIDUAL_ADD
- 160: AT_MLP_FUSED
- 170: AT_LM_HEAD_LINEAR
- 180: AT_TOPK_TOPP_FILTER
- 190: AT_SAMPLE_RNG