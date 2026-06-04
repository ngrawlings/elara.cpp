# EPAVM System Ring Layer

## Goal

Put one narrow layer between the VM host and the outside system. The CPU VM and a future CUDA VM must use the same contract:

- input arrives as frames on a ring
- output leaves as frames on a ring
- AT commands are normal frames
- memory references are passed as handles/descriptors, not raw process pointers
- the CPU host invokes AT work with the requested thread count
- the CUDA host invokes the same AT work with the same request shape

The BRPC endpoint can point at a CPU `epavm` instance or a CUDA `epavm` instance. The IDE and higher host code should not care which one it is talking to.

## Placement

Current shape:

```text
IDE / C++ app
  -> BRPC JSON
    -> EpaDbgService
      -> EpaDbgVmHost
        -> epa_kernel_* CPU API
```

Target shape:

```text
IDE / C++ app
  -> BRPC JSON
    -> EpaDbgService
      -> EpaVmHost
        -> EpaSystemLink
          -> CPU system backend
          -> CUDA system backend
```

`EpaSystemLink` is the new layer. It owns the standardized rings, frame format, memory-handle table, and AT command dispatch contract.

## Core Rule

Nothing above `EpaSystemLink` gets a CPU-only shortcut.

If a feature cannot be represented as a system frame plus memory descriptors, it is not ready for CUDA. CPU can implement the backend directly in-process, but it still has to consume and produce the same frames as CUDA.

## Rings

Use two logical rings at minimum:

- `host_to_vm`: ingress, control, AT responses, memory registration
- `vm_to_host`: egress, host_signal, AT requests, faults, debug events

They can be backed by normal CPU memory, shared memory, pinned memory, or CUDA-visible memory. The ring semantics stay the same.

Each ring is single-producer/single-consumer at the transport layer. If multiple VM workers emit frames, they go through a backend-owned arbitration point before the ring write.

## Frame Header V1

All frames start with a fixed-size little-endian header:

```c
typedef struct EpaSysFrameHeader {
    uint32_t magic;          // 'EPFR'
    uint16_t version;        // 1
    uint16_t header_bytes;   // sizeof(EpaSysFrameHeader)
    uint16_t kind;           // EpaSysFrameKind
    uint16_t flags;          // ack/error/has_refs/etc.
    uint32_t frame_bytes;    // header + payload + refs
    uint64_t seq;            // producer sequence
    uint64_t correlation_id; // request/response pairing
    uint32_t kernel_id;      // numeric runtime kernel slot
    uint32_t worker_id;      // worker lane, or 0xffffffff for system
    uint32_t payload_bytes;
    uint32_t ref_count;
} EpaSysFrameHeader;
```

`payload_bytes` is followed by `ref_count` memory references.

## Frame Kinds

```c
enum EpaSysFrameKind {
    EPA_SYS_INGRESS        = 1,
    EPA_SYS_EGRESS         = 2,
    EPA_SYS_HOST_SIGNAL    = 3,
    EPA_SYS_AT_REQUEST     = 4,
    EPA_SYS_AT_RESPONSE    = 5,
    EPA_SYS_MEM_REGISTER   = 6,
    EPA_SYS_MEM_RELEASE    = 7,
    EPA_SYS_DEBUG_EVENT    = 8,
    EPA_SYS_FAULT          = 9,
    EPA_SYS_CONTROL        = 10
};
```

Ingress and egress become ordinary frames. The existing host mailbox becomes `EPA_SYS_HOST_SIGNAL` or `EPA_SYS_EGRESS` depending on whether the payload is an event or a data result.

## Memory References

Frames never carry raw pointers across the layer. They carry descriptors:

```c
typedef struct EpaSysMemoryRef {
    uint64_t handle;       // backend-owned stable id
    uint64_t offset;
    uint64_t bytes;
    uint32_t space;        // CPU, CUDA_DEVICE, CUDA_PINNED, SHARED
    uint32_t access;       // READ, WRITE, READ_WRITE
    uint32_t element_type; // optional typed view
    uint32_t element_bytes;
} EpaSysMemoryRef;
```

On CPU, a handle maps to an owned host allocation or a GHS block. On CUDA, a handle maps to VRAM, pinned host memory, or an imported device allocation. The shape is identical.

GHS payloads are allocated at final size. Large or non-zero initial payloads should be built in VM local bytes first, then copied into GHS during allocation:

```text
G_ALLOC_L
  in:  r0 = type/tag
       r1 = final_size_bytes
       r2 = local_bytes_offset
  out: r0 = ghs_handle_lo
       r1 = ghs_handle_hi
```

E exposes this as `ghs_alloc_from_local(TypeName, localPayload)`. This avoids constructing large payloads on the stack; the stack only carries the resulting two-word GHS handle.

## AT Request

An AT command is a frame with `kind = EPA_SYS_AT_REQUEST`.

AT implementations are EPA `AT_ENTRY` blocks, not worker entries and not native host functions:

```text
AT_ENTRY_START <at_id:u32> <frame_words:u16>
  ...
AT_ENTRY_END
```

E source declares the same body with a dedicated top-level entry:

```text
at_entry Name(u32 thread_index, SharedBody body) {
  ...
}
```

The fixed AT entry prototype is:

```text
r0 = thread_index
r1 = shared_ghs_handle_lo
r2 = shared_ghs_handle_hi
r3 = requested_thread_count
stack/frame words = optional descriptor parameters declared by frame_words
```

