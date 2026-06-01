# Types and Structs

## Struct Declarations

Structs are the payload types passed between workers and kernels. They are declared in two steps:

**Forward declaration:**

    struct MyPayload;

**Type constructor:**

    type MyPayload(int tag, int value) {
      return tag;
    }

The constructor defines the fields and the **discriminant** — the field returned by `return` is used when a union type checks `typeof()`.

## Built-in Types

| Type | Description |
|------|-------------|
| `int` | 32-bit signed integer |
| `VM`  | Opaque handle passed into the kernel block |

## Struct Fields

Struct fields are declared as constructor parameters. All fields are integers in the current runtime:

    type ScenePayload(int frame, int op_count, int flags) {
      return frame;
    }

Fields are accessed by name on a variable of that type:

    ScenePayload sp = kernal_get_ghs(1);
    int f = sp.frame;

## The `reg` Keyword

The `reg` modifier declares a worker-local register variable that persists across loop iterations within a single worker invocation:

    worker counter_worker(MyPayload p) {
      reg int loop_count;
      loop_count = p.value;
      while (loop_count) {
        loop_count = loop_count - 1;
      }
      kernel_signal();
    }

## The `local` Keyword

The `local` modifier declares a local-area struct for use as a staging buffer before `far_signal`:

    worker sender(MyPayload p) {
      local OutPayload outbound;
      // populate outbound fields
      far_signal("remote.kernel", outbound);
      kernel_signal();
    }

`local` structs are placed in the worker's local arena rather than the global handshake slot, keeping them out of the kernel-visible payload path until explicitly sent.
