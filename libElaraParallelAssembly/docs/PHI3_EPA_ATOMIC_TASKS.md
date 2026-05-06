# EPA Atomic Tasks for Phi‑3 Port
*Draft specification — AtomicTasks abstract massive matrix/tensor operations and execute directly on GHS.*

---

## 0. Definition (authoritative)

An **AtomicTask**:

- Is named `AT_*`
- Represents a **large-grain matrix/tensor compute operation**
- Operates **directly on GHS memory** using offsets + descriptors
- Performs **no allocation**
- Performs **no routing**
- Has **no visibility of EPA workers or kernel**
- Is backend-agnostic (CUDA, CPU reference, future backends)

AtomicTasks are the **only place** where heavy compute occurs.

EPA workers exist only to:
- validate
- sequence
- parameterize
- and route data between AtomicTasks

---

## 1. AtomicTask execution contract

Each AtomicTask:
- Reads parameters from a descriptor region in the packet
- Reads/writes tensor regions in the same GHS handle
- Uses scratch only inside declared capacity
- Returns only a status code (OK / ERROR)

No AtomicTask:
- Allocates memory
- Frees memory
- Touches routing headers
- Sleeps or blocks

---

## 2. Phi‑3 model structure (context)

Phi‑3 (decoder-only transformer) consists of:

- Token embedding
- Repeated layers:
  - RMSNorm
  - Attention (QKV + RoPE + KV cache + attention + output proj)
  - RMSNorm
  - MLP (up → activation → down)
- Final RMSNorm
- LM head (logits)
- Optional sampler

AtomicTasks below are organized by this structure.

---

## 3. Core embedding & input tasks

### AT_EMBED_GATHER
**Purpose:** Token embedding lookup.

- Input: token IDs
- Output: activation tensor `X`
- Shape: `[B, T, d_model]` or `[B, 1, d_model]`
- Notes:
  - Reads embedding table (read-only)
  - Writes X in-place

---

### AT_POSITION_ENCODE_PREP
**Purpose:** Prepare positional encoding metadata (RoPE base, scale, offsets).

- Input: positions
- Output: descriptor fields only
- Notes:
  - No tensor writes
  - Enables later fused RoPE tasks

---

## 4. Normalization tasks

### AT_RMSNORM
**Purpose:** RMS normalization of activations.

- Input: `X`
- Output: `X_norm`
- Can be in-place or write to a new offset
- Used:
  - before attention
  - before MLP
  - final normalization

---

### AT_RMSNORM_INPLACE
**Purpose:** Same as `AT_RMSNORM`, but overwrites input.

- Used when descriptor allows destructive update

---

## 5. Attention-related tasks

### AT_QKV_LINEAR
**Purpose:** Compute Q, K, V projections.

- Input: `X`
- Output: `Q`, `K`, `V`
- Shape:
  - Q: `[B, T, H, D]`
  - K/V: same
- Notes:
  - Usually fused with RMSNorm and RoPE

---

### AT_ROPE_APPLY
**Purpose:** Apply Rotary Positional Embedding.

- Input: Q and K
- Output: Q and K (rotated)
- Notes:
  - No allocation
  - Often fused

---

### AT_RMSNORM_QKV_ROPE_FUSED
**Purpose:** High-value fused task.

- Combines:
  - RMSNorm
  - QKV projection
  - RoPE application
- Input: `X`
- Output: `Q`, `K`, `V`
- Expected to be one of the most performance-critical tasks

---

### AT_KV_CACHE_APPEND
**Purpose:** Append K/V to KV cache.

- Input: `K`, `V`
- Output: Updated KV cache
- Notes:
  - Writes into cache at current cursor
  - Cursor stored in descriptor
  - Cache memory already allocated

---

### AT_FLASH_ATTN
**Purpose:** Attention computation.

- Input:
  - Q
  - K/V cache
- Output:
  - Attention output
- Notes:
  - Causal masking
  - Likely implemented via FlashAttention-style kernel
  - Scratch-heavy but bounded

---

### AT_ATTN_OUTPUT_LINEAR
**Purpose:** Output projection after attention.

- Input: attention output
- Output: projected tensor
- Often fused with residual add

---

### AT_RESIDUAL_ADD
**Purpose:** Add residual connection.

- Input: `X_old`, `X_new`
- Output: `X`
- In-place

---

## 6. MLP tasks

### AT_MLP_UP_PROJ
**Purpose:** First MLP projection.

- Input: `X`
- Output: `H`
- Shape: `[B, T, d_ff]`

---

### AT_MLP_ACTIVATION
**Purpose:** Apply activation (SiLU / GELU).

- Input: `H`
- Output: `H_act`
- Often fused

---

### AT_MLP_DOWN_PROJ
**Purpose:** Project back to model dimension.

- Input: `H_act`
- Output: `X_mlp`

---

### AT_MLP_FUSED
**Purpose:** High-value fused MLP kernel.

- Combines:
  - up projection
  - activation
  - down projection
- Input: `X`
- Output: `X_mlp`
- One kernel if possible

---

## 7. Final projection & output

### AT_LM_HEAD_LINEAR
**Purpose:** Project final activations to logits.

- Input: `X_last`
- Output: `logits`
- Shape: `[vocab]` or `[B, vocab]`

---

### AT_LOGITS_SCALE_CLIP
**Purpose:** Optional scaling or clipping.

- Used if Phi‑3 config requires it

---

## 8. Sampling tasks (optional GPU-side)

### AT_TOPK_FILTER
**Purpose:** Apply top‑k filtering.

- Input: logits
- Output: masked logits

---

### AT_TOPP_FILTER
**Purpose:** Apply nucleus (top‑p) filtering.

- Input: logits
- Output: masked logits

---

### AT_SAMPLE_RNG
**Purpose:** Sample token ID.

- Input: logits
- Output: token ID
- Notes:
  - RNG state stored per stream in descriptor

---

## 9. Utility & support tasks

### AT_TENSOR_COPY
**Purpose:** Fast tensor copy inside GHS.

- Used for repacking or layout changes

---

### AT_TENSOR_ZERO
**Purpose:** Zero a tensor region.

- Used for scratch init or debug

---

### AT_ASSERT_FINITE
**Purpose:** Debug-only numeric check.

- Fails if NaN/Inf detected

---

## 10. Task grouping expectations

High-performance EPA deployments should aim to implement at least:

**Must-have fused tasks**
- `AT_RMSNORM_QKV_ROPE_FUSED`
- `AT_FLASH_ATTN`
- `AT_MLP_FUSED`

Everything else can be composed from these initially.

---

## 11. Backend mapping

Each AtomicTask must have:
- `backend_cuda`
- `backend_cpu_ref` (correctness first)

Identical semantics, different execution strategy.

---

## 12. Invariants (non-negotiable)

- AtomicTasks never allocate or free memory
- AtomicTasks never route packets
- AtomicTasks never touch ring buffers or mailboxes
- All data movement is **in-place**
- All lifetimes are managed outside the task

---

## 13. Status

This list is intentionally **complete but amendable**.

Tasks may be:
- merged
- split
- replaced by better fused kernels

…but **no task should violate the execution contract**.

This document is the **AtomicTask boundary** for EPA × Phi‑3.