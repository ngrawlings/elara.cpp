# EPA Game Engine Kernel Hierarchy Draft

## Status

This is a first-draft architecture note for a game engine built around the current EPA runtime model in `libElaraParallelAssembly` and a C++ host using the generated EPA VM host adapter.

It reflects the system as it stands today:

- One `.e` file compiles to one kernel with its workers.
- `entry.e` is the root kernel compile unit.
- Multiple kernels are bundled into `build/epa.bin`.
- The C/C++ runtime loads that bundle into one `EpaKernel` instance per bundle entry.
- The host starts kernels, stops kernels, and currently supplies the first CPU scheduler thread per kernel.
- A kernel may request more dedicated scheduler threads at runtime with `request_threads(n)`.
- Inter-kernel communication is performed with `far_signal(target_id, local_payload)`.
- Local kernel coordination is performed with `kernel_signal()`.
- Host coordination is performed with `host_signal()`.

This document is intentionally biased toward a GPU-native game engine model where the CPU host becomes mostly IO, presentation, resource ownership, and debug/control plane.

## 1. Current EPA Runtime Understanding

### 1.1 Compilation and load model

- Each `.e` file defines one kernel and its worker set.
- `.em` files are include-only modules used through `#include`.
- `e2epabin` compiles all `.e` units, assembles them, and emits:
  - per-kernel EPA assembly
  - per-kernel EPA blobs
  - one indexed bundle: `build/epa.bin`
- `entry.e` is always the root kernel and always occupies bundle slot `0`.
- Subsequent `.e` files become additional kernels in the same bundle.

### 1.2 Runtime kernel module model

- The C runtime bundle loader creates one `EpaKernel` instance per bundled kernel.
- Each kernel gets a runtime kernel id from the bundle `path_id`.
- The runtime registry allows kernel lookup by string id.
- `far_signal(...)` resolves the destination kernel through that registry.

This means the engine is already moving toward a kernel mesh rather than a single monolithic VM.

### 1.3 Scheduler and thread model

Current CPU scheduler understanding:

- Each started kernel begins with one dedicated scheduler-owned thread.
- The host may add more dedicated threads to a specific kernel.
- The kernel itself may request a larger thread budget with `request_threads(n)`.
- The thread request is a desired total, not an incremental add.
- Requests only grow capacity today; they do not shrink it.

This is a strong bridge model for future CUDA/OpenCL backends:

- host remains the control plane
- backend execution remains kernel-centric
- scheduling decisions can be translated into device launch/resume policy later

### 1.4 Communication model

There are now three distinct signal paths:

- `kernel_signal()`
  - worker -> owning kernel
  - used for local coordination and integration

- `far_signal(target_id, local_payload)`
  - worker -> other kernel
  - payload must be staged in local arena storage
  - this is the engine's inter-kernel bus

- `host_signal()`
  - worker -> host
  - uses outbound mailbox
  - used for IO-facing coordination, debug, and presentation boundary work

This separation is important. It keeps:

- local orchestration local
- inter-kernel routing explicit
- host interaction minimized and intentional

### 1.5 Memory and payload model

Current memory tiers:

- GHS
  - transferable payload/state carrier
  - intended for owned working state and ingress payload

- `static`
  - persistent worker-owned state
  - initialized once before the worker wait loop
  - survives wake cycles
  - this is the storage class that should carry durable worker continuity

- stack/frame locals
  - per-function / per-activation scalar and value state

- local byte arena
  - worker-owned scratch area
  - can persist across wake cycles unless reset
  - scoped allocation support exists for transient call-chain use
  - ideal for building outbound `far_signal(...)` payloads

Declared-type ABI direction:

- primitives pass by value
- declared types pass by reference by default
- local arena typed objects can move through call chains without implicit copying

This is a good fit for an engine because large artifacts should not be copied casually.

Practical rule:

- use `static` for durable worker state such as position, mode, phase, timers, cached decisions, and other continuity-bearing values
- use `local` for outbound typed payloads and temporary typed work objects
- do not treat inbound GHS as a substitute for owned persistent state

That separation matters. A stateful worker should keep its identity in `static`, not rebuild it implicitly every wake.

## 2. Role Of The C++ Host

The C++ host should not be the gameplay brain. It should be the control and integration plane.

### 2.1 Host responsibilities

- load `build/epa.bin`
- instantiate runtime kernels
- assign runtime lifecycle
- start/stop kernels
- provide first scheduler thread per kernel
- obey or cap runtime thread requests
- own windowing, OS surface, swap/present, file IO, device IO, input ingress
- own debug services, breakpoint/step plumbing, and tooling RPC
- own persistent resources that are not yet device-native

### 2.2 Host should avoid

- per-frame gameplay orchestration
- CPU-side scene assembly where device-side kernels can do it
- unnecessary copying of GPU-resident artifact state
- becoming the default router between kernels

