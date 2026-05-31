"""
ai_rpc.py — AI-dedicated logic-side RPC server for the EPA-IDE.

Listens on a separate TCP port (default 18779). Accepts one client at a time.
Protocol: newline-delimited JSON over TCP.
Connections are long-lived and may carry many requests/responses.

  Request:  {"method": "method_name", "params": {...}}
  Response: {"ok": true,  "result": <value>}
         or {"ok": false, "error": "message"}

The UI RPC client (port 18777) remains untouched — this server operates
entirely on the logic/state side and drives the UI only through the shared
client_ref callback, using the same _deferred pattern as app.py.

Start with:
    server = AiRpcServer(port=18779, ide=ide_bindings)
    server.start()   # background thread, non-blocking
    server.stop()    # on shutdown
"""

import json
import socket
import threading
import time
from pathlib import Path


_AI_RPC_PROTOCOL_VERSION = 2
_AI_RPC_KEEPALIVE_SECONDS = 15


def _enable_socket_keepalive(sock: socket.socket) -> None:
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    except OSError:
        return
    for opt_name, value in (
        ("TCP_KEEPIDLE", 30),
        ("TCP_KEEPINTVL", 10),
        ("TCP_KEEPCNT", 3),
    ):
        opt = getattr(socket, opt_name, None)
        if opt is None:
            continue
        try:
            sock.setsockopt(socket.IPPROTO_TCP, opt, value)
        except OSError:
            pass


# ---------------------------------------------------------------------------
# Help text — the canonical reference for any AI connecting to this server.
# Updated here whenever the API changes.
# ---------------------------------------------------------------------------

