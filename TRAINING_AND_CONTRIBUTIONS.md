# Elara Training and Contributions

**DRAFT — subject to revision**

---

## Overview

The Elara platform is open-source under the MIT License. The training and seminar programme described here is a separate commercial offering built on top of that open foundation.

The code is free. Expert guidance on how to use it well is not.

---

## Training Programme

### What We Offer

Elara is a general-purpose platform, but games are not a general-purpose problem. Each class of game has its own scheduling demands, data layouts, rendering budgets, and interaction models.

We offer focused training engagements for studios and engineering teams who want to adopt Elara for a specific class of game rather than spending months reverse-engineering the right patterns from first principles.

### Game Class Frameworks

Elara training is organised around game classes, not individual feature tutorials. Current and planned classes include:

| Class | Description |
|---|---|
| Real-Time Strategy | Deterministic simulation, spatial queries, command-and-control agent patterns |
| Role-Playing | World state management, dialogue trees, save/load architecture |
| Platformer / Action | Fixed-timestep physics integration, input latency budgets, animation state machines |
| Puzzle | Turn-based scheduling, undo/redo history, constraint satisfaction patterns |
| Simulation / Management | Entity component models, economy loops, long-tick scheduling |
| Multiplayer / Online | Client/server split, state replication, latency compensation |

Each class has a corresponding reference framework that participants receive during training. These frameworks are MIT-licensed and intended as a starting point, not a black box.

### What a Seminar Covers

A typical engagement covers:

- Elara architecture orientation relevant to the game class
- The matching reference framework — structure, trade-offs, and extension points
- Hands-on sessions integrating Elara's threading, event, socket, and storage layers into a working game prototype
- Debugging and profiling patterns specific to the class
- Q&A and review of the team's intended architecture

Seminars are available in-person or remote. Duration is typically two to three days per class, with custom engagements available for teams requiring deeper work.

---

## Contributions

### Philosophy

Elara is MIT-licensed. Contributions to the core platform are welcome and governed by that licence. There is no contributor licence agreement required for the open software layer as defined in [LICENSING.md](LICENSING.md).

Contributions to the Protected Architecture (ISA, opcode specifications, execution semantics) are subject to separate terms and should be discussed directly before any work begins.

### What We Welcome

- Bug fixes across any library
- Documentation improvements
- New utility classes that fit the existing API style
- Test coverage additions
- Platform portability work
- Example projects that demonstrate real usage

### What We Ask

- Follow the existing coding conventions and linter policy described in [ELARA_AGENT_API.md](ELARA_AGENT_API.md)
- Prefer small, focused changes — one concern per pull request
- New public API should match the ownership and namespace conventions already in use
- If a change touches the EPA instruction format, opcode table, or execution semantics, open a discussion before writing code

### Game Class Framework Contributions

The reference frameworks distributed during training are also MIT-licensed. Improvements to these frameworks — whether from seminar participants or the wider community — are encouraged. Accepted improvements feed back into future training cohorts, which benefits everyone.

---

## Contact

For training enquiries or to discuss a contribution that touches the Protected Architecture, reach out directly.

---

*The training programme is a commercial service. The platform it trains you on is not.*