### 2.3 Future target shape

The ideal long-term shape is:

- EPA kernels coordinate gameplay, simulation, artifact composition, and render preparation
- host handles:
  - external IO
  - presentation boundary
  - tooling/debugger
  - resource/bootstrap ownership

In that model, the host is closer to an IO coprocessor and runtime supervisor than a traditional game loop authority.

## 3. Proposed Kernel Hierarchy For A Game Engine

This draft assumes a game engine where multiple kernel trees run concurrently and communicate through explicit mesh links.

## 3.1 Top-level principle

Not one giant root kernel for the whole game.

Instead:

- one root orchestration kernel
- many domain kernels beneath it
- each domain kernel may own specialized sub-kernels
- artifacts are composed upward
- commands/events flow sideways and downward

This matches the EPA model better than a monolith.

## 3.2 Suggested kernel tiers

### Tier 0: Root Frame Kernel

Suggested id:

- `engine.root.frame`

Role:

- root coordinator for a frame or simulation tick
- collects completed artifact signals from domain kernels
- composes final scene/frame submission intent
- signals host only when external presentation/IO is needed

Primary tasks:

- receive local completion signals from child kernels
- receive merged artifact handles/state
- decide frame completion
- trigger final render/present kernel or host signal
- request higher thread budget during heavy integration windows if needed

Workers inside root frame kernel:

- frame event routing worker
- artifact merge worker
- frame completion worker

### Tier 1: Major Domain Kernels

Each major game subsystem should be its own kernel.

Examples:

- `world.simulation`
- `world.navigation`
- `render.scene`
- `render.lighting`
- `render.ui`
- `audio.mix`
- `gameplay.rules`
- `input.dispatch`
- `physics.broadphase`
- `physics.narrowphase`

Role:

- coordinate their own subsystem's worker pipelines
- expose clean artifact outputs upward
- communicate laterally only when necessary

These kernels should be long-lived and usually started at application boot.

### Tier 2: Artifact Kernels

These are kernels that build or maintain a specific gameplay or render artifact.

Examples:

- `actors.player.avatar`
- `actors.npc.guard_001`
- `weapons.player.machinegun`
- `weapons.ammunition.rapid_fire_animation`
- `render.hud.main`
- `render.level.room_12`

Role:

- own one coherent artifact or actor domain
- integrate local workers into a stable output object
- signal parent/domain kernels when output changed or action completed

This is where EPA becomes especially strong. A kernel can coordinate many workers to maintain one object, character, weapon, room, or effect with minimal CPU involvement.

### Tier 3: Specialized Service Kernels

These are kernels that exist to provide reusable services across domain kernels.

Examples:

- `services.path_cache`
- `services.visibility`
- `services.projectile_resolver`
- `services.animation_decode`
- `services.particle_spawn`

Role:

- receive requests by far ingress
- perform specialized high-parallel work
- return results by far ingress or kernel signaling to requestors

These should be used carefully. Too many service hops can increase routing complexity.

## 4. Inter-Kernel Communication Mesh Layout

The mesh should not be fully arbitrary even though `far_signal(...)` allows loose routing.

The runtime supports arbitrary kernel-to-kernel communication, but the engine architecture should still impose shape.

## 4.1 Recommended mesh rule

Allow three categories of links:

- parent <-> child
- sibling <-> sibling within same domain
- requestor -> shared service kernel

Avoid:

- unrestricted all-to-all chatter
- kernels routing large amounts of data through the host
- artifact ownership bouncing randomly across unrelated domains

## 4.2 Mesh directions

### Downward flow

Used for:

- commands
- intent
- resource or mode changes
- activation of specialized sub-artifacts

Examples:

- `engine.root.frame` -> `render.scene`
- `gameplay.rules` -> `actors.player.avatar`
- `weapons.player.machinegun` -> `weapons.ammunition.rapid_fire_animation`

### Upward flow

Used for:

- completed artifact outputs
- local completion signals
- state summaries
- frame-ready signals

Examples:

- `actors.player.avatar` -> `world.simulation`
- `render.scene` -> `engine.root.frame`

### Lateral flow

Used for:

- peer coordination within same domain
- specialized cross-artifact cooperation

Examples:

- `actors.player.avatar` -> `weapons.player.machinegun`
- `physics.broadphase` -> `physics.narrowphase`

Lateral links should be explicit and sparse.

## 4.3 Payload conventions

Suggested convention for inter-kernel payloads:

- use `local` typed payloads for outbound `far_signal(...)`
- use `static` for any state that must still exist on the next worker wake
- treat them as message objects, not shared state
- tag them clearly with `typeid(...)`
- route them with `typeof(...)` on ingress

Recommended categories:

