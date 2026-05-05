# libElaraUiRpc

`libElaraUiRpc` is a transport bridge for `libElaraUI`.

It provides:

- `ElaraUiRpcPeer`: a duplex framed JSON-RPC peer that can both issue requests and serve inbound requests on the same socket.
- `ElaraUiRpcUiService`: a concrete `ui.*` RPC surface for `ElaraRootWidget`.
- `ElaraUiRpcUiBridge`: an outbound event pump that forwards `libElaraUI` events to a remote RPC method such as `ui.event`.

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
