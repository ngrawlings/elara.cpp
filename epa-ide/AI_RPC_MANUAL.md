# EPA-IDE AI RPC — Collaboration Manual

This document is written for an AI assistant that wants to collaborate with the
EPA-IDE. It covers connection, protocol, every available method, error handling,
and recommended workflows for common tasks.

---

## 1. What the AI RPC is

The AI RPC is a logic-side control interface exposed by `app.py`. It gives an
AI client direct, synchronous access to:

- the live editor state (tabs, content, cursors)
- the project and nav tree
- the EPA compiler
- the UI layer (via a proxy — see §9)
- exception and event logs for self-diagnosis
- the UI server process (restart, gdb capture)

The UI RPC (`elaraui-server`, port 18777) only accepts **one connection** — the
one from `app.py`. Do **not** connect to port 18777 directly. Use `ui_call` on
the AI RPC instead.

---

## 2. Connection

| Property    | Value                           |
|-------------|----------------------------------|
| Host        | `127.0.0.1`                      |
| Port        | `18779` (default; set via `--ai-rpc-port`) |
| Transport   | TCP                              |
| Encoding    | UTF-8                            |
| Framing     | Newline-delimited JSON — one JSON object per line (`\n` terminated) |
| Concurrency | One active client at a time; new connections are queued by the OS |

The server is started when `app.py` is launched with `--ai-rpc-port 18779`.
Look for this line on app.py's stdout to confirm it is ready:

```
{"ai_rpc_listening": "127.0.0.1:18779"}
```

### Minimal Python client

```python
import socket, json

sock = socket.create_connection(("127.0.0.1", 18779))
fh = sock.makefile("rwb", buffering=0)

def call(method, **params):
    req = {"method": method, "params": params}
    fh.write((json.dumps(req) + "\n").encode())
    fh.flush()
    return json.loads(fh.readline())

r = call("ping")
assert r["ok"] and r["result"]["pong"]
```

### Minimal shell one-liner (nc)

```sh
echo '{"method":"ping","params":{}}' | nc -q1 127.0.0.1 18779
```

---

## 3. Request / response format

**Request:**
```json
{"method": "method_name", "params": {"key": "value"}}
```
`params` may be omitted or `{}` for methods that take no arguments.

**Success response:**
```json
{"ok": true, "result": <method-specific value>}
```

**Error response:**
```json
{"ok": false, "error": "error_code: human-readable detail"}
```

Every response is a single JSON object terminated by `\n`. Read one line,
parse it, check `ok`.

---

## 4. Error codes

| Code             | Meaning                                              |
|------------------|------------------------------------------------------|
| `unknown_method` | The method name was not recognised.                  |
| `missing_param`  | A required parameter was absent.                     |
| `no_such_tab`    | No open tab matches the given `tab_id` or `path`.    |
| `compile_error`  | EPA compilation failed; check `result.error`.        |
| `ui_unavailable` | The UI RPC client is not yet connected to the UI.    |
| `io_error`       | File-system operation failed.                        |
| `internal: ...`  | Unexpected Python exception — check `get_exceptions`.|

---

## 5. Method reference

### 5.1 Utility

#### `help`
Returns the full HELP dict (same information as this manual, machine-readable).

```json
{"method": "help", "params": {}}
```

#### `ping`
Liveness check.

```json
{"method": "ping", "params": {}}
// result: {"pong": true, "ts": 1716300000.0}
```

---

### 5.2 IDE state queries

#### `get_ide_state`
Full snapshot: active tab, all tabs, project root, theme, AI model state.

```json
{"method": "get_ide_state", "params": {}}
// result:
{
  "active_tab": "tab-uuid",
  "tabs": [...],
  "project_root": "/home/user/myproject",
  "theme": "dark",
  "ai_model": "claude-sonnet-4-6",
  "ai_generating": false
}
```

#### `list_tabs`
All open editor tabs.

```json
{"method": "list_tabs", "params": {}}
// result: list of:
{
  "tab_id": "str",
  "path": "/absolute/path/to/file.e",
  "title": "file.e",
  "view": "e",          // "e" | "epa" | "debug"
  "language": "e",      // "e" | "cpp" | "python" | "epa" | "plain"
  "has_epa": false
}
```

#### `get_active_tab`
The currently focused tab.

```json
{"method": "get_active_tab", "params": {}}
// result: {"tab_id": "str", "path": "str", "view": "str"}
```

#### `get_file_content`
Live editor content for a tab. Prefer `tab_id`; fall back to `path`.