HELP = {
    "overview": (
        "EPA-IDE AI RPC — logic-side control interface. "
        "One client at a time. All calls are synchronous request/response. "
        "Protocol: newline-delimited JSON (one JSON object per line). "
        "All responses have 'ok' (bool) and either 'result' or 'error'."
    ),
    "connection": {
        "default_port": 18779,
        "encoding": "UTF-8",
        "framing": "newline-delimited JSON (\\n after each object)",
        "concurrency": "one active client at a time; new connections are queued",
        "session_model": "keep one TCP connection open and send multiple requests over it",
        "recommended_keepalive_seconds": _AI_RPC_KEEPALIVE_SECONDS,
    },
    "methods": {
        # ---- Query --------------------------------------------------------
        "hello": {
            "desc": "Return protocol/session capabilities for a long-lived AI RPC connection.",
            "params": {},
            "result": {
                "protocol_version": _AI_RPC_PROTOCOL_VERSION,
                "keepalive_seconds": _AI_RPC_KEEPALIVE_SECONDS,
                "multi_request_connection": True,
                "server_time": "float — unix timestamp",
            },
        },
        "help": {
            "desc": "Return this help document.",
            "params": {},
            "result": "This object.",
        },
        "ping": {
            "desc": "Liveness check for keepalive sessions.",
            "params": {},
            "result": {"pong": True},
        },
        "get_ide_state": {
            "desc": (
                "Return a snapshot of the full IDE state: open tabs, active tab, "
                "project root, AI chat state, theme."
            ),
            "params": {},
            "result": {
                "active_tab": "tab_id or ''",
                "tabs": "list — see list_tabs",
                "project_root": "str or null",
                "theme": "'dark' | 'light'",
                "ai_model": "str",
                "ai_generating": "bool",
            },
        },
        "list_tabs": {
            "desc": "List all open editor tabs.",
            "params": {},
            "result": [
                {
                    "tab_id": "str",
                    "path": "str — absolute path",
                    "title": "str",
                    "view": "'e' | 'epa' | 'debug'",
                    "language": "str — 'e', 'cpp', 'python', 'epa', 'plain'",
                    "has_epa": "bool — true if EPA ASM was compiled this session",
                }
            ],
        },
        "get_active_tab": {
            "desc": "Return info about the currently focused editor tab.",
            "params": {},
            "result": {"tab_id": "str", "path": "str", "view": "str"},
        },
        "get_file_content": {
            "desc": "Return the current text content of an editor tab.",
            "params": {
                "tab_id": "str (preferred) — the tab identifier",
                "path": "str (fallback) — absolute file path if tab_id unknown",
            },
            "result": {
                "tab_id": "str",
                "path": "str",
                "content": "str — current editor text",
                "language": "str",
            },
        },
        "get_epa_asm": {
            "desc": (
                "Return the last-compiled EPA assembly for a tab. "
                "If not yet compiled, triggers a fresh compile first."
            ),
            "params": {
                "tab_id": "str",
                "force_recompile": "bool (optional, default false)",
            },
            "result": {
                "tab_id": "str",
                "asm": "str — EPA ASM text or ''",
                "error": "str or null — compiler error if any",
                "compiled_at": "float — unix timestamp or 0",
            },
        },
        "get_debug_state": {
            "desc": "Return debug/trace state for a tab.",
            "params": {"tab_id": "str"},
            "result": {
                "tab_id": "str",
                "trace": "str — trace log text",
                "meta": "str — debug meta text",
                "ingress_text": "str",
                "worker_id": "str",
            },
        },
        "get_project": {
            "desc": "Return project root path and top-level directory listing.",
            "params": {},
            "result": {
                "root": "str or null",
                "entries": "list of {name, path, is_dir}",
            },
        },
        "list_dir": {
            "desc": "List directory contents.",
            "params": {"path": "str — absolute directory path"},
            "result": {"path": "str", "entries": "list of {name, path, is_dir}"},
        },
        "read_file": {
            "desc": (
                "Read any file from disk (not just open tabs). "
                "Returns the raw text. Use get_file_content for live editor state."
            ),
            "params": {
                "path": "str — absolute path",
                "max_bytes": "int (optional, default 131072)",
            },
            "result": {"path": "str", "content": "str", "truncated": "bool"},
        },
        # ---- Actions ------------------------------------------------------
        "open_file": {
            "desc": "Open a file in the editor. Returns the new or existing tab_id.",
            "params": {"path": "str — absolute file path"},
            "result": {"tab_id": "str", "already_open": "bool"},
        },
        "switch_view": {
            "desc": "Switch a tab's view between E source, EPA assembly, and debug.",
            "params": {
                "tab_id": "str",
                "view": "'e' | 'epa' | 'debug'",
            },
            "result": {"tab_id": "str", "view": "str"},
        },
        "set_file_content": {
            "desc": (
                "Replace the editor content for a tab. "
                "Does NOT save to disk — use save_file for that."
            ),
            "params": {
                "tab_id": "str",
                "content": "str — new text",
            },
            "result": {"tab_id": "str", "ok": True},
        },
        "save_file": {
            "desc": "Save the current editor content of a tab to its path on disk.",
            "params": {"tab_id": "str"},
            "result": {"tab_id": "str", "path": "str", "bytes_written": "int"},
        },
        "trigger_compile": {
            "desc": (
                "Trigger EPA compilation for a tab and wait for the result. "
                "Returns the assembled EPA ASM text or an error."
            ),
            "params": {"tab_id": "str"},
            "result": {
                "tab_id": "str",
                "asm": "str",
                "error": "str or null",
                "compiled_at": "float",
            },
        },
        "set_active_tab": {
            "desc": "Bring a tab into focus.",
            "params": {"tab_id": "str"},
            "result": {"tab_id": "str"},
        },
        "close_tab": {
            "desc": "Close an editor tab (does not save).",
            "params": {"tab_id": "str"},
            "result": {"tab_id": "str", "closed": True},
        },
        # ---- Nav tree -----------------------------------------------------
        "get_nav_tree": {
            "desc": (
                "Return the full navigation tree structure. "
                "Each node: {id, label, expanded, children:[...]}. "
                "id is the absolute file/directory path. "
                "Leaf nodes (files) have no children key."
            ),
            "params": {},
            "result": "list of root nodes",
        },
        "set_node_expanded": {
            "desc": (
                "Expand or collapse a tree node. "
                "Pass the node id (absolute path) and desired expanded state. "
                "The UI tree is rebuilt immediately."
            ),
            "params": {
                "node_id": "str — absolute path matching a tree node id",
                "expanded": "bool",
            },
            "result": {"node_id": "str", "expanded": "bool", "found": "bool"},
        },
        "tree_open_file": {
            "desc": (
                "Open a file from the nav tree into a permanent editor tab. "
                "Equivalent to double-clicking the file in the tree."
            ),
            "params": {"path": "str — absolute file path"},
            "result": {"tab_id": "str", "path": "str"},
        },
        # ---- Editor text operations ---------------------------------------
        "editor_insert": {
            "desc": (
                "Insert text at a position in an editor tab. "
                "Lines are 1-indexed; columns are 0-indexed (0 = before first char). "
                "The UI editor is updated immediately. "
                "Returns the cursor position after the inserted text."
            ),
            "params": {
                "tab_id": "str",
                "line": "int — 1-indexed line number",
                "col": "int — 0-indexed column",
                "text": "str — text to insert (may contain newlines)",
            },
            "result": {
                "tab_id": "str",
                "cursor_line": "int — 1-indexed line after insert",
                "cursor_col": "int — 0-indexed column after insert",
                "lines_total": "int",
                "chars_total": "int",
            },
        },
        "editor_delete_range": {
            "desc": (
                "Delete text between two positions. "
                "Lines are 1-indexed; columns are 0-indexed. "
                "The range is (from_line, from_col) inclusive to (to_line, to_col) exclusive."
            ),
            "params": {
                "tab_id": "str",
                "from_line": "int",
                "from_col": "int",
                "to_line": "int",
                "to_col": "int",
            },
            "result": {
                "tab_id": "str",
                "deleted_chars": "int",
                "lines_total": "int",
                "chars_total": "int",
            },
        },
        "editor_replace_range": {
            "desc": (
                "Replace a text range with new content. "
                "Combines delete + insert in one call. "
                "To insert without deleting: set from/to to the same position. "
                "To delete without inserting: pass text=''."
            ),
            "params": {
                "tab_id": "str",
                "from_line": "int — 1-indexed",
                "from_col": "int — 0-indexed",
                "to_line": "int — 1-indexed",
                "to_col": "int — 0-indexed",
                "text": "str — replacement ('' to delete only)",
            },
            "result": {
                "tab_id": "str",
                "cursor_line": "int",
                "cursor_col": "int",
                "lines_total": "int",
                "chars_total": "int",
            },
        },
        "editor_get_range": {
            "desc": "Read text from a range without modifying it.",
            "params": {
                "tab_id": "str",
                "from_line": "int — 1-indexed",
                "from_col": "int — 0-indexed",
                "to_line": "int — 1-indexed",
                "to_col": "int — 0-indexed",
            },
            "result": {"tab_id": "str", "text": "str", "chars": "int"},
        },
        # ---- Ingress profiles -----------------------------------------------
        "list_ingress_types": {
            "desc": (
                "Return all EPA message types discovered by scanning .em files in the "
                "current project's epa/ directory, along with their fields. "
                "Use this to know what types exist and what fields to fill."
            ),
            "params": {},
            "result": [
                {
                    "type_name": "str — e.g. 'FrameTick'",
                    "fields": "list[str] — field names in declaration order",
                }
            ],
        },
        "list_ingress_profiles": {
            "desc": (
                "Return all saved ingress profiles for a given type. "
                "Profiles are stored as JSON files under "
                "{project}/epa/profiles/{type_name}/."
            ),
            "params": {"type_name": "str"},
            "result": [
                {
                    "name": "str — profile name (filename stem)",
                    "path": "str — absolute path to the .json file",
                }
            ],
        },
        "get_ingress_profile": {
            "desc": "Read the field values of a saved ingress profile.",
            "params": {
                "type_name": "str",
                "profile_name": "str",
            },
            "result": {
                "type_name": "str",
                "profile_name": "str",
                "fields": "dict — field_name → value (str)",
                "path": "str",
            },
        },
        "save_ingress_profile": {
            "desc": (
                "Create or overwrite an ingress profile. "
                "Fields not present in the message type definition are ignored. "
                "After saving, the UI profile list is refreshed automatically. "
                "Values should be decimal or hex (0x…) integer strings, or plain integers."
            ),
            "params": {
                "type_name": "str — e.g. 'FrameTick'",
                "profile_name": "str — name for this profile, e.g. 'default' or 'stress_test'",
                "fields": "dict — {field_name: value} — string or int values",
            },
            "result": {
                "type_name": "str",
                "profile_name": "str",
                "path": "str — absolute path written",
                "fields_written": "int",
            },
        },
        "delete_ingress_profile": {
            "desc": "Delete a saved ingress profile and refresh the UI list.",
            "params": {
                "type_name": "str",
                "profile_name": "str",
            },
            "result": {"deleted": "bool", "path": "str"},
        },
        # ---- Project management ---------------------------------------------
        "open_project": {
            "desc": (
                "Open a project directory. Populates the nav tree, sets the "
                "window title, and makes the toolbar visible. The path must "
                "contain a .elaraproject/project.json file or it will be "
                "treated as an anonymous project."
            ),
            "params": {"path": "str — absolute path to project directory"},
            "result": {"project_root": "str", "project_name": "str"},
        },
        # ---- Exception log --------------------------------------------------
        "get_exceptions": {
            "desc": (
                "Return recent exceptions captured by the IDE process. "
                "Includes uncaught exceptions, thread exceptions, and "
                "connection errors. Use this to diagnose crashes and "
                "unexpected behaviour."
            ),
            "params": {"limit": "int — max entries to return (default 50, 0=all)"},
            "result": [
                {
                    "ts": "float — unix timestamp",
                    "context": "str — where the exception was caught",
                    "type": "str — exception class name",
                    "message": "str",
                    "traceback": "str — full traceback text",
                }
            ],
        },
        "clear_exceptions": {
            "desc": "Clear the exception log.",
            "params": {},
            "result": {"cleared": "int — number of entries removed"},
        },
        # ---- UI server management -------------------------------------------
        "get_ui_status": {
            "desc": (
                "Return the current UI connection and server process status. "
                "connected: whether the RPC client is live. "
                "managed_process: whether app.py launched the elaraui-server. "
                "process_alive: whether that process is still running. "
                "recent_output: last captured stdout/stderr lines from the server."
            ),
            "params": {},
            "result": {
                "connected": "bool",
                "managed_process": "bool",
                "process_alive": "bool",
                "process_pid": "int or null",
                "server_cmd": "list[str]",
                "recent_output": "list[str]",
            },
        },
        "restart_ui": {
            "desc": (
                "Kill the current elaraui-server (if managed) and launch a new one. "
                "app.py will reconnect automatically once the new server is ready. "
                "Set use_gdb=true to run under gdb --batch, which captures a full "
                "backtrace on crash in recent_output (readable via get_ui_status). "
                "After calling this, poll get_ui_status until connected=true."
            ),
            "params": {
                "use_gdb": "bool — wrap with gdb --batch for crash capture (default false)",
            },
            "result": {"pid": "int", "cmd": "list[str]", "use_gdb": "bool"},
        },
        "reload_ui_document": {
            "desc": (
                "Reload the current IDE UI document into the connected UI head. "
                "Useful after restart_ui when the IDE is running in --repl mode."
            ),
            "params": {},
            "result": {"loaded": True},
        },
        "trigger_action": {
            "desc": (
                "Invoke an IDE action directly without synthesizing a UI click. "
                "Use this for AI-driven debug testing when ui.clickWidget would be reentrant."
            ),
            "params": {
                "action": "str — e.g. debug.cpp.pause",
                "target": "str optional — widget id, defaults to action",
                "payload": "dict optional — extra event payload",
            },
            "result": {"received": True},
        },
        # ---- Event log ------------------------------------------------------
        "get_event_log": {
            "desc": (
                "Return recent IDE event log entries. Covers ui_rpc_out, ui_event, "
                "ui_disconnect, session_start, exception, ai_rpc_in, and "
                "ui_rpc_out_error events. Use this to reconstruct what was happening "
                "when the UI crashed so you can replay it under gdb."
            ),
            "params": {
                "limit": "int — max entries to return (default 100, 0=all)",
                "type_filter": "str — if given, only return entries of this type",
            },
            "result": [
                {
                    "ts": "float — unix timestamp",
                    "type": "str — event type",
                    "...": "event-specific fields",
                }
            ],
        },
        "clear_event_log": {
            "desc": "Clear the in-memory event log ring buffer (does not delete log files).",
            "params": {},
            "result": {"cleared": "int — number of entries removed"},
        },
        # ---- Clipboard diagnostic -------------------------------------------
        "probe_clipboard": {
            "desc": (
                "Step through select, copy, and paste operations on editor widgets "
                "and return a step-by-step report of each UI call and its result. "
                "Use this to diagnose why copy/paste is not working. "
                "source_tab_id: the tab to copy from. "
                "dest_tab_id: the tab to paste into (optional). "
                "Each step records the UI method called, its params, and the result "
                "or error returned. A snapshotWidget is taken of both editors so you "
                "can inspect focus and selection state."
            ),
            "params": {
                "source_tab_id": "str — tab to copy from",
                "dest_tab_id": "str (optional) — tab to paste into",
            },
            "result": {
                "steps": "list — each step: {step, ok, method?, params?, result?, error?}",
                "source_tab_id": "str",
                "source_editor_target": "str — UI widget id used for copy",
                "dest_tab_id": "str or null",
                "dest_editor_target": "str or null — UI widget id used for paste",
            },
        },
        # ---- Raw UI proxy ---------------------------------------------------
        "ui_call": {
            "desc": (
                "Proxy a raw ui.* RPC call through the established logic-side "
                "UI connection. The UI RPC server only accepts the one connection "
                "from the logic side; this is the only way to drive the UI directly. "
                "method must be the short name without the 'ui.' prefix "
                "(e.g. 'setText', 'setVisible', 'snapshot'). "
                "params is an arbitrary dict forwarded verbatim."
            ),
            "params": {
                "method": "str — UI RPC method name (without 'ui.' prefix)",
                "params": "dict — forwarded to the UI RPC call",
            },
            "result": "whatever the UI RPC call returns",
        },
    },
    "errors": {
        "unknown_method": "The method name was not recognised.",
        "missing_param": "A required parameter was absent.",
        "no_such_tab": "No open tab matches the given tab_id or path.",
        "compile_error": "EPA compilation failed; check result.error.",
        "ui_unavailable": "The UI RPC client is not yet connected.",
        "io_error": "File-system operation failed.",
    },
}


