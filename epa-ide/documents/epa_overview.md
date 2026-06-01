# EPA Overview

EPA (Elara Parallel Assembly) is the low-level instruction set that E compiles to. It runs inside the **EPA VM**, a register-based virtual machine with parallel execution lanes designed for deterministic, low-latency compute.

## Design Goals

- **Predictable latency** — no garbage collection, no dynamic dispatch
- **Parallelism** — multiple workers can be scheduled across EPA execution lanes
- **Compact binary** — `.bin` files are small and fast to load
- **Host integration** — the EPA VM exposes a mailbox for C++ host communication

## Toolchain

| Step | Input | Output | Tool |
|------|-------|--------|------|
| Compile | `.e` | `.epa` | E compiler |
| Assemble | `.epa` | `.bin` | EPA assembler |
| Link | `.bin` files | single `.bin` | EPA linker/bundler |
| Run | `.bin` | live execution | EPA VM (C++ host) |

## The EPA VM

The EPA VM is embedded in the C++ host application. It is loaded with:

    vm.loadBin("path/to/bundle.bin");
    vm.ingressPayload(kernel_id, worker_index, payload_bytes);

Worker outputs come back through the mailbox:

    vm.readMailbox(callback);

## Raw EPA Blocks

E allows inline EPA assembly inside a worker using the `EPA { }` syntax:

    worker my_worker(MyPayload p) {
      EPA {
        // raw EPA instructions
      }
      kernel_signal();
    }

This is used for fine-grained performance tuning where the E compiler output is not optimal. Most production code should stay in E.

## E3SB Format

Scene and render commands produced by the scene compiler worker are emitted as **E3SB** (Elara 3D Scene Binary) records through the host mailbox. The C++ host parses these into JSON scene command arrays and sends them to the Vulkan surface via `ui.setSectionJson`.