```json
{"method": "get_file_content", "params": {"tab_id": "abc"}}
// result: {"tab_id": "abc", "path": "/...", "content": "...", "language": "e"}
```

#### `get_epa_asm`
EPA assembly for a tab. Triggers a fresh compile if none exists yet.

```json
{"method": "get_epa_asm", "params": {"tab_id": "abc", "force_recompile": false}}
// result:
{
  "tab_id": "abc",
  "asm": "; EPA ASM text ...",
  "error": null,
  "compiled_at": 1716300000.0
}
```

#### `get_debug_state`
Trace/debug output for a tab.

```json
{"method": "get_debug_state", "params": {"tab_id": "abc"}}
// result: {"tab_id": "abc", "trace": "...", "meta": "...", "ingress_text": "...", "worker_id": "..."}
```

---

### 5.3 File system

#### `get_project`
Project root and top-level directory entries.

```json
{"method": "get_project", "params": {}}
// result: {"root": "/home/user/myproject", "entries": [{"name":"src","path":"/...","is_dir":true}, ...]}
```

#### `list_dir`
Directory listing for any path.

```json
{"method": "list_dir", "params": {"path": "/home/user/myproject/src"}}
// result: {"path": "...", "entries": [{"name":"main.e","path":"/...","is_dir":false}, ...]}
```

#### `read_file`
Read a file from disk. For live editor text use `get_file_content` instead.

```json
{"method": "read_file", "params": {"path": "/home/user/myproject/src/main.e", "max_bytes": 131072}}
// result: {"path": "...", "content": "...", "truncated": false}
```

---

### 5.4 Editor actions

#### `open_file`
Open a file in the editor. Returns the tab id (existing or new).

```json
{"method": "open_file", "params": {"path": "/absolute/path/to/file.e"}}
// result: {"tab_id": "abc", "already_open": false}
```

#### `switch_view`
Switch a tab between E source, EPA assembly, and debug view.

```json
{"method": "switch_view", "params": {"tab_id": "abc", "view": "epa"}}
// result: {"tab_id": "abc", "view": "epa"}
```

#### `set_file_content`
Replace the entire editor buffer. Does **not** save to disk.

```json
{"method": "set_file_content", "params": {"tab_id": "abc", "content": "fn main() {}"}}
// result: {"tab_id": "abc", "ok": true}
```

#### `save_file`
Flush the current editor content to disk.

```json
{"method": "save_file", "params": {"tab_id": "abc"}}
// result: {"tab_id": "abc", "path": "/...", "bytes_written": 1024}
```

#### `trigger_compile`
Compile a tab and wait synchronously for the result.

```json
{"method": "trigger_compile", "params": {"tab_id": "abc"}}
// result: {"tab_id": "abc", "asm": "...", "error": null, "compiled_at": 1716300000.0}
```

#### `set_active_tab`
Bring a tab into focus.

```json
{"method": "set_active_tab", "params": {"tab_id": "abc"}}
// result: {"tab_id": "abc"}
```

#### `close_tab`
Close an editor tab without saving.

```json
{"method": "close_tab", "params": {"tab_id": "abc"}}
// result: {"tab_id": "abc", "closed": true}
```

---

### 5.5 Surgical text editing

All line numbers are **1-indexed**. All column numbers are **0-indexed**
(column 0 = before the first character on the line). Edits are applied to the
live editor buffer and reflected in the UI immediately.

#### `editor_insert`
Insert text at a position (may contain newlines).

```json
{
  "method": "editor_insert",
  "params": {"tab_id": "abc", "line": 3, "col": 0, "text": "// added\n"}
}
// result: {"tab_id":"abc","cursor_line":4,"cursor_col":0,"lines_total":20,"chars_total":512}
```

#### `editor_delete_range`
Delete the half-open range `[from, to)`.

```json
{
  "method": "editor_delete_range",
  "params": {"tab_id": "abc", "from_line": 3, "from_col": 0, "to_line": 4, "to_col": 0}
}
// result: {"tab_id":"abc","deleted_chars":9,"lines_total":19,"chars_total":503}
```

#### `editor_replace_range`
Delete a range and insert replacement text in one call. To insert only: set
from and to to the same position. To delete only: pass `"text": ""`.

```json
{
  "method": "editor_replace_range",
  "params": {
    "tab_id": "abc",
    "from_line": 5, "from_col": 4,
    "to_line": 5,   "to_col": 12,
    "text": "newName"
  }
}
// result: {"tab_id":"abc","cursor_line":5,"cursor_col":11,"lines_total":19,"chars_total":506}
```

