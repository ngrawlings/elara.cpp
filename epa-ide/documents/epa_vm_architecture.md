# EPA VM Architecture

## Execution Model

The EPA VM uses a **worker-per-lane** model. Each worker occupies one execution lane and runs to completion before signaling back. Workers do not preempt each other within a kernel.

## Memory Layout

Each kernel declares its memory budget at compile time:

    declare default_in_words 256
    declare default_out_words 256
    declare default_signal_mail_box_size 128

- **in_words** — size of the per-worker input buffer (in 32-bit words)
- **out_words** — size of the per-worker output buffer (in 32-bit words)
- **signal_mail_box_size** — size of the inter-kernel signal queue

These can be overridden per-worker with the `@attributes` decorator:

    @attributes in_words:128 out_words:64 signal_mail_box_size:32
    worker my_worker(MyPayload p) { ... }

## Kernel Registration

When the EPA VM loads a `.bin` bundle it parses the kernel table and registers each kernel by its string ID. The C++ host then ingresses payloads by kernel ID and worker index:

    vm.ingressPayload("my.kernel.id", worker_index, payload);

## Global Handshake Slot (GHS)

When a worker calls `kernel_signal()`, its output payload is copied into the kernel's **GHS** at the worker's slot index. The kernel retrieves it with:

    MyPayload result = kernal_get_ghs(wid);

The GHS is a fixed-size array of payload slots. Each slot holds one worker's last output.

## Mailbox

The EPA VM mailbox is a byte queue between the VM and the C++ host. Workers write to it implicitly via `host_signal()`. The C++ host reads the mailbox with a callback:

    vm.setMailboxCallback([](const uint8_t* data, size_t len) {
        // parse E3SB or custom records
    });

## Cross-Kernel Signals

`far_signal` routes a payload from a worker in one kernel to a worker in another kernel running in the same VM instance. The VM resolves the target worker by kernel ID and worker name using the cross-kernel index baked into the `.bin` bundle at link time.
