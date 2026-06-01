# Current Rendering Task State

## Goal

Build toward full Vulkan-surface 3D rendering driven by EPA scene logic.

## Current Proof Point

The original working checkpoint is a one-shot EPA flow:

1. The C++ host connects to `elaraui-server` over BRPC.
2. It loads a simple document whose root widget is `elara.widgets.vulkan_surface`.
3. It loads `demos/orange-fortress/build/epa.bin`.
4. It ingresses one `ScenePoseInput` payload into `orange.fortress.scene` worker `1`.
5. The scene worker emits one E3SB type-3 scene command into the host mailbox.
6. The C++ host parses the E3SB record into JSON scene commands.
7. It updates the Vulkan surface's `commands` section with `ui.setSectionJson`.
8. The host enters cached scene replay mode after printing scene confirmation.

Verified success text:

```text
surface mailbox callback wid=1 len=4096
surface E3SB parsed: emitted=1 frame=4294966396 records=1
ui.setSectionJson ok: target=app.surface section=commands bytes=293
Scene confirm received from EPA scene worker.
```

## Cached Cube Replay

The C++ host now builds three cached cube scene command buffers and replays them
directly into the Vulkan surface. This lets the UI server rendering path be
iterated without waiting on the EPA scene compiler.

Boundary rule: cached scenes, camera selection, EPA ingress, and input
interpretation belong in the Orange Fortress host. `elaraui-server` only
displays received draw/scene instructions and forwards raw input events.

Controls:

- `1`, `2`, `3`: select one of three fixed camera angles
- `space`: next angle
- arrows/WASD: host-side movement state for the EPA scene pose path
- stdin commands also work: `1`, `2`, `3`, `next`, `prev`, `quit`

Each cached scene includes:

- environment command (`scene_op 20`)
- camera view (`scene_op 10`)
- camera clip (`scene_op 11`)
- material (`scene_op 30`)
- cube mesh declaration (`scene_op 40`)
- cube instance (`scene_op 50`)
- cube transform (`scene_op 51`)
- text overlay identifying the active angle

## Empty Scene Investigation

The first suspected issue was the CPU fallback path, but that was intentionally
removed from the active draw path so rendering proof stays Vulkan-only.

The actual empty-scene bug was in the live RPC command parser:
`libElaraUiRpc/libelarauirpc/ElaraUiRpcUiService.cpp` accepted
`setSectionJson(..., "commands", ...)`, but its Vulkan surface command parser
only handled `clear`, `rect`, `line`, and `text`. It did not forward
`{"op":"scene"}` entries to `ElaraVulkanSurfaceWidget::addSceneCommand(...)`.
The RPC call returned success while silently dropping all camera/material/mesh
instance commands.

Current fix:

- `ElaraUiRpcUiService.cpp` now parses `op == "scene"` in the live
  `setSectionJson` path.
- `ElaraVulkanSurfaceWidget.cpp` does not call the CPU fallback from
  `drawCanvas`; the surface proof is Vulkan-only.
- `libElaraUI/demo/rpc_server.cpp` no longer contains the demo listener that
  mutated widgets with app behavior such as `setText`.
- `OrangeFortressApp.cpp` seeds the initial document with a cached cube scene
  before loading it, so a fresh host connection should not begin with an empty
  surface while waiting for EPA confirmation.
- The three cached cube cameras were retuned so all angles face the cube.

Smoke test command:

```sh
libElaraUI/build/bin/elaraui-server --port 18810 --backend-id org.elara.ui.orange-fortress.cache-replay --persistent
printf '1\n2\n3\nnext\nprev\nquit\n' | ./build/orange-fortress 127.0.0.1 18810
```

The server accepted all three cached cube command payloads after the RPC parser
patch.

Clean-port smoke test on 2026-06-01 used port `18778` because an old Python
AI-RPC REPL client was still connected to `18777` and sending unrelated
`setText`/`setForegroundColor` calls. On the clean port:

```text
Document loaded: {"loaded":true}
Scene confirm received from EPA scene worker.
ui.setSectionJson ok: target=app.surface section=commands bytes=817
ui.setSectionJson ok: target=app.surface section=commands bytes=830
ui.setSectionJson ok: target=app.surface section=commands bytes=832
```

The server also logged raw `keyDown`/`keyUp` events for `app.surface`, which
confirms keyboard capture on the UI side. The host owns their interpretation.

## Files Involved

- `demos/orange-fortress/epa/scene.e`
  - Constructs the first scene payload.
  - Currently emits one E3SB scene command directly for the checkpoint.
- `demos/orange-fortress/epa/scene_compiler.e`
  - Local copy of the platform scene compiler.
  - Kernel id is `orange.fortress.scene_compiler`.
  - Included in the bundle, but its full worker path still needs follow-up.
- `demos/orange-fortress/epa/platform_common.em`
  - Local copy of platform scene payload types and opcodes.
- `demos/orange-fortress/cpp/src/OrangeFortressApp.cpp`
  - Vulkan surface document load, EPA ingress, mailbox parse, scene confirmation,
    cached cube scene construction, and angle replay controls.
- `libElaraUI/demo/rpc_server.cpp`
  - Generic UI RPC/render shell only. Demo mutation logic was removed.
  - Supports `--backend-id` so independent UI server instances can run beside
    EPA-IDE.

## Known Gap

`scene_compiler.e` receives the far-signaled payload, but the full compiler
worker currently stalls before `frame_commit` when driven through the current
runtime path. For the checkpoint, `scene.e` emits the one-command E3SB frame
directly. The next task should make the scene compiler complete and then move
the direct emission back behind the compiler.

## Next Rendering Steps

- Use cached cube replay to tune `elaraui-server` / `ElaraVulkanSurfaceWidget`
  until all three camera angles render correctly.
- Fix or simplify the `scene_compile_full` worker so it reliably reaches
  `frame_commit`.
- Expand from one command to the full camera/environment/material/mesh/instance
  command stream.
- Replace the current projected/wireframe Vulkan widget handling with actual
  staged 3D renderer data.
- Keep the one-shot host as a fast integration test while building the renderer.
