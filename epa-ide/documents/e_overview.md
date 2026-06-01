# E Language Overview

E is a statically-typed, event-driven language designed to compile to EPA (Elara Parallel Assembly). It is built around the concept of **kernels** and **workers**, where each worker handles one unit of work and signals back when complete.

## Key Properties

- **Statically typed** — all variables and struct fields carry declared types
- **No dynamic allocation** — memory is managed via fixed-size in/out word buffers declared per-kernel
- **Deterministic execution** — each worker runs to completion on a single invocation
- **Signal-driven** — workers communicate up the call chain via `kernel_signal`, `host_signal`, and `far_signal`

## File Structure

An E file contains:

1. **Declare blocks** — set buffer sizes for the kernel
2. **Struct declarations** — name the payload types
3. **Type constructors** — define how payload types are built
4. **The kernel block** — sets the kernel ID and registers workers
5. **Worker definitions** — the per-invocation logic units
6. **Function definitions** — pure helpers callable from workers

## Compilation

E files are compiled to `.epa` assembly by the E compiler, then assembled to `.bin` by the EPA assembler. The resulting binary is loaded into the EPA VM by the C++ host.

## Example

    declare default_in_words 256
    declare default_out_words 256

    struct MyPayload;

    type MyPayload(int tag) {
      return tag;
    }

    kernel(VM vm) {
      kernalId("my.kernel");
      my_worker(vm);
      int wid = 0;
      while (wid = kernel_wait_signal()) {
        if (wid == 1) {
          MyPayload p = kernal_get_ghs(1);
        }
      }
    }

    worker my_worker(MyPayload payload) {
      kernel_signal();
    }
