# Kernels and Workers

## The Kernel Block

Every E file has exactly one `kernel(VM vm)` block. It runs at startup and is responsible for:

- Registering a kernel ID with `kernalId("my.kernel.id")`
- Registering each worker with `worker_name(vm)`
- Running the kernel event loop via `kernel_wait_signal()`

The kernel ID is a dot-separated string that uniquely identifies this kernel on the EPA VM. Remote kernels target it using `far_signal`.

## Workers

A worker is a function that processes one incoming payload and signals back when done. Workers are declared with the `worker` keyword:

    worker my_worker(MyPayload payload) {
      // do work
      kernel_signal();
    }

### Worker Rules

- A worker **must** call `kernel_signal()` before returning
- Workers may also call `host_signal()` to notify the C++ host
- Workers may call `far_signal(kernel_id, payload)` to send a payload to another kernel
- Workers receive their payload by value — modifications are local

## Union Ingress

Workers can accept multiple payload types using the `|` union syntax:

    worker router(EPABlob|KeyInput ingress) {
      int kind = typeof(ingress);
      if (kind == typeid(EPABlob)) {
        // handle blob
      } else if (kind == typeid(KeyInput)) {
        // handle key
      }
      kernel_signal();
    }

## The Signal Loop

The kernel block waits for worker completions using `kernel_wait_signal()`. This returns the worker index (1-based) of the first completed worker:

    int wid = 0;
    while (wid = kernel_wait_signal()) {
      if (wid == 1) {
        MyPayload result = kernal_get_ghs(1);
        // integrate result
      }
    }

`kernal_get_ghs(wid)` retrieves the payload written by that worker into the kernel's global handshake slot.

## Worker Chaining with `next`

Workers can hand off to a successor without returning to the kernel using the `next` keyword:

    worker stage_a(MyPacket p) {
      // process
      next stage_b;
    }

    worker stage_b(MyPacket p) {
      kernel_signal();
    }

This is used for pipeline chains where successive stages each consume the same payload.
