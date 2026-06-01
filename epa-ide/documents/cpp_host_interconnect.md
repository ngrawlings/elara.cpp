# C++ Host — UI RPC Interconnect

The C++ host communicates with `elaraui-server` using **BRPC** (Binary RPC), a compact framed binary protocol over TCP.

## Protocol Stack

    C++ Host  ←—— TCP/BRPC ——→  elaraui-server
       |                              |
    elara_ui/rpc.py              libElaraUiRpc
    (Python mirror)              (C++ server)

The Python `ElaraUiRpcClient` in `elara_ui/rpc.py` and the C++ `ElaraUiRpcClient` in `libElaraUiRpc` both speak the same wire format. The Python side is used by `app.py` (the IDE logic) and the C++ side is used by the host application.

## Key RPC Methods

| Method | Description |
|--------|-------------|
| `ui.loadDocument` | Send the full widget tree JSON to the server |
| `ui.setText` | Update the text of a label, button, or text widget |
| `ui.setSectionJson` | Update a named section (e.g. `commands`, `items`) of a widget |
| `ui.setProperty` | Set a scalar property on a widget |
| `ui.scrollToBottom` | Scroll a rich text editor to the bottom |
| `ui.snapshot` | Fetch the current widget tree snapshot |

## Event Flow

`elaraui-server` emits raw input events back to the connected client via the same BRPC connection. Events are delivered as framed JSON objects:

    {"event": "keyDown", "target": "app.surface", "keyval": 65}
    {"event": "mouseDown", "target": "app.surface", "button": 1, "x": 100, "y": 200}
    {"event": "action", "target": "app.button", "action": "my_action"}

The C++ host subscribes to events from specific widget targets. Only the host interprets these events — the server never infers application meaning from raw inputs.

## Vulkan Surface Commands

The Vulkan surface widget accepts a `commands` section containing an array of scene command objects:

    ui.setSectionJson("app.surface", "commands", [
      {"op": "scene", "scene_op": 10, ...},
      {"op": "scene", "scene_op": 20, ...}
    ])

The server parses the `op == "scene"` entries and forwards them to `ElaraVulkanSurfaceWidget::addSceneCommand(...)`.

## Connection Ownership

The IDE (`app.py`) owns the single live connection to the IDE-managed `elaraui-server`. For standalone demo runs, the C++ host connects directly. **Do not open a second direct connection to an IDE-owned server** — use `ui_call` via the AI RPC instead.

## BRPC vs JSON-RPC

The default codec is BRPC (binary framing). JSON-RPC mode (`--json-rpc` flag) uses newline-delimited JSON and is available for debugging. Both modes use the same method names and parameter shapes.
