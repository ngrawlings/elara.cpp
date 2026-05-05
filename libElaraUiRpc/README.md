# libElaraUiRpc

`libElaraUiRpc` is a transport bridge for `libElaraUI`.

It provides:

- `ElaraUiRpcPeer`: a duplex framed JSON-RPC peer that can both issue requests and serve inbound requests on the same socket.
- `ElaraUiRpcUiService`: a concrete `ui.*` RPC surface for `ElaraRootWidget`.
- `ElaraUiRpcUiBridge`: an outbound event pump that forwards `libElaraUI` events to a remote RPC method such as `ui.event`.
- `ElaraUiDocumentBuilder`: a flat stateful client-side builder that generates correct `elara_ui_protocol` JSON without forcing callers to hand-build nested JSON.

The current `ui.*` surface supports:

- `ui.setText`
- `ui.setVisible`
- `ui.setEnabled`
- `ui.setBounds`
- `ui.setFocus`
- `ui.enableEvent`
- `ui.disableEvent`
- `ui.dispatchMouseMove`
- `ui.dispatchMouseDown`
- `ui.dispatchMouseUp`
- `ui.dispatchKeyDown`
- `ui.dispatchKeyUp`
- `ui.snapshot`
- `ui.snapshotWidget`

`ui.snapshot` returns the active content tree, popup tree, and focus handle.
`ui.snapshotWidget` returns a recursive snapshot for a single widget target.

## Flat Document Builder

`ElaraUiDocumentBuilder` is intended for language-agnostic client layers and bindings. It keeps a cached document model locally and serializes it back to the exact JSON layout format the server expects.

Example:

```cpp
#include <libelarauirpc/ElaraUiDocumentBuilder.h>

using namespace elara;
using namespace elara::ui::rpc;

ElaraUiDocumentBuilder ui;
ui.createWindow("Demo", 800, 600, "org.elara.ui.demo");
ui.setThemeMode("light");

ui.createTabs("app.tabs");
ui.setRootContent("app.tabs");

ui.createGrid("app.widgets");
ui.addTab("app.tabs", "Widgets", "app.widgets");

ui.addGridColumnExact("app.widgets", 24);
ui.addGridColumnFill("app.widgets");
ui.addGridColumnExact("app.widgets", 180);
ui.addGridRowExact("app.widgets", 24);
ui.addGridRowExact("app.widgets", 40);
ui.addGridRowExact("app.widgets", 40);
ui.addGridRowFill("app.widgets");

ui.createLabel("app.widgets.label", "Name:", 14);
ui.createTextInput("app.widgets.input", "type here", "");
ui.createButton("app.widgets.save", "Save", "save.clicked");

ui.placeGridChild("app.widgets", "app.widgets.label", 1, 1);
ui.placeGridChild("app.widgets", "app.widgets.input", 2, 1);
ui.placeGridChild("app.widgets", "app.widgets.save", 2, 2);

String document_json = ui.toJson();
```

Useful operations:
- `createWindow(...)`
- `createWidget(...)` plus convenience methods such as `createButton(...)`, `createGrid(...)`, `createTabs(...)`
- `addChild(...)`
- `addTab(...)`
- `placeGridChild(...)`
- `setPropertyString(...)`, `setPropertyNumber(...)`, `setPropertyBool(...)`
- `setSectionJson(...)` for advanced widget sections like `demo_data`, `nodes`, or other raw JSON payloads