#### `editor_get_range`
Read text from a range without modifying it.

```json
{
  "method": "editor_get_range",
  "params": {"tab_id": "abc", "from_line": 1, "from_col": 0, "to_line": 3, "to_col": 0}
}
// result: {"tab_id":"abc","text":"line1\nline2\n","chars":12}
```

---

### 5.6 Project management

#### `open_project`
Open a project directory. Populates the nav tree, sets the window title, and
makes the toolbar visible. If the directory contains
`.elaraproject/project.json` that file is used for project metadata; otherwise
it is treated as an anonymous project.

```json
{"method": "open_project", "params": {"path": "/home/user/myproject"}}
// result: {"project_root": "/home/user/myproject", "project_name": "myproject"}
```

---

### 5.7 Navigation tree

#### `get_nav_tree`
Full tree structure. Each node: `{id, label, expanded, children:[...]}`.
`id` is the absolute file/directory path. Leaf nodes (files) have no
`children` key.

```json
{"method": "get_nav_tree", "params": {}}
// result: list of root nodes
```

#### `set_node_expanded`
Expand or collapse a tree node.

```json
{"method": "set_node_expanded", "params": {"node_id": "/home/user/myproject/src", "expanded": true}}
// result: {"node_id": "/...", "expanded": true, "found": true}
```

#### `tree_open_file`
Open a file from the nav tree into a permanent editor tab. Equivalent to
double-clicking it in the tree UI.

```json
{"method": "tree_open_file", "params": {"path": "/home/user/myproject/src/main.e"}}
// result: {"tab_id": "abc", "path": "/..."}
```

---

### 5.8 Exception log

`app.py` captures every uncaught exception, thread exception, and connection
error into a ring buffer (max 200 entries). Each entry includes the last 30
event log entries at the time of the exception so you can reconstruct context.

#### `get_exceptions`

```json
{"method": "get_exceptions", "params": {"limit": 10}}
// result: list of:
{
  "ts": 1716300000.0,
  "context": "uncaught",
  "type": "AttributeError",
  "message": "'NoneType' object has no attribute 'foo'",
  "traceback": "Traceback (most recent call last):\n  ...",
  "recent_events": [...]
}
```

`limit=0` returns all entries.

#### `clear_exceptions`

```json
{"method": "clear_exceptions", "params": {}}
// result: {"cleared": 3}
```

---

### 5.9 Event log

Every significant IDE event is written to a ring buffer (max 2000 entries) and
to a per-session JSONL file in `artifacts/event-YYYYMMDD-HHMMSS.jsonl`. The
event log is your primary debugging tool when the UI crashes: it tells you
exactly what was happening on both sides of the RPC boundary.

**Event types:**

| Type                | When it fires                                              |
|---------------------|------------------------------------------------------------|
| `session_start`     | `app.py` successfully connected to `elaraui-server`        |
| `ui_rpc_out`        | Every call sent **to** the UI RPC (`client.call(...)`)     |
| `ui_rpc_out_error`  | A `ui_rpc_out` call that raised an exception               |
| `ui_event`          | Every event received **from** the UI (button press, etc.)  |
| `ui_disconnect`     | The UI RPC connection closed                               |
| `ai_rpc_in`         | Every call received on the AI RPC (except `ping`/`help`)   |
| `exception`         | Every exception captured by the exception log              |

#### `get_event_log`

```json
{"method": "get_event_log", "params": {"limit": 50}}
// result: list of event entries, newest last
```

Filter by type:
```json
{"method": "get_event_log", "params": {"limit": 20, "type_filter": "ui_rpc_out"}}
```

`limit=0` returns all entries in the ring buffer.

#### `clear_event_log`
Clears the in-memory ring buffer. Does **not** delete on-disk JSONL files.

```json
{"method": "clear_event_log", "params": {}}
// result: {"cleared": 142}
```

---

### 5.10 UI server management

#### `get_ui_status`
Check whether the UI is connected and whether `app.py` is managing the
`elaraui-server` process.

```json
{"method": "get_ui_status", "params": {}}
// result:
{
  "connected": true,
  "managed_process": true,
  "process_alive": true,
  "process_pid": 12345,
  "server_cmd": ["/path/to/elaraui-server", "--port", "18777"],
  "recent_output": ["[stdout] listening on :18777", ...]
}
```

#### `restart_ui`
Kill the current `elaraui-server` (if managed by `app.py`) and relaunch it.
`app.py` will reconnect automatically. Poll `get_ui_status` until
`connected` is `true`.