- command payloads
- artifact delta payloads
- event payloads
- request/response payloads

Do not use current worker inbound GHS as implicit outbound payload. Outbound messages should be prepared explicitly.

## 5. Suggested First Engine Kernel Set

A practical first draft for a Doom/Duke-style engine:

### Root and frame

- `engine.root.frame`

### Input and control

- `input.dispatch`
- `gameplay.rules`

### Player and NPC

- `actors.player.avatar`
- `actors.npc.population`

### Weapons and projectiles

- `weapons.player.machinegun`
- `weapons.projectiles`
- `weapons.fx`

### World and physics

- `world.level`
- `physics.broadphase`
- `physics.narrowphase`

### Rendering

- `render.scene`
- `render.visibility`
- `render.lighting`
- `render.ui`
- `render.final.compose`

### Host boundary

- host receives final frame completion and input IO only

## 6. Example Interaction Mesh

Example update loop shape:

1. Host ingresses input and timing into `input.dispatch` and/or `engine.root.frame`.
2. `input.dispatch` far-signals gameplay/actor kernels with typed input events.
3. `gameplay.rules` routes intent to:
   - player actor kernel
   - weapon kernel
   - world rule kernels
4. Actor kernels update internal workers and signal local parent kernels.
5. Weapon kernels emit projectile or effect requests to specialized kernels.
6. World and physics kernels update artifact state.
7. Render domain kernels consume state or summarized artifacts.
8. `render.final.compose` assembles the final frame artifact.
9. `engine.root.frame` decides frame complete.
10. `host_signal()` is used only when the host must present or perform external IO.

This keeps the bus mostly inside the kernel mesh.

## 7. Recommended Task Split Per Kernel

Inside each kernel, the kernel itself should coordinate while workers perform narrow tasks.

### Kernel duties

- wait for signals or ingress
- route typed inbound payloads
- request additional threads when load spikes
- coordinate artifact integration
- decide when local work unit is complete
- emit:
  - `kernel_signal()` to local kernel
  - `far_signal(...)` to other kernels
  - `host_signal()` only for host boundary work

### Worker duties

- narrow stateless or lightly stateful transforms
- parse/update one payload
- perform one slice of simulation
- stage outbound local payloads
- notify kernel on completion

This separation should stay strict.

## 8. Thread Budget Strategy

Current EPA supports:

- host startup with one thread
- kernel self-request for more threads

Recommended strategy:

- root frame kernel: modest but elastic
- high-fanout render or physics kernels: request more threads on demand
- small artifact kernels: stay at one thread unless under burst load

Future direction:

- kernel metadata may declare preferred initial thread count
- host may cap or override
- CUDA/OpenCL backend may translate the same request into launch width / stream policy rather than CPU threads

## 9. Debugging Model

The IDE and debug RPC path should map onto this hierarchy directly.

Needed debugger surfaces:

- loaded kernel list from `epa.bin`
- per-kernel status
- per-kernel thread count
- per-kernel queue depth / wake reason
- far-signal trace
- current GHS / stack / local arena inspection
- breakpoints at kernel and worker boundaries
- signal and exception event stream

Best debugging mental model:

- debug one kernel tree at a time
- debug one worker wake cycle at a time
- inspect mesh traffic as messages, not as shared memory

This is one of the strongest arguments for the IDE.

## 10. Open Questions

These are the main unresolved design questions for the engine hierarchy.

### 10.1 Kernel id authoring

Today ids come from bundle `path_id`.

Likely future requirement:

- explicit hierarchical kernel ids in source or bundle metadata
- examples:
  - `weapons.player.machinegun`
  - `weapons.ammunition.rapid_fire_animation`

### 10.2 Default ingress worker policy

`far_signal(...)` currently routes to the target kernel's default ingress worker.

Future options:

- keep one default ingress worker per kernel
- or allow explicit target ingress endpoints later

### 10.3 Artifact ownership model

Need a strict rule for:

- who owns mutable artifact state
- who may emit read-only public views
- when a parent kernel may safely integrate a child artifact

### 10.4 Kernel hierarchy authoring

Need a clean source-level way to express:

- parent/child kernel relationships
- started kernels
- optional kernels
- preferred thread budgets

### 10.5 CUDA/OpenCL resumption

Need a backend-level design for:

- yield points
- resumable state
- host control plane handoff
- mapping `request_threads(...)` to device scheduling

## 11. Draft Architectural Position

The most promising direction is:

- EPA kernels become the native coordination fabric of the engine
- workers become the dense execution fabric
- far ingress becomes the internal engine bus
- host becomes mostly IO, presentation boundary, and debug/control plane

The resulting hierarchy should look less like a traditional CPU game loop and more like an operating mesh of device-native artifact coordinators.

That is the part that could make this engine qualitatively different rather than just incrementally faster.
