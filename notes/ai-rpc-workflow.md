# AI RPC Workflow Notes

These are working notes for using EPA-IDE's AI RPC without repeatedly opening
short-lived connections.

## What AI RPC Is

AI RPC is the logic-side control interface exposed by `epa-ide/app.py`.

Use it for:

- editor and project inspection
- opening projects and files
- surgical editor changes
- compile/debug helpers
- querying exceptions and event logs
- proxying `ui.*` calls through the IDE with `ui_call`

Do not confuse it with the UI RPC on `elaraui-server`.

## Ports And Ownership

- Default AI RPC address: `127.0.0.1:18779`
- The port is configurable with `--ai-rpc-port`
- Some local runs may intentionally use another port such as `18792`
- UI RPC usually runs on `127.0.0.1:18777` in the IDE setup

Critical boundary:

- `app.py` owns the single live connection to `elaraui-server`
- Do not connect a second client directly to the IDE-owned UI server
- Use AI RPC `ui_call` instead when driving the IDE UI

## Transport

- TCP
- UTF-8
- newline-delimited JSON
- one JSON request per line
- one JSON response per line

Request form:

```json
{"method":"ping","params":{}}
```

Success form:

```json
{"ok":true,"result":{...}}
```

Error form:

```json
{"ok":false,"error":"error_code: detail"}
```

## Persistent Client

The persistent REPL client is:

```sh
python3 epa-ide/ai_rpc_client.py --port 18779
```

It keeps one TCP connection open, sends keepalive pings, and accepts either a
bare method name or a raw JSON request.

If the IDE was launched with a non-default port, pass that port explicitly.

Useful forms:

```text
hello
ping
get_ui_status
get_project
open_project {"path":"/home/nyhl/workspace/elara.cpp/demos/orange-fortress"}
get_event_log {"limit":20}
get_exceptions {"limit":10}
ui_call {"method":"snapshot","params":{}}
{"method":"ping","params":{}}
```

Exit with:

```text
:quit
```

Ready signal from the IDE:

```text
{"ai_rpc_listening":"127.0.0.1:18779"}
```

## High-Value Methods

Use these first before guessing:

- `ping`
- `get_ide_state`
- `get_project`
- `open_project`
- `list_tabs`
- `get_file_content`
- `open_file`
- `trigger_compile`
- `get_ui_status`
- `ui_call`
- `get_event_log`
- `get_exceptions`
- `restart_ui`

## Important Constraints

- One active AI RPC client at a time. A second connection may sit queued until
  the first client closes.
- Always close AI RPC clients after testing.
- `elaraui-server` itself accepts a single UI RPC connection from the IDE.
- `ui_call` only works when the IDE is connected to the UI server.
- `save_file` is explicit after editor modifications.
- Prefer surgical editor methods over replacing whole buffers when possible.

## Project Workflow

If the IDE starts without the right project loaded, use:

```text
open_project {"path":"/home/nyhl/workspace/elara.cpp/demos/orange-fortress"}
```

Then verify with:

```text
get_project
list_tabs
get_ide_state
```

## Diagnostics Workflow

When anything about the IDE/UI path is unclear, inspect this order:

1. `get_ui_status`
2. `get_event_log {"limit":50}`
3. `get_exceptions {"limit":10}`
4. `restart_ui {"use_gdb":true}` if the UI server needs crash capture

The event log is usually the fastest way to understand what was happening
across the IDE/UI boundary.

## Independent Demo UI

For an independent demo UI, launch a separate `elaraui-server` on a different
port and a distinct backend/application id:

```sh
libElaraUI/build/bin/elaraui-server \
  --port 18798 \
  --backend-id org.elara.ui.orange-fortress.one-shot \
  --persistent
```

The `--backend-id` option was added so a second GTK application instance does
not collide with the running EPA-IDE UI server.

## Quick Checks

Check listeners:

```sh
ss -ltnp
```

Run the Orange Fortress one-shot host against the demo UI server:

```sh
cd demos/orange-fortress/cpp
./build/orange-fortress 127.0.0.1 18798
```

Expected success markers:

```text
Document loaded: {"loaded":true}
surface mailbox callback wid=1 len=4096
surface E3SB parsed: emitted=1 ...
ui.setSectionJson ok: target=app.surface section=commands ...
Scene confirm received from EPA scene worker.
```