```json
{"method": "restart_ui", "params": {"use_gdb": false}}
// result: {"pid": 12346, "cmd": [...], "use_gdb": false}
```

**Crash capture with gdb:**

```json
{"method": "restart_ui", "params": {"use_gdb": true}}
```

This wraps the server with `gdb --batch -ex "handle SIGPIPE nostop noprint"
-ex "run" -ex "thread apply all bt full"`. When the server crashes, gdb
prints a full backtrace and exits. The output is collected in `recent_output`
and readable via `get_ui_status`.

Workflow:
1. `restart_ui` with `use_gdb=true`
2. Replay the failing sequence of `ui_call` calls (from `get_event_log`)
3. After the crash, call `get_ui_status` and read `recent_output` for the backtrace

---

### 5.11 Raw UI proxy

#### `ui_call`
Forward any `ui.*` call through `app.py`'s live connection. This is the
**only** way to drive the UI directly — `elaraui-server` rejects second
connections.

`method` is the short name without the `ui.` prefix.

```json
{
  "method": "ui_call",
  "params": {
    "method": "snapshot",
    "params": {}
  }
}
// result: whatever the UI RPC call returns
```

Returns `ui_unavailable` if `app.py` is not yet connected to the UI.

---

## 6. Common workflows

### Read the active file and suggest a change

```python
state = call("get_ide_state")["result"]
tab_id = state["active_tab"]
if not tab_id:
    raise RuntimeError("no active tab")

content = call("get_file_content", tab_id=tab_id)["result"]["content"]

# ... analyse content, compute replacement ...

call("editor_replace_range", tab_id=tab_id,
     from_line=10, from_col=4, to_line=10, to_col=20,
     text="new_function_name")
call("save_file", tab_id=tab_id)
```

### Open a project and browse its files

```python
call("open_project", path="/home/user/myproject")

tree = call("get_nav_tree")["result"]

# Expand the src directory
call("set_node_expanded", node_id="/home/user/myproject/src", expanded=True)

# Open a source file
r = call("tree_open_file", path="/home/user/myproject/src/main.e")
tab_id = r["result"]["tab_id"]
```

### Compile and inspect EPA assembly

```python
r = call("trigger_compile", tab_id=tab_id)["result"]
if r["error"]:
    print("Compile error:", r["error"])
else:
    print(r["asm"])
    # Switch the UI tab to show the assembly
    call("switch_view", tab_id=tab_id, view="epa")
```

### Diagnose a UI crash

```python
# 1. Check current status
status = call("get_ui_status")["result"]
if not status["connected"]:
    # 2. Fetch recent events to understand what was happening
    events = call("get_event_log", limit=50, type_filter="ui_rpc_out")["result"]
    for e in events:
        print(e["ts"], e["method"], e.get("params", {}))

    # 3. Fetch any Python exceptions
    excs = call("get_exceptions", limit=5)["result"]
    for ex in excs:
        print(ex["context"], ex["type"], ex["message"])
        print(ex["traceback"])

    # 4. Restart under gdb
    call("restart_ui", use_gdb=True)

    # 5. Poll until reconnected
    import time
    for _ in range(30):
        s = call("get_ui_status")["result"]
        if s["connected"]:
            break
        time.sleep(1)

    # 6. Read crash output
    s = call("get_ui_status")["result"]
    print("\n".join(s["recent_output"]))
```

---

## 7. Important constraints

- **One connection at a time.** If you open a second TCP connection to port
  18779 it will be queued but not served until you close the first one. There
  is no need for two simultaneous AI clients.

- **`set_file_content` vs surgical edits.** `set_file_content` replaces the
  entire buffer atomically — it is fine for small files but will discard undo
  history. Prefer `editor_replace_range` (or `editor_insert` / `editor_delete_range`)
  for targeted changes so the user can Ctrl-Z if needed.

- **`save_file` is explicit.** No method auto-saves. Always call `save_file`
  after making changes you want persisted.

- **`ui_call` requires a live UI connection.** If `app.py` has not yet
  connected to `elaraui-server` (or the UI crashed), `ui_call` returns
  `ui_unavailable`. Check `get_ui_status` first if in doubt.

- **Event log ring buffer is in-memory only.** `clear_event_log` clears RAM;
  the JSONL files in `artifacts/` are never deleted by the RPC. They are the
  authoritative record and survive restarts.

- **The `content` field is excluded from `ai_rpc_in` event log entries** to
  keep the log compact. If you need to audit what content was sent, read the
  JSONL file directly.