# ---------------------------------------------------------------------------
# AiRpcServer
# ---------------------------------------------------------------------------

class AiRpcServer:
    """Logic-side AI RPC server. Runs in background threads."""

    def __init__(self, port: int, ide: "IdeBindings"):
        self._port = port
        self._ide = ide
        self._server_sock: socket.socket | None = None
        self._accept_thread: threading.Thread | None = None
        self._lock = threading.Lock()
        self._running = False

    def start(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        _enable_socket_keepalive(sock)
        sock.bind(("127.0.0.1", self._port))
        sock.listen(4)
        sock.settimeout(1.0)
        self._server_sock = sock
        self._running = True
        self._accept_thread = threading.Thread(target=self._accept_loop, daemon=False,
                                               name="ai-rpc-accept")
        self._accept_thread.start()
        print(json.dumps({"ai_rpc_listening": f"127.0.0.1:{self._port}"}), flush=True)

    def stop(self):
        self._running = False
        if self._server_sock:
            try:
                self._server_sock.close()
            except Exception:
                pass
        if self._accept_thread and self._accept_thread.is_alive():
            self._accept_thread.join(timeout=3.0)

    def _accept_loop(self):
        while self._running:
            try:
                conn, addr = self._server_sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            print(json.dumps({"ai_rpc_client": f"{addr[0]}:{addr[1]}"}), flush=True)
            t = threading.Thread(target=self._handle_client, args=(conn, addr),
                                 daemon=True, name=f"ai-rpc-{addr[1]}")
            t.start()

    def _handle_client(self, conn: socket.socket, addr):
        _enable_socket_keepalive(conn)
        conn.settimeout(None)
        buf = b""
        try:
            while self._running:
                try:
                    chunk = conn.recv(4096)
                except OSError:
                    break
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        req = json.loads(line.decode("utf-8"))
                    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
                        self._send(conn, {"ok": False, "error": f"json parse error: {exc}"})
                        continue
                    resp = self._dispatch(req)
                    self._send(conn, resp)
        finally:
            try:
                conn.close()
            except Exception:
                pass
            print(json.dumps({"ai_rpc_disconnect": f"{addr[0]}:{addr[1]}"}), flush=True)

    def _send(self, conn: socket.socket, obj: dict):
        try:
            conn.sendall((json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8"))
        except OSError:
            pass

    def _dispatch(self, req: dict) -> dict:
        method = req.get("method", "")
        params = req.get("params") or {}
        if not isinstance(params, dict):
            params = {}
        # Log every incoming call (skip help/ping to keep the log lean)
        if method not in ("help", "ping"):
            self._ide.log_event("ai_rpc_in", method=method,
                                params={k: v for k, v in params.items()
                                        if k not in ("content",)} if params else {})
        handler = getattr(self, f"_m_{method}", None)
        if handler is None:
            return {"ok": False, "error": f"unknown_method: {method!r}"}
        try:
            result = handler(params)
            return {"ok": True, "result": result}
        except _RpcError as exc:
            return {"ok": False, "error": str(exc)}
        except Exception as exc:
            return {"ok": False, "error": f"internal: {exc}"}

    # -----------------------------------------------------------------------
    # Method implementations
    # -----------------------------------------------------------------------

    def _m_hello(self, _params):
        return {
            "protocol_version": _AI_RPC_PROTOCOL_VERSION,
            "keepalive_seconds": _AI_RPC_KEEPALIVE_SECONDS,
            "multi_request_connection": True,
            "server_time": time.time(),
        }

    def _m_help(self, _params):
        return HELP

    def _m_ping(self, _params):
        return {"pong": True, "ts": time.time()}

    def _m_get_ide_state(self, _params):
        ide = self._ide
        return {
            "active_tab": ide.app_state.get("active_editor_tab", ""),
            "tabs": self._tab_list_snapshot(),
            "project_root": ide.app_state.get("project_root"),
            "theme": ide.app_state.get("theme", "dark"),
            "ai_model": ide.ai_state.get("model", ""),
            "ai_generating": ide.ai_state.get("generating", False),
        }

    def _m_list_tabs(self, _params):
        return self._tab_list_snapshot()

    def _tab_list_snapshot(self):
        ide = self._ide
        result = []
        for t in ide.tab_list:
            tab_id = t.get("tab_id", "")
            state = ide.editor_state.get(tab_id, {})
            result.append({
                "tab_id": tab_id,
                "path": t.get("path", ""),
                "title": t.get("title", Path(t.get("path", "")).name),
                "view": state.get("view", "e"),
                "language": ide.language_for_path(t.get("path", "")),
                "has_epa": bool(state.get("epa_text")),
            })
        return result

    def _m_get_active_tab(self, _params):
        ide = self._ide
        tab_id = ide.app_state.get("active_editor_tab", "")
        t = next((t for t in ide.tab_list if t.get("tab_id") == tab_id), None)
        state = ide.editor_state.get(tab_id, {})
        return {
            "tab_id": tab_id,
            "path": t.get("path", "") if t else "",
            "view": state.get("view", "e"),
        }

    def _m_get_file_content(self, params):
        ide = self._ide
        tab_id = params.get("tab_id", "")
        path = params.get("path", "")
        t, state = self._resolve_tab(tab_id, path)
        content = state.get("source_text", "")
        if not content:
            # Try reading from disk as fallback
            p = Path(t.get("path", ""))
            if p.is_file():
                try:
                    content = p.read_text(encoding="utf-8")
                except OSError:
                    content = ""
        return {
            "tab_id": t["tab_id"],
            "path": t.get("path", ""),
            "content": content,
            "language": ide.language_for_path(t.get("path", "")),
        }

    def _m_get_epa_asm(self, params):
        ide = self._ide
        tab_id = params.get("tab_id", "")
        force = bool(params.get("force_recompile", False))
        t, state = self._resolve_tab(tab_id, "")
        tid = t["tab_id"]
        if force or not state.get("epa_text"):
            result = ide.compile_tab(tid)
            return {
                "tab_id": tid,
                "asm": result.get("asm", ""),
                "error": result.get("error"),
                "compiled_at": result.get("compiled_at", 0.0),
            }
        return {
            "tab_id": tid,
            "asm": state.get("epa_text", ""),
            "error": state.get("epa_error"),
            "compiled_at": state.get("epa_compiled_at", 0.0),
        }

    def _m_trigger_compile(self, params):
        ide = self._ide
        tab_id = params.get("tab_id")
        if not tab_id:
            raise _RpcError("missing_param: tab_id")
        t, _ = self._resolve_tab(tab_id, "")
        result = ide.compile_tab(t["tab_id"])
        return result

    def _m_get_debug_state(self, params):
        ide = self._ide
        tab_id = params.get("tab_id", "")
        t, state = self._resolve_tab(tab_id, "")
        return {
            "tab_id": t["tab_id"],
            "trace": state.get("debug_trace", ""),
            "meta": state.get("debug_meta", ""),
            "ingress_text": state.get("debug_ingress", ""),
            "worker_id": state.get("debug_worker_id", ""),
        }

    def _m_get_project(self, _params):
        ide = self._ide
        root = ide.app_state.get("project_root")
        entries = []
        if root:
            try:
                for p in sorted(Path(root).iterdir(), key=lambda x: (not x.is_dir(), x.name.lower())):
                    entries.append({"name": p.name, "path": str(p), "is_dir": p.is_dir()})
            except OSError:
                pass
        return {"root": root, "entries": entries}

    def _m_list_dir(self, params):
        path = params.get("path")
        if not path:
            raise _RpcError("missing_param: path")
        p = Path(path)
        if not p.is_dir():
            raise _RpcError(f"io_error: not a directory: {path}")
        entries = []
        try:
            for child in sorted(p.iterdir(), key=lambda x: (not x.is_dir(), x.name.lower())):
                entries.append({"name": child.name, "path": str(child), "is_dir": child.is_dir()})
        except OSError as exc:
            raise _RpcError(f"io_error: {exc}")
        return {"path": str(p), "entries": entries}

    def _m_read_file(self, params):
        path = params.get("path")
        if not path:
            raise _RpcError("missing_param: path")
        max_bytes = int(params.get("max_bytes", 131072))
        p = Path(path)
        if not p.is_file():
            raise _RpcError(f"io_error: not a file: {path}")
        try:
            raw = p.read_bytes()
        except OSError as exc:
            raise _RpcError(f"io_error: {exc}")
        truncated = len(raw) > max_bytes
        raw = raw[:max_bytes]
        try:
            content = raw.decode("utf-8")
        except UnicodeDecodeError:
            content = raw.decode("latin-1")
        return {"path": str(p), "content": content, "truncated": truncated}

    def _m_open_file(self, params):
        path = params.get("path")
        if not path:
            raise _RpcError("missing_param: path")
        ide = self._ide
        existing = next((t for t in ide.tab_list if t.get("path") == str(path)), None)
        if existing:
            return {"tab_id": existing["tab_id"], "already_open": True}
        tab_id = ide.open_file(str(path))
        if not tab_id:
            raise _RpcError(f"io_error: could not open {path}")
        return {"tab_id": tab_id, "already_open": False}

    def _m_switch_view(self, params):
        ide = self._ide
        tab_id = params.get("tab_id")
        view = params.get("view")
        if not tab_id:
            raise _RpcError("missing_param: tab_id")
        if view not in ("e", "epa", "debug"):
            raise _RpcError("missing_param: view must be 'e', 'epa', or 'debug'")
        self._resolve_tab(tab_id, "")
        ide.switch_view(tab_id, view)
        return {"tab_id": tab_id, "view": view}

    def _m_set_file_content(self, params):
        ide = self._ide
        tab_id = params.get("tab_id")
        content = params.get("content")
        if not tab_id:
            raise _RpcError("missing_param: tab_id")
        if content is None:
            raise _RpcError("missing_param: content")
        t, state = self._resolve_tab(tab_id, "")
        ide.set_editor_content(t["tab_id"], str(content))
        return {"tab_id": t["tab_id"], "ok": True}

    def _m_save_file(self, params):
        ide = self._ide
        tab_id = params.get("tab_id")
        if not tab_id:
            raise _RpcError("missing_param: tab_id")
        t, state = self._resolve_tab(tab_id, "")
        path = t.get("path", "")
        if not path:
            raise _RpcError("io_error: tab has no associated file path")
        content = state.get("source_text", "")
        try:
            Path(path).write_text(content, encoding="utf-8")
        except OSError as exc:
            raise _RpcError(f"io_error: {exc}")
        return {"tab_id": t["tab_id"], "path": path, "bytes_written": len(content.encode("utf-8"))}

    def _m_set_active_tab(self, params):
        ide = self._ide
        tab_id = params.get("tab_id")
        if not tab_id:
            raise _RpcError("missing_param: tab_id")
        t, _ = self._resolve_tab(tab_id, "")
        ide.set_active_tab(t["tab_id"])
        return {"tab_id": t["tab_id"]}

    def _m_close_tab(self, params):
        ide = self._ide
        tab_id = params.get("tab_id")
        if not tab_id:
            raise _RpcError("missing_param: tab_id")
        t, _ = self._resolve_tab(tab_id, "")
        ide.close_tab(t["tab_id"])
        return {"tab_id": t["tab_id"], "closed": True}

    # ---- Nav tree ---------------------------------------------------------

    def _m_get_nav_tree(self, _params):
        return self._ide.get_nav_tree()

    def _m_set_node_expanded(self, params):
        node_id = params.get("node_id", "")
        expanded = bool(params.get("expanded", True))
        if not node_id:
            raise _RpcError("missing_param: node_id")
        found = self._ide.set_node_expanded(node_id, expanded)
        return {"node_id": node_id, "expanded": expanded, "found": found}

    def _m_tree_open_file(self, params):
        path = params.get("path", "")
        if not path:
            raise _RpcError("missing_param: path")
        if not Path(path).is_file():
            raise _RpcError(f"io_error: not a file: {path}")
        tab_id = self._ide.tree_open_file(path)
        return {"tab_id": tab_id, "path": path}

    # ---- Editor text operations -------------------------------------------

    @staticmethod
    def _text_to_offset(lines: list, line_1: int, col_0: int) -> int:
        """Convert 1-indexed line, 0-indexed column to character offset."""
        line_1 = max(1, min(line_1, len(lines)))
        col_0 = max(0, min(col_0, len(lines[line_1 - 1])))
        return sum(len(lines[i]) + 1 for i in range(line_1 - 1)) + col_0

    @staticmethod
    def _offset_to_line_col(text: str, offset: int):
        before = text[:offset]
        line = before.count("\n") + 1
        col = offset - before.rfind("\n") - 1
        return line, col

    def _do_replace_range(self, params, replacement: str) -> dict:
        ide = self._ide
        tab_id = params.get("tab_id", "")
        if not tab_id:
            raise _RpcError("missing_param: tab_id")
        from_line = int(params.get("from_line", 1))
        from_col = int(params.get("from_col", 0))
        to_line = int(params.get("to_line", from_line))
        to_col = int(params.get("to_col", from_col))
        return ide.editor_replace_range(tab_id, from_line, from_col, to_line, to_col, replacement)

    def _m_editor_insert(self, params):
        tab_id = params.get("tab_id", "")
        line = int(params.get("line", 1))
        col = int(params.get("col", 0))
        text = params.get("text", "")
        if not tab_id:
            raise _RpcError("missing_param: tab_id")
        if text is None:
            raise _RpcError("missing_param: text")
        p = {"tab_id": tab_id, "from_line": line, "from_col": col,
             "to_line": line, "to_col": col}
        return self._do_replace_range(p, str(text))

    def _m_editor_delete_range(self, params):
        tab_id = params.get("tab_id", "")
        if not tab_id:
            raise _RpcError("missing_param: tab_id")
        result = self._do_replace_range(params, "")
        # Annotate with deleted_chars (chars_total vs pre-delete length isn't tracked here,
        # but the caller can compare from the pre-call get_file_content)
        return result

    def _m_editor_replace_range(self, params):
        tab_id = params.get("tab_id", "")
        if not tab_id:
            raise _RpcError("missing_param: tab_id")
        text = params.get("text", "")
        if text is None:
            raise _RpcError("missing_param: text")
        return self._do_replace_range(params, str(text))

    def _m_editor_get_range(self, params):
        ide = self._ide
        tab_id = params.get("tab_id", "")
        if not tab_id:
            raise _RpcError("missing_param: tab_id")
        t, state = self._resolve_tab(tab_id, "")
        text = state.get("source_text", "")
        lines = text.split("\n")
        from_line = int(params.get("from_line", 1))
        from_col = int(params.get("from_col", 0))
        to_line = int(params.get("to_line", len(lines)))
        to_col = int(params.get("to_col", len(lines[-1]) if lines else 0))
        from_off = self._text_to_offset(lines, from_line, from_col)
        to_off = self._text_to_offset(lines, to_line, to_col)
        if from_off > to_off:
            from_off, to_off = to_off, from_off
        excerpt = text[from_off:to_off]
        return {"tab_id": t["tab_id"], "text": excerpt, "chars": len(excerpt)}

    def _m_probe_clipboard(self, params):
        import time as _time
        source_tab_id = params.get("source_tab_id", "")
        dest_tab_id = params.get("dest_tab_id", "")
        if not source_tab_id:
            raise _RpcError("missing_param: source_tab_id")

        ide = self._ide
        t_src, state_src = self._resolve_tab(source_tab_id, "")
        src_view = state_src.get("view", "e")
        src_target = f"{t_src['tab_id']}.{'epa' if src_view == 'epa' else 'source'}"

        steps = []

        def _step(name, method, call_params):
            try:
                result = ide.ui_call(method, call_params)
                steps.append({"step": name, "ok": True, "method": f"ui.{method}",
                               "params": call_params, "result": result})
                return result
            except Exception as exc:
                steps.append({"step": name, "ok": False, "method": f"ui.{method}",
                               "params": call_params, "error": str(exc)})
                return None

        # 1. Activate the source tab
        _step("activate_source_tab", "setActiveTab",
              {"target": "editor.tabs", "index": t_src["index"]})

        # 2. Snapshot source editor widget before doing anything
        _step("snapshot_source_before", "snapshotWidget", {"target": src_target})

        # 3. Explicitly focus the source editor
        _step("focus_source_editor", "setFocus", {"target": src_target})

        # 4. select_all via focused action (same path as the menu shortcut)
        _step("select_all_via_focused", "performFocusedAction", {"action": "edit.select_all"})

        # 5. Copy via focused action (what the menu uses)
        _step("copy_via_focused", "performFocusedAction", {"action": "edit.copy"})

        # 6. Copy via explicit target (bypasses focus routing — compare result with step 5)
        _step("copy_via_target", "performAction",
              {"target": src_target, "action": "edit.copy"})

        # 7. Snapshot source after copy attempt
        _step("snapshot_source_after_copy", "snapshotWidget", {"target": src_target})

        steps.append({
            "step": "source_content_snapshot",
            "ok": True,
            "note": "logic-side source_text (may differ from UI widget if unsynchronised)",
            "length": len(state_src.get("source_text", "")),
            "preview": state_src.get("source_text", "")[:200],
        })

        dest_editor_target = None
        if dest_tab_id:
            t_dst, state_dst = self._resolve_tab(dest_tab_id, "")
            dst_view = state_dst.get("view", "e")
            dst_target = f"{t_dst['tab_id']}.{'epa' if dst_view == 'epa' else 'source'}"
            dest_editor_target = dst_target

            dst_before = state_dst.get("source_text", "")
            steps.append({
                "step": "dest_content_before_paste",
                "ok": True,
                "length": len(dst_before),
                "preview": dst_before[:200],
            })

            # 8. Snapshot dest editor before paste
            _step("snapshot_dest_before_paste", "snapshotWidget", {"target": dst_target})

            # 9. Activate dest tab
            _step("activate_dest_tab", "setActiveTab",
                  {"target": "editor.tabs", "index": t_dst["index"]})

            # 10. Explicitly focus dest editor
            _step("focus_dest_editor", "setFocus", {"target": dst_target})

            # 11. Paste via focused action
            _step("paste_via_focused", "performFocusedAction", {"action": "edit.paste"})

            # 12. Paste via explicit target
            _step("paste_via_target", "performAction",
                  {"target": dst_target, "action": "edit.paste"})

            # 13. Snapshot dest after paste
            _step("snapshot_dest_after_paste", "snapshotWidget", {"target": dst_target})

            # Brief yield so any pending text_changed events can propagate
            _time.sleep(0.3)

            dst_after = state_dst.get("source_text", "")
            steps.append({
                "step": "dest_content_after_paste",
                "ok": True,
                "note": "logic-side source_text — only updates if a text_changed event fired",
                "length": len(dst_after),
                "changed": dst_after != dst_before,
                "preview": dst_after[:200],
            })

        return {
            "steps": steps,
            "source_tab_id": t_src["tab_id"],
            "source_editor_target": src_target,
            "dest_tab_id": dest_tab_id or None,
            "dest_editor_target": dest_editor_target,
        }

    def _m_ui_call(self, params):
        method = params.get("method", "")
        if not method:
            raise _RpcError("missing_param: method")
        ui_params = params.get("params") or {}
        return self._ide.ui_call(method, ui_params)

    def _m_open_project(self, params):
        path = params.get("path", "")
        if not path:
            raise _RpcError("missing_param: path")
        if not Path(path).is_dir():
            raise _RpcError(f"io_error: not a directory: {path}")
        return self._ide.open_project(path)

    def _m_get_exceptions(self, params):
        limit = int(params.get("limit", 50))
        return self._ide.get_exceptions(limit)

    def _m_clear_exceptions(self, _params):
        return self._ide.clear_exceptions()

    def _m_get_ui_status(self, _params):
        return self._ide.get_ui_status()

    def _m_restart_ui(self, params):
        use_gdb = bool(params.get("use_gdb", False))
        return self._ide.restart_ui(use_gdb)

    def _m_reload_ui_document(self, _params):
        return self._ide.reload_ui_document()

    def _m_trigger_action(self, params):
        action = params.get("action", "")
        if not action:
            raise _RpcError("missing_param: action")
        target = params.get("target", "") or action
        payload = params.get("payload") or {}
        if not isinstance(payload, dict):
            raise _RpcError("missing_param: payload must be an object")
        payload = dict(payload)
        payload.setdefault("action", action)
        return self._ide.trigger_action(action, target, payload)

    def _m_get_event_log(self, params):
        limit = int(params.get("limit", 100))
        type_filter = str(params.get("type_filter", ""))
        return self._ide.get_event_log(limit, type_filter)

    def _m_clear_event_log(self, _params):
        return self._ide.clear_event_log()

    # ---- Ingress profiles -------------------------------------------------

    def _m_list_ingress_types(self, _params):
        types = self._ide.list_ingress_types()
        return [{"type_name": name, "fields": fields} for name, fields in sorted(types.items())]

    def _m_list_ingress_profiles(self, params):
        type_name = params.get("type_name", "")
        if not type_name:
            raise _RpcError("missing_param: type_name")
        return self._ide.list_ingress_profiles(type_name)

    def _m_get_ingress_profile(self, params):
        type_name = params.get("type_name", "")
        profile_name = params.get("profile_name", "")
        if not type_name:
            raise _RpcError("missing_param: type_name")
        if not profile_name:
            raise _RpcError("missing_param: profile_name")
        return self._ide.get_ingress_profile(type_name, profile_name)

    def _m_save_ingress_profile(self, params):
        type_name = params.get("type_name", "")
        profile_name = params.get("profile_name", "")
        fields = params.get("fields")
        if not type_name:
            raise _RpcError("missing_param: type_name")
        if not profile_name:
            raise _RpcError("missing_param: profile_name")
        if not isinstance(fields, dict):
            raise _RpcError("missing_param: fields must be a dict")
        # Normalise values to strings
        fields_str = {k: str(v) for k, v in fields.items()}
        return self._ide.save_ingress_profile(type_name, profile_name, fields_str)

    def _m_delete_ingress_profile(self, params):
        type_name = params.get("type_name", "")
        profile_name = params.get("profile_name", "")
        if not type_name:
            raise _RpcError("missing_param: type_name")
        if not profile_name:
            raise _RpcError("missing_param: profile_name")
        return self._ide.delete_ingress_profile(type_name, profile_name)

    # -----------------------------------------------------------------------
    # Helpers
    # -----------------------------------------------------------------------

    def _resolve_tab(self, tab_id: str, path: str):
        """Find a tab entry and its editor_state dict, or raise _RpcError."""
        ide = self._ide
        t = None
        if tab_id:
            t = next((x for x in ide.tab_list if x.get("tab_id") == tab_id), None)
        if t is None and path:
            t = next((x for x in ide.tab_list if x.get("path") == path), None)
        if t is None:
            raise _RpcError(f"no_such_tab: {tab_id or path!r}")
        state = ide.editor_state.get(t["tab_id"], {})
        return t, state


class _RpcError(Exception):
    pass


# ---------------------------------------------------------------------------
# IdeBindings — interface between the server and app.py's closures.
# Create one instance in main() and pass it to AiRpcServer.
# ---------------------------------------------------------------------------

class IdeBindings:
    """
    Thin wrapper that gives AiRpcServer access to app.py state and callbacks.

    All fields must be set before the server starts accepting connections,
    but they may be mutated in place as the IDE evolves (tab_list, etc. are
    the live objects from main()).
    """

    def __init__(self):
        # Live state dicts from main() — set these to the actual objects.
        self.tab_list: list = []
        self.editor_state: dict = {}
        self.app_state: dict = {}
        self.ai_state: dict = {}

        # Callbacks — set these to callables before start().
        self._language_for_path = None      # (path: str) -> str
        self._compile_tab = None            # (tab_id: str) -> dict
        self._open_file = None              # (path: str) -> tab_id str or None
        self._switch_view = None            # (tab_id: str, view: str) -> None
        self._set_editor_content = None     # (tab_id: str, content: str) -> None
        self._set_active_tab = None         # (tab_id: str) -> None
        self._close_tab = None              # (tab_id: str) -> None
        self._get_nav_tree = None           # () -> list
        self._set_node_expanded = None      # (node_id: str, expanded: bool) -> bool
        self._tree_open_file = None         # (path: str) -> tab_id str
        self._editor_replace_range = None   # (tab_id, fl, fc, tl, tc, text) -> dict
        self._ui_call = None                # (method: str, params: dict) -> any
        self._open_project = None           # (path: str) -> dict
        self._get_exceptions = None         # (limit: int) -> list
        self._clear_exceptions = None       # () -> dict
        self._get_ui_status = None          # () -> dict
        self._restart_ui = None             # (use_gdb: bool) -> dict
        self._reload_ui_document = None     # () -> dict
        self._trigger_action = None         # (action, target, payload) -> dict
        self._get_event_log = None          # (limit: int, type_filter: str) -> list
        self._clear_event_log = None        # () -> dict
        self._log_event = None              # (event_type: str, **details) -> None
        self._list_ingress_types = None     # () -> dict {type_name: [fields]}
        self._list_ingress_profiles = None  # (type_name: str) -> list[{name, path}]
        self._get_ingress_profile = None    # (type_name, profile_name) -> dict
        self._save_ingress_profile = None   # (type_name, profile_name, fields) -> dict
        self._delete_ingress_profile = None # (type_name, profile_name) -> dict

    # Delegate to callbacks so callers use clean attribute access.
    def language_for_path(self, path: str) -> str:
        return self._language_for_path(path) if self._language_for_path else "plain"

    def compile_tab(self, tab_id: str) -> dict:
        if not self._compile_tab:
            raise _RpcError("ui_unavailable: compile callback not set")
        return self._compile_tab(tab_id)

    def open_file(self, path: str):
        if not self._open_file:
            raise _RpcError("ui_unavailable: open_file callback not set")
        return self._open_file(path)

    def switch_view(self, tab_id: str, view: str):
        if not self._switch_view:
            raise _RpcError("ui_unavailable: switch_view callback not set")
        self._switch_view(tab_id, view)

    def set_editor_content(self, tab_id: str, content: str):
        if not self._set_editor_content:
            raise _RpcError("ui_unavailable: set_editor_content callback not set")
        self._set_editor_content(tab_id, content)

    def set_active_tab(self, tab_id: str):
        if not self._set_active_tab:
            raise _RpcError("ui_unavailable: set_active_tab callback not set")
        self._set_active_tab(tab_id)

    def close_tab(self, tab_id: str):
        if not self._close_tab:
            raise _RpcError("ui_unavailable: close_tab callback not set")
        self._close_tab(tab_id)

    def get_nav_tree(self) -> list:
        if not self._get_nav_tree:
            return []
        return self._get_nav_tree()

    def set_node_expanded(self, node_id: str, expanded: bool) -> bool:
        if not self._set_node_expanded:
            raise _RpcError("ui_unavailable: set_node_expanded callback not set")
        return self._set_node_expanded(node_id, expanded)

    def tree_open_file(self, path: str) -> str:
        if not self._tree_open_file:
            raise _RpcError("ui_unavailable: tree_open_file callback not set")
        return self._tree_open_file(path)

    def ui_call(self, method: str, params: dict):
        if not self._ui_call:
            raise _RpcError("ui_unavailable: ui_call callback not set")
        return self._ui_call(method, params)

    def open_project(self, path: str) -> dict:
        if not self._open_project:
            raise _RpcError("ui_unavailable: open_project callback not set")
        return self._open_project(path)

    def get_exceptions(self, limit: int = 50) -> list:
        if not self._get_exceptions:
            return []
        return self._get_exceptions(limit)

    def clear_exceptions(self) -> dict:
        if not self._clear_exceptions:
            return {"cleared": 0}
        return self._clear_exceptions()

    def get_ui_status(self) -> dict:
        if not self._get_ui_status:
            return {"connected": False, "managed_process": False, "process_alive": False,
                    "process_pid": None, "server_cmd": [], "recent_output": []}
        return self._get_ui_status()

    def restart_ui(self, use_gdb: bool = False) -> dict:
        if not self._restart_ui:
            raise _RpcError("ui_unavailable: restart_ui callback not set")
        return self._restart_ui(use_gdb)

    def reload_ui_document(self) -> dict:
        if not self._reload_ui_document:
            raise _RpcError("ui_unavailable: reload_ui_document callback not set")
        return self._reload_ui_document()

    def trigger_action(self, action: str, target: str, payload: dict) -> dict:
        if not self._trigger_action:
            raise _RpcError("ui_unavailable: trigger_action callback not set")
        return self._trigger_action(action, target, payload)

    def get_event_log(self, limit: int = 100, type_filter: str = "") -> list:
        if not self._get_event_log:
            return []
        return self._get_event_log(limit, type_filter)

    def clear_event_log(self) -> dict:
        if not self._clear_event_log:
            return {"cleared": 0}
        return self._clear_event_log()

    def log_event(self, event_type: str, **details):
        if self._log_event:
            self._log_event(event_type, **details)

    def list_ingress_types(self) -> dict:
        if not self._list_ingress_types:
            return {}
        return self._list_ingress_types()

    def list_ingress_profiles(self, type_name: str) -> list:
        if not self._list_ingress_profiles:
            raise _RpcError("ui_unavailable: ingress callbacks not set")
        return self._list_ingress_profiles(type_name)

    def get_ingress_profile(self, type_name: str, profile_name: str) -> dict:
        if not self._get_ingress_profile:
            raise _RpcError("ui_unavailable: ingress callbacks not set")
        return self._get_ingress_profile(type_name, profile_name)

    def save_ingress_profile(self, type_name: str, profile_name: str, fields: dict) -> dict:
        if not self._save_ingress_profile:
            raise _RpcError("ui_unavailable: ingress callbacks not set")
        return self._save_ingress_profile(type_name, profile_name, fields)

    def delete_ingress_profile(self, type_name: str, profile_name: str) -> dict:
        if not self._delete_ingress_profile:
            raise _RpcError("ui_unavailable: ingress callbacks not set")
        return self._delete_ingress_profile(type_name, profile_name)

    def editor_replace_range(self, tab_id: str, from_line: int, from_col: int,
                             to_line: int, to_col: int, text: str) -> dict:
        if not self._editor_replace_range:
            raise _RpcError("ui_unavailable: editor_replace_range callback not set")
        try:
            return self._editor_replace_range(tab_id, from_line, from_col, to_line, to_col, text)
        except KeyError as exc:
            raise _RpcError(str(exc))
