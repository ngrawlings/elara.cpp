# Signals

Signals are the primary communication mechanism in E. They allow workers to notify the kernel, the C++ host, and remote kernels when work is complete.

## kernel_signal

Notifies the kernel that this worker has finished and its output is available in the kernel's global handshake slot (GHS).

    kernel_signal();

The kernel's `kernel_wait_signal()` loop unblocks and returns the worker index. The payload is then retrieved with `kernal_get_ghs(wid)`.

**Every worker must call `kernel_signal()` exactly once before returning.**

## host_signal

Notifies the C++ host that this worker has produced an output worth reading. The host receives a mailbox message and can inspect the worker state.

    host_signal();

`host_signal()` is optional. Use it when the C++ side needs to consume intermediate worker outputs such as scene commands, rendered frames, or debug events.

## far_signal

Sends a payload to a worker in a **different kernel** by kernel ID string:

    far_signal("remote.kernel.id", outbound_payload);

The target worker is resolved by the multi-file cross-kernel index at build time. Worker names are never referenced by raw integer index.

### far_signal form

    far_signal(kernel_id, payload)

- `kernel_id` — string literal naming the target kernel
- `payload` — a `local`-qualified struct variable staged in local arena

The target kernel must have an `acl` block permitting inbound signals from the sending kernel:

    // acl {
    //   "my.sending.kernel" -> target_worker;
    // }

## Signal Ordering

Within a single worker invocation the typical order is:

1. Do compute work
2. Stage `local` output payloads
3. `far_signal(...)` — notify remote kernels
4. `host_signal()` — notify the C++ host (optional)
5. `kernel_signal()` — notify the parent kernel (required)
