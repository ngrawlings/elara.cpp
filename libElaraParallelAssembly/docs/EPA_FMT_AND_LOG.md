# EPA Egress Formatting and Logging

EPA no longer has core `FMT` or `LOG` opcodes.

Formatting and human-readable logging are intentionally library/protocol
features. They are too expensive and policy-heavy for the slim-core ISA.

## Standard Module

Use the common E include:

```c
#include "common/egress.em"
```

It defines the first library-level helpers:

```c
function int fmt(int payload_off, int payload_len);
function int log(int payload_off, int payload_len);
```

The function bodies are ordinary E functions with `EPA { ... }` blocks. This
keeps the implementation portable and replaceable without spending opcode
slots.

## Egress Frame V1

The standard egress frame is four `u32` words:

- `word0`: kind
- `word1`: payload offset
- `word2`: payload length
- `word3`: payload type

Payload bytes live in caller-owned local memory, GHS, or another explicit
handle-backed storage path. The host/OS egress router interprets the frame.

## ISA Boundary

The slim core only supplies primitive movement/signalling operations. It does
not know about decimal formatting, string interpolation, log levels, encodings,
or host stdout.