Every thread receives the same shared GHS handle and uses `thread_index` plus the descriptor data/GHS body layout to determine its slot. This keeps the ISA 1:1 across CPU, CUDA, and silicon: the system layer schedules AT entry invocations, but the executable body is EPA bytecode.

VM opcode first draft:

```text
REQUEST_AT
  stack before:
    descriptor_word[0]
    descriptor_word[1]
    ...
    descriptor_word[n - 1]
    descriptor_word_count

  stack after successful submission:
    descriptor words removed

  registers after successful submission:
    r0 = request_id low u32
    r1 = 1
```

The opcode copies descriptor words into stable request storage before the stack pointer is changed. If submission fails, the stack is left intact.

Descriptor header words:

```text
word 0 = at_id
word 1 = at_version
word 2 = virtual_threads
word 3 = real_threads
word 4 = param_words
word 5 = result_ref_index
word 6... = params / refs for the current descriptor version
```

`REQUEST_AT` does not execute an AT command inside the VM and does not call a CPU-native AT router. It only submits the copied descriptor to the system request queue. The system layer turns that descriptor into an `EPA_SYS_AT_REQUEST` frame on the global ring, invokes the CPU/CUDA/silicon implementation behind the same contract, and returns results as `EPA_SYS_AT_RESPONSE` plus memory refs.

If the system AT request ring is temporarily full, `REQUEST_AT` applies backpressure instead of faulting: the descriptor remains on the stack, the instruction pointer does not advance, and the worker yields so the same instruction can retry later. It faults only for hard/impossible cases such as malformed descriptors, unknown AT ids, or resource failures that cannot be resolved by queue space becoming available.

Payload:

```c
typedef struct EpaSysAtRequest {
    uint32_t at_id;             // stable command id
    uint32_t at_version;
    uint32_t requested_threads;
    uint32_t flags;
    uint32_t param_bytes;
    uint32_t result_ref_index;  // memory ref slot for output, or 0xffffffff
} EpaSysAtRequest;
```

The frame refs describe all inputs/outputs. The payload holds scalar parameters.

Example:

```text
AT_REQUEST
  at_id = FLASH_ATTN
  requested_threads = 4096
  payload = scalar dimensions / strides / flags
  refs[0] = q tensor
  refs[1] = k tensor
  refs[2] = v tensor
  refs[3] = output tensor
```

CPU backend invokes the CPU AT implementation with `requested_threads`. CUDA backend invokes the CUDA implementation with the same `requested_threads` and the same refs, just backed by device memory.

## AT Response

An AT command returns an `EPA_SYS_AT_RESPONSE` with the same `correlation_id`.

Payload:

```c
typedef struct EpaSysAtResponse {
    uint32_t status;       // OK, FAULT, UNSUPPORTED, BAD_REFS
    uint32_t result_code;
    uint32_t threads_used;
    uint32_t payload_bytes;
} EpaSysAtResponse;
```

It may include refs for newly produced memory. For example, CUDA can return a VRAM handle; CPU can return the same logical handle shape mapped to host memory.

## Backend Interface

First C/C++ shape:

```c
typedef struct EpaSystemBackend EpaSystemBackend;

typedef struct EpaSystemBackendVTable {
    int  (*open)(EpaSystemBackend*, char err[256]);
    void (*close)(EpaSystemBackend*);
    int  (*submit_frame)(EpaSystemBackend*, const EpaSysFrameHeader*, const void *payload,
                         const EpaSysMemoryRef *refs, char err[256]);
    int  (*poll_frame)(EpaSystemBackend*, EpaSysFrameHeader*, void *payload, uint32_t payload_cap,
                       EpaSysMemoryRef *refs, uint32_t refs_cap, char err[256]);
    int  (*register_memory)(EpaSystemBackend*, void *ptr, uint64_t bytes, uint32_t space,
                            uint64_t *out_handle, char err[256]);
    int  (*release_memory)(EpaSystemBackend*, uint64_t handle, char err[256]);
} EpaSystemBackendVTable;
```

CPU backend can call current `epa_kernel_*` functions behind this interface. CUDA backend can marshal frames over BRPC or shared ring memory to the CUDA process.

## Migration Plan

1. Add frame structs and ring helpers with tests.
2. Add `EpaSystemBackend` with a CPU backend that wraps the existing `epa_kernel_*` API.
3. Move `EpaDbgVmHost` to call the backend, not `epa_kernel_*` directly.
4. Convert existing ingress and host_signal paths into `EPA_SYS_INGRESS` and `EPA_SYS_HOST_SIGNAL` frames.
5. Add `EPA_SYS_AT_REQUEST` and `EPA_SYS_AT_RESPONSE` handling for one simple CPU AT command.
6. Add the CUDA backend as a second implementation of the same interface.
7. Add a build target for CUDA that compiles the same AT command ABI with CUDA toolchain blocks behind `#ifdef EPAVM_CUDA`.

## Non-Goals For The First Cut

- Do not expose CUDA-specific types above `EpaSystemLink`.
- Do not make BRPC aware of AT internals.
- Do not hand workers raw pointers.
- Do not require CUDA to exist for CPU tests.
- Do not replace all existing debug service methods at once.

## Naming

Suggested names:

- `EpaSystemLink`: owner of rings and frame encode/decode
- `EpaSystemBackend`: CPU/CUDA backend interface
- `EpaSysFrame`: standardized frame
- `EpaSysMemoryRef`: CPU/VRAM/shared memory reference
- `EpaSysAtRequest`: AT command request
