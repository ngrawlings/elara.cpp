import argparse
import concurrent.futures
import json
import os
import re
import select
import signal
import shlex
import shutil
import socket
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from pathlib import Path

from elara_ui.builder import UiDocumentBuilder
from elara_ui.rpc import ElaraUiRpcClient, ElaraUiRpcError, _BRpcCodec
from elara_ui.snapshot_dumper import UiSnapshotDumper
from elara_ui.repl_client import ElaraUiRepl

from ai_rpc import AiRpcServer, IdeBindings

# Elara Versioning — add versioning directory to path and import.
try:
    _ev_dir = Path(__file__).resolve().parent / "elara_versioning"
    if str(_ev_dir) not in sys.path:
        sys.path.insert(0, str(_ev_dir))
    from elara_versioning.evmanager_core import ProjectRepo, PROJECT_DIR_NAME as EV_PROJECT_DIR
    _EV_AVAILABLE = True
except Exception:
    _EV_AVAILABLE = False
    ProjectRepo = None
    EV_PROJECT_DIR = ".project"


from constants import INITIAL_E_TABS, AI_MODELS, ANTHROPIC_SYSTEM_PROMPT
from dap_client import PythonDapClient
from editor_tabs import (
    _editor_ids, _editor_language_for_path, _focus_editor_widget,
    _project_toolbar_items, _set_project_toolbar_enabled,
    _create_e_tab, _create_python_tab, _create_cpp_tab, _build_kernel_row_widgets,
    _PROJECT_TOOLBAR_ITEMS,
)
from ui_document import build_document
from dialogs import (
    build_open_file_dialog, build_new_project_wizard, build_open_project_dialog,
    build_new_file_dialog, build_worker_fault_dialog, build_error_dialog,
    build_project_add_menu_dialog, build_cpp_technology_wizard, build_ingress_profile_editor,
    start_background_worker,
    _to_class_name, _to_symbol_name, _e_template_items, _e_template_summary,
    _e_file_content, _file_content, _folder_items,
    _cpp_header_content, _cpp_source_content,
    E_FILE_TEMPLATES,
)
from help_window import build_help_window, load_doc as _help_load_doc
from e3d_runtime import load_runtime_preview
from level_path_runtime import (
    build_level_path_preview,
    hit_test_item as level_path_hit_test_item,
    load_level_path_document,
    move_item as level_path_move_item,
    place_primitive as level_path_place_primitive,
    rotate_view as level_path_rotate_view,
    screen_to_world as level_path_screen_to_world,
    view_cube_contains as level_path_view_cube_contains,
    view_cube_hit_test as level_path_view_cube_hit_test,
)

def _ide_state_path() -> Path:
    return Path.home() / ".config" / "epa-ide" / "state.json"


_ide_state_lock = threading.Lock()
_ide_state_cache: dict | None = None
_ide_state_write_timer: threading.Timer | None = None
_IDE_STATE_FLUSH_DELAY = 0.3


def _flush_ide_state_to_disk():
    global _ide_state_write_timer
    with _ide_state_lock:
        _ide_state_write_timer = None
        snapshot = dict(_ide_state_cache) if _ide_state_cache is not None else None
    if snapshot is None:
        return
    p = _ide_state_path()
    try:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(snapshot, indent=2), encoding="utf-8")
    except Exception:
        pass


def _load_ide_state() -> dict:
    global _ide_state_cache
    with _ide_state_lock:
        if _ide_state_cache is not None:
            return dict(_ide_state_cache)
        p = _ide_state_path()
        try:
            _ide_state_cache = json.loads(p.read_text(encoding="utf-8"))
        except Exception:
            _ide_state_cache = {}
        return dict(_ide_state_cache)


def _save_ide_state(updates: dict):
    global _ide_state_cache, _ide_state_write_timer
    with _ide_state_lock:
        state = dict(_ide_state_cache) if _ide_state_cache is not None else {}
        for key, value in updates.items():
            if isinstance(value, dict) and isinstance(state.get(key), dict):
                merged = dict(state[key])
                merged.update(value)
                state[key] = merged
            else:
                state[key] = value
        _ide_state_cache = state
        if _ide_state_write_timer is not None:
            _ide_state_write_timer.cancel()
        _ide_state_write_timer = threading.Timer(_IDE_STATE_FLUSH_DELAY, _flush_ide_state_to_disk)
        _ide_state_write_timer.daemon = True
        _ide_state_write_timer.start()


def _layout_value(value, fallback: int, minimum: int = 120) -> int:
    try:
        parsed = int(round(float(value)))
    except Exception:
        return fallback
    return parsed if parsed >= minimum else fallback


def _window_value(value, fallback: int, minimum: int = 640) -> int:
    try:
        parsed = int(round(float(value)))
    except Exception:
        return fallback
    return parsed if parsed >= minimum else fallback


def _current_layout_state() -> dict:
    return _load_ide_state()


def _default_codex_cli_path() -> str:
    candidates = sorted(
        Path.home().glob(".vscode/extensions/openai.chatgpt-*/bin/*/codex"),
        key=lambda p: str(p),
        reverse=True,
    )
    for candidate in candidates:
        if candidate.is_file() and os.access(str(candidate), os.X_OK):
            return str(candidate)
    return "codex"


def _use_system_window_header(ide_state: dict | None = None) -> bool:
    state = ide_state if isinstance(ide_state, dict) else _load_ide_state()
    ui_state = state.get("ui", {}) if isinstance(state, dict) else {}
    if isinstance(ui_state, dict):
        return bool(ui_state.get("use_system_window_header", False))
    return False


def _right_panel_visible(ide_state: dict | None = None) -> bool:
    state = ide_state if isinstance(ide_state, dict) else _load_ide_state()
    ui_state = state.get("ui", {}) if isinstance(state, dict) else {}
    if isinstance(ui_state, dict) and "right_panel_visible" in ui_state:
        return bool(ui_state.get("right_panel_visible"))
    return True


def _bottom_panel_visible(ide_state: dict | None = None) -> bool:
    state = ide_state if isinstance(ide_state, dict) else _load_ide_state()
    ui_state = state.get("ui", {}) if isinstance(state, dict) else {}
    if isinstance(ui_state, dict) and "bottom_panel_visible" in ui_state:
        return bool(ui_state.get("bottom_panel_visible"))
    return False


def _persist_runtime_layout_state(client):
    # Window size — saved independently so a layout query failure does not lose size.
    # When maximized we only update the maximized flag, not the size, so the pre-maximized
    # size is preserved for the next non-maximized restore.
    try:
        window_state = client.get_window_state() or {}
        win_w = _window_value(window_state.get("width"), 0, minimum=1)
        win_h = _window_value(window_state.get("height"), 0, minimum=1)
        maximized = bool(window_state.get("maximized", False))
        if maximized:
            _save_ide_state({"window": {"maximized": True}})
        elif win_w > 0 and win_h > 0:
            _save_ide_state({"window": {"width": win_w, "height": win_h, "maximized": False}})
        else:
            _save_ide_state({"window": {"maximized": False}})
    except Exception:
        pass

    # Panel layout — column/row sizes saved independently.
    # Use 'size' (not 'computed_size'): computed_size is 0 until the first draw()
    # call, but size is set immediately from the document and updated by drag-resize.
    # Saving computed_size=0 would overwrite the correct persisted values with fallbacks.
    try:
        shell = client.get_grid_layout_state("app.shell") or {}
        columns = shell.get("columns") or []
        nav_width = None
        ai_width = None
        right_panel_visible = _right_panel_visible()
        if len(columns) > 1:
            raw = float(columns[1].get("size") or 0)
            if raw >= 120:
                nav_width = int(round(raw))
        if len(columns) > 3 and right_panel_visible:
            raw = float(columns[3].get("size") or 0)
            if raw >= 120:
                ai_width = int(round(raw))
        layout = {}
        if nav_width is not None:
            layout["nav_width"] = nav_width
        if ai_width is not None:
            layout["ai_width"] = ai_width
        if layout:
            _save_ide_state({"layout": layout})
    except Exception:
        pass

    try:
        center = client.get_grid_layout_state("app.center") or {}
        center_rows = center.get("rows") or []
        bottom_panel_visible = _bottom_panel_visible()
        if len(center_rows) > 1 and bottom_panel_visible:
            raw = float(center_rows[1].get("size") or 0)
            if raw >= 120:
                _save_ide_state({"layout": {"bottom_height": int(round(raw))}})
    except Exception:
        pass


def _breadcrumb_for(path: str) -> str:
    home = str(Path.home())
    p = Path(path)
    try:
        rel = p.relative_to(home)
        parts = ["Home"] + list(rel.parts)
    except ValueError:
        parts = list(p.parts)
    return " › ".join(parts) if parts else path


def _open_file_items(path: str) -> list:
    """Return {id, label} dicts for entries (dirs + files) under path."""
    entries = []
    parent = str(Path(path).parent)
    if parent != path:
        entries.append({"id": parent, "label": ".."})
    try:
        names = sorted(os.listdir(path), key=str.lower)
        for name in names:
            if name.startswith("."):
                continue
            full = os.path.join(path, name)
            if os.path.isdir(full):
                entries.append({"id": full, "label": name + "/"})
            else:
                entries.append({"id": full, "label": name})
    except OSError:
        pass
    return entries



def main():
    parser = argparse.ArgumentParser(description="Load the generated Elara UI document into a running RPC head")
    parser.add_argument("--host", default="127.0.0.1", help="RPC server host")
    parser.add_argument("--port", default=18777, type=int, help="RPC server port")
    parser.add_argument("--snapshot", action="store_true", help="Fetch a root snapshot after loading")
    parser.add_argument("--dump-snapshot", action="store_true", help="Dump the full runtime widget snapshot to JSON after loading")
    parser.add_argument("--snapshot-out", default="elara-ui-snapshot.json", help="Path used by --dump-snapshot and the integrated REPL snapshot command")
    parser.add_argument("--repl", action="store_true", help="Start the integrated Elara UI REPL after loading the document")
    parser.add_argument("--output", help="Write the generated document JSON to this path")
    parser.add_argument("--once", action="store_true", help="Load once and exit immediately")
    parser.add_argument("--no-events", action="store_true", help="Do not subscribe to default UI events")
    parser.add_argument("--no-worker", action="store_true", help="Do not start the optional multi-core worker template")
    parser.add_argument("--event-log", default=None, help="Write all received UI events to this JSONL file")
    parser.add_argument("--ai-rpc-port", default=18779, type=int, help="AI logic-side RPC port (0 to disable)")
    parser.add_argument("--json-rpc", action="store_true", help="Use JSON RPC codec instead of the default binary BRPC codec")
    args = parser.parse_args()

    client_ref = {}
    wizard_state = {}            # live checkbox state for the new-project wizard
    nav_state = {}               # current browse path in the wizard file picker
    open_project_nav_state = {}  # current browse path in the open-project dialog
    open_file_nav_state = {}     # current browse path in the open-file dialog
    app_state = {}               # persistent project state set after universal creation
    new_file_state = {}          # live state for the new-file dialog
    new_file_nav_state = {}      # current browse path in the new-file dialog
    cpp_tech_state = {}          # live state for the add-C++ technology wizard
    editor_state = {}
    ingress_editor_state = {}    # live state for the ingress profile editor window
    app_state["active_editor_tab"] = INITIAL_E_TABS[0][0] if INITIAL_E_TABS else ""
    app_state["theme"] = "dark"
    app_state["nav_view"] = "files"
    app_state["nav_3d_tab_present"] = False
    app_state["debug_vm_started"] = False
    initial_state = _current_layout_state()
    initial_layout = initial_state.get("layout", {}) if isinstance(initial_state, dict) else {}
    app_state["right_panel_visible"] = _right_panel_visible(initial_state)
    app_state["right_panel_width"] = _layout_value(initial_layout.get("ai_width"), 320)
    app_state["bottom_panel_visible"] = _bottom_panel_visible(initial_state)
    app_state["bottom_panel_height"] = _layout_value(initial_layout.get("bottom_height"), 220)
    tab_list = []                # [{"tab_id", "path", "index", "preview"}]
    tab_click_state = {}         # double-click detection: {"path", "time"}
    initial_ai_config = initial_state.get("ai", {}) if isinstance(initial_state, dict) else {}
    if not isinstance(initial_ai_config, dict):
        initial_ai_config = {}
    default_codex_cli = _default_codex_cli_path()
    default_codex_args = "exec --skip-git-repo-check --cd {cwd} --model {model} -"
    saved_codex_args = str(initial_ai_config.get("codex_args", default_codex_args) or default_codex_args)
    if saved_codex_args == "exec --skip-git-repo-check --cd {cwd} --model {model} {prompt}":
        saved_codex_args = default_codex_args
    ai_state = {
        "messages":     [],      # [{"role": "user"|"assistant", "content": str}]
        "model":        str(initial_ai_config.get("model", "codex:gpt-5") or "codex:gpt-5"),
        "ctx_file":     True,
        "ctx_project":  False,
        "ctx_selection": False,
        "input_text":   "",
        "generating":   False,
        "cancel_event": None,
        "process":      None,
        "codex_cli":    str(initial_ai_config.get("codex_cli", default_codex_cli) or default_codex_cli),
        "codex_args":   saved_codex_args,
    }
    terminal_state = {
        "cwd": "",
        "input": "",
        "output": "Terminal ready. Open a project to set the working directory.",
    }
    console_state = {
        "output": "EPA-IDE Console — type 'help' for available commands.\n",
        "input": "",
        "history": [],
        "history_pos": -1,
    }
    app_state["bottom_view"] = "build"
    cpp_gdb_state = {
        "proc": None,
        "token": 0,
        "running": False,
        "project_root": "",
        "binary": "",
        "args": [],
        "status": "No GDB session attached.",
        "read_buffer": "",
        "breakpoints_by_path": {},
    }
    python_dbg_state = {
        "started": False,
        "status": "No Python debug session attached.",
        "proc": None,
        "dap": None,
        "port": None,
        "thread_id": None,
        "stopped": False,
    }
    host_debug_bridge = {
        "server": None,
        "thread": None,
        "port": None,
        "client_connected": False,
        "client_socket": None,
        "client_send_lock": threading.Lock(),
        "client_pid": None,
        "external_logic_connected": False,
        "cpp_ext_logic_port": 0,
        "ext_logic_proxied": False,
        "last_host_pong": 0.0,
        "last_ext_pong": 0.0,
        "ping_thread_started": False,
    }
    external_logic_bridge = {
        "server": None,
        "thread": None,
        "port": None,
        "client_sockets": set(),
        "client_lock": threading.Lock(),
    }

    # AI RPC bindings — callbacks are set later, once the inner closures exist.
    ide_bindings = IdeBindings()
    ide_bindings.tab_list = tab_list
    ide_bindings.editor_state = editor_state
    ide_bindings.app_state = app_state
    ide_bindings.ai_state = ai_state
    ide_bindings._language_for_path = _editor_language_for_path
    ai_rpc_server: AiRpcServer | None = None

    # --- Event log -----------------------------------------------------------
    # Ring buffer of all significant IDE events. Each entry is a plain dict
    # with at minimum {"ts": float, "type": str}. Written to a per-session
    # JSONL file in artifacts/ so it survives process crashes.
    _event_log: list = []
    _event_log_lock = threading.Lock()
    _event_log_max = 2000
    _event_log_fh: list = [None]        # mutable ref so nested closures can reassign
    _ai_rpc_server_ref: list = [None]   # same pattern for ai_rpc_server

    def _push_event(event_type: str, **details):
        entry = {"ts": time.time(), "type": event_type}
        entry.update(details)
        with _event_log_lock:
            _event_log.append(entry)
            if len(_event_log) > _event_log_max:
                del _event_log[: len(_event_log) - _event_log_max]
        fh = _event_log_fh[0]
        if fh is not None:
            try:
                fh.write(json.dumps(entry, ensure_ascii=False, default=str) + "\n")
                fh.flush()
            except Exception:
                pass

    def _event_log_recent(n: int = 30) -> list:
        with _event_log_lock:
            return list(_event_log[-n:])

    # Trim large string values before storing in event log (keeps log readable).
    def _trim_for_log(params, max_str: int = 300):
        if not isinstance(params, dict):
            return params
        out = {}
        for k, v in params.items():
            if isinstance(v, str) and len(v) > max_str:
                out[k] = f"{v[:max_str]}…[{len(v)} chars]"
            else:
                out[k] = v
        return out

    # --- Exception log -------------------------------------------------------
    _exception_log: list = []
    _exception_log_lock = threading.Lock()
    _exception_log_max = 200

    def _push_exception(exc, context: str = "unknown"):
        import traceback as _tb
        tb_text = ("".join(_tb.format_exception(type(exc), exc, exc.__traceback__))
                   if isinstance(exc, BaseException) else "")
        recent = _event_log_recent(30)
        entry = {
            "ts": time.time(),
            "context": context,
            "type": type(exc).__name__ if isinstance(exc, BaseException) else "str",
            "message": str(exc),
            "traceback": tb_text,
            "recent_events": recent,
        }
        with _exception_log_lock:
            _exception_log.append(entry)
            if len(_exception_log) > _exception_log_max:
                del _exception_log[: len(_exception_log) - _exception_log_max]
        _push_event("exception", context=context,
                    exc_type=entry["type"], message=entry["message"],
                    traceback=tb_text)

    _orig_excepthook = sys.excepthook

    def _excepthook(exc_type, exc_val, exc_tb):
        _push_exception(exc_val, "uncaught")
        _orig_excepthook(exc_type, exc_val, exc_tb)

    sys.excepthook = _excepthook
    threading.excepthook = lambda args: _push_exception(
        args.exc_value if args.exc_value else RuntimeError(repr(args)),
        f"thread:{getattr(args.thread, 'name', '?')}",
    )

    # --- UI server subprocess tracking ---------------------------------------
    _ui_server: dict = {"proc": None, "cmd": [], "output_lines": []}

    def _resolve_ui_server_cmd():
        workspace = Path(__file__).resolve().parent.parent
        candidates = [
            workspace / "libElaraUI" / "build" / "bin" / "elaraui-server",
            workspace / "build" / "bin" / "elaraui-server",
        ]
        default = Path("/usr/local/bin/elaraui-server")
        import os as _os
        binary = str(default)
        for candidate in candidates:
            if candidate.is_file() and _os.access(str(candidate), _os.X_OK):
                binary = str(candidate)
                break
        return [binary, "--port", str(args.port), "--persistent"]

    # --- epavm subprocess + client -------------------------------------------
    _epa_dbg: dict = {"proc": None, "client": None, "output_lines": [], "port": None, "build_output": ""}

    # ── Shared context for subsystem modules ─────────────────────────────────
    ctx: dict = {
        "client_ref":           client_ref,
        "app_state":            app_state,
        "cpp_gdb_state":        cpp_gdb_state,
        "python_dbg_state":     python_dbg_state,
        "tab_list":             tab_list,
        "editor_state":         editor_state,
        "terminal_state":       terminal_state,
        "console_state":        console_state,
        "ai_state":             ai_state,
        "ingress_editor_state": ingress_editor_state,
        "host_debug_bridge":    host_debug_bridge,
        "wizard_state":         wizard_state,
        "nav_state":            nav_state,
        "tab_click_state":      tab_click_state,
        "new_file_state":       new_file_state,
        "new_file_nav_state":   new_file_nav_state,
        "open_project_nav_state": open_project_nav_state,
        "open_file_nav_state":  open_file_nav_state,
        "_epa_dbg":             _epa_dbg,
        "args":                 args,
        "ide_bindings":         ide_bindings,
    }

    import cpp_debug as _cpp_debug_mod
    import python_debug as _python_debug_mod
    _cpp_debug_mod.setup(ctx)
    _python_debug_mod.setup(ctx)

    # Unpack extracted functions into local scope for use throughout main()
    _set_cpp_thread_items        = ctx["_set_cpp_thread_items"]
    _set_cpp_status_text         = ctx["_set_cpp_status_text"]
    _set_cpp_vm_status           = ctx["_set_cpp_vm_status"]
    _set_cpp_vm_buttons          = ctx["_set_cpp_vm_buttons"]
    _set_cpp_tree_nodes          = ctx["_set_cpp_tree_nodes"]
    _set_cpp_registers_text      = ctx["_set_cpp_registers_text"]
    _set_cpp_memory_text         = ctx["_set_cpp_memory_text"]
    _cpp_gdb_stop_session        = ctx["_cpp_gdb_stop_session"]
    _cpp_gdb_read_until_prompt   = ctx["_cpp_gdb_read_until_prompt"]
    _cpp_gdb_send                = ctx["_cpp_gdb_send"]
    _cpp_gdb_threads_from_lines  = ctx["_cpp_gdb_threads_from_lines"]
    _cpp_gdb_status_from_lines   = ctx["_cpp_gdb_status_from_lines"]
    _cpp_gdb_refresh_ui          = ctx["_cpp_gdb_refresh_ui"]
    _cpp_gdb_show_thread         = ctx["_cpp_gdb_show_thread"]
    _ensure_cpp_gdb_session      = ctx["_ensure_cpp_gdb_session"]
    _cpp_gdb_log                 = ctx["_cpp_gdb_log"]
    _cpp_gdb_execute             = ctx["_cpp_gdb_execute"]
    _cpp_gdb_handle_action       = ctx["_cpp_gdb_handle_action"]
    _cpp_gdb_set_breakpoint      = ctx["_cpp_gdb_set_breakpoint"]
    _set_python_thread_items     = ctx["_set_python_thread_items"]
    _set_python_status_text      = ctx["_set_python_status_text"]
    _set_python_vm_status        = ctx["_set_python_vm_status"]
    _set_python_vm_buttons       = ctx["_set_python_vm_buttons"]
    _set_python_frame_items      = ctx["_set_python_frame_items"]
    _set_python_locals_text      = ctx["_set_python_locals_text"]
    _set_python_registers_text   = ctx["_set_python_registers_text"]
    _set_python_memory_text      = ctx["_set_python_memory_text"]
    _python_dbg_refresh_ui       = ctx["_python_dbg_refresh_ui"]
    _python_dbg_refresh_threads  = ctx["_python_dbg_refresh_threads"]
    _python_dbg_show_thread      = ctx["_python_dbg_show_thread"]
    _python_dap_on_event         = ctx["_python_dap_on_event"]
    _python_dbg_stop             = ctx["_python_dbg_stop"]
    _python_dbg_launch           = ctx["_python_dbg_launch"]
    _python_dbg_handle_action    = ctx["_python_dbg_handle_action"]

    def _epa_dbg_binary():
        workspace = Path(__file__).resolve().parent.parent
        return workspace / "epavm" / "build" / "epavm"

    def _epa_dbg_port() -> int | None:
        port = _epa_dbg.get("port")
        return int(port) if port else None

    def _allocate_epa_dbg_port() -> int:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("127.0.0.1", 0))
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            return int(s.getsockname()[1])

    def _epa_dbg_client() -> "EpaDbgClient | None":
        return _epa_dbg.get("client")

    def _epa_dbg_running() -> bool:
        proc = _epa_dbg.get("proc")
        return bool(proc and proc.poll() is None and _epa_dbg.get("client"))

    def _epa_dbg_set_vm_status(state: str, detail: str = ""):
        """Update the visible VM status indicator in the Debug panel."""
        ui_c = client_ref.get("client")
        if not ui_c:
            return

        labels = {
            "idle":     ("●  VM idle", "#777777"),
            "starting": ("●  VM starting", "#c7922b"),
            "running":  ("●  VM running", "#3ea35f"),
            "stopping": ("●  VM stopping", "#c7922b"),
            "error":    ("●  VM error", "#c85151"),
        }
        text, color = labels.get(state, (f"●  VM {state}", "#777777"))
        if detail:
            text = f"{text}: {detail}"
        try:
            ui_c.set_text("nav.debug.vm_status", text)
        except Exception:
            pass

    def _epa_dbg_set_vm_button(running: bool):
        """Update the Start/Reset button label to reflect current VM state."""
        ui_c = client_ref.get("client")
        if not ui_c:
            return
        label = "⟳  Reset" if running else "▶  Start"
        try:
            ui_c.set_text("nav.debug.vm_reset", label)
            ui_c.set_enabled("nav.debug.vm_stop", running)
        except Exception:
            pass
        _epa_dbg_set_vm_status("running" if running else "idle")

    def _epa_dbg_log(line: str):
        """Append a line to the epavm output buffer and push to the build output panel."""
        buf = _epa_dbg.get("build_output", "") + line + "\n"
        buf = buf[-32000:]  # cap at ~32k chars
        _epa_dbg["build_output"] = buf
        try:
            ui_c = client_ref.get("client")
            if ui_c:
                app_state["bottom_build_output"] = buf
                _set_build_output(ui_c, buf)
        except Exception:
            pass

    def _epa_dbg_show_error(title: str, message: str, artifact_lines: list | None = None):
        """Write an error artifact and show an error dialog in the UI."""
        artifacts_dir = Path(__file__).resolve().parent / "artifacts"
        artifacts_dir.mkdir(exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        artifact_path = artifacts_dir / f"epavm-error-{stamp}.txt"
        try:
            artifact_path.write_text(
                f"{title}\n{'='*60}\n{message}\n"
                + ("\n--- process output ---\n" + "\n".join(artifact_lines) if artifact_lines else ""),
                encoding="utf-8",
            )
        except Exception:
            pass
        ui_c = client_ref.get("client")
        _epa_dbg_set_vm_status("error", title.replace("epavm: ", "").replace("epa-dbg: ", ""))
        if ui_c:
            try:
                ui_c.open_window(
                    "epavm-error",
                    title,
                    520, 220,
                    build_error_dialog(title, f"{message}\n\nDetails written to:\n{artifact_path}"),
                )
            except Exception:
                pass

    def _epa_dbg_launch():
        """Start epavm if not already running, connect client."""
        from epavm_client import EpaDbgClient

        _epa_dbg_set_vm_status("starting")
        proc = _epa_dbg.get("proc")
        if proc and proc.poll() is None:
            if _epa_dbg.get("client") and _epa_dbg["client"].connected:
                _epa_dbg_set_vm_button(True)
                return
            # Process alive but client gone — reconnect.
            port = _epa_dbg_port()
            if not port:
                _epa_dbg_show_error(
                    "epavm: reconnect failed",
                    "epavm is running but no port was recorded for this session.",
                    list(_epa_dbg.get("output_lines", [])),
                )
                return
            try:
                c = EpaDbgClient("127.0.0.1", port)
                c.connect_retry(timeout=5.0)
                _epa_dbg["client"] = c
                _epa_dbg_set_vm_button(True)
            except Exception as exc:
                _epa_dbg_show_error(
                    "epavm: reconnect failed",
                    f"Could not reconnect to epavm on port {port}.\n{exc}",
                    list(_epa_dbg.get("output_lines", [])),
                )
            return

        binary = _epa_dbg_binary()
        if not binary.is_file():
            _epa_dbg_set_vm_button(False)
            _epa_dbg_show_error(
                "epavm: binary not found",
                f"Binary not found at:\n{binary}\n\nRun: make in the epavm directory.",
            )
            return

        out_lines = _epa_dbg.setdefault("output_lines", [])
        out_lines.clear()
        port = _allocate_epa_dbg_port()
        _epa_dbg["port"] = port
        # A fresh debugger process starts empty, so invalidate any prior load/kernel selection state.
        app_state.pop("debug_kernel_loaded", None)
        app_state.pop("debug_active_kernel", None)

        def _reader(stream, tag):
            for raw in stream:
                line = raw.decode(errors="replace").rstrip()
                out_lines.append(f"[{tag}] {line}")
                if len(out_lines) > 500:
                    del out_lines[: len(out_lines) - 500]
                _epa_dbg_log(f"[{tag}] {line}")
                if tag == "stderr":
                    fault_detail = _format_fault_marker_detail(line)
                    if fault_detail:
                        _epa_dbg_log(fault_detail)

        try:
            new_proc = subprocess.Popen(
                [str(binary), "--debug", str(port), "127.0.0.1"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                start_new_session=True,
            )
        except OSError as exc:
            _epa_dbg_set_vm_button(False)
            _epa_dbg_show_error(
                "epavm: failed to start",
                f"Could not execute:\n{binary}\n\n{exc}",
            )
            return

        threading.Thread(target=_reader, args=(new_proc.stdout, "stdout"), daemon=True).start()
        threading.Thread(target=_reader, args=(new_proc.stderr, "stderr"), daemon=True).start()
        _epa_dbg["proc"] = new_proc
        _epa_dbg_log(f"[epavm] started debug service on port {port}")

        # Give process a moment then check it hasn't immediately exited
        time.sleep(0.3)
        exit_code = new_proc.poll()
        if exit_code is not None:
            time.sleep(0.5)   # let readers drain
            _epa_dbg_show_error(
                "epavm: process exited immediately",
                f"epavm exited with code {exit_code} before accepting connections.",
                list(out_lines),
            )
            _epa_dbg["proc"] = None
            _epa_dbg["port"] = None
            return

        try:
            c = EpaDbgClient("127.0.0.1", port)
            c.connect_retry(timeout=8.0)
            _epa_dbg["client"] = c
            _epa_dbg_set_vm_button(True)
            _refresh_status_panel(client_ref.get("client"))
        except Exception as exc:
            exit_code = new_proc.poll()
            time.sleep(0.3)
            _epa_dbg_show_error(
                "epavm: connection failed",
                f"Process started on port {port} (exit={exit_code}) but TCP connect failed.\n{exc}",
                list(out_lines),
            )
            _epa_dbg["proc"] = None
            _epa_dbg["port"] = None
            _refresh_status_panel(client_ref.get("client"))

    def _epa_dbg_terminate_process(proc) -> None:
        if not proc or proc.poll() is not None:
            return
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except Exception:
            try:
                proc.terminate()
            except Exception:
                pass
        try:
            proc.wait(timeout=4)
            return
        except subprocess.TimeoutExpired:
            pass
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass
        try:
            proc.wait(timeout=4)
        except Exception:
            pass

    def _epa_dbg_stop():
        """Terminate epavm process and close client."""
        _epa_dbg_set_vm_status("stopping")
        c = _epa_dbg.get("client")
        if c:
            try:
                c.close()
            except Exception:
                pass
            _epa_dbg["client"] = None

        proc = _epa_dbg.get("proc")
        if proc and proc.poll() is None:
            _epa_dbg_terminate_process(proc)
        _epa_dbg["proc"] = None
        _epa_dbg["port"] = None
        _host_debug_bridge_stop()
        app_state.pop("debug_session_id", None)
        app_state.pop("debug_session_path", None)
        app_state.pop("debug_kernel_loaded", None)
        app_state.pop("debug_active_kernel", None)
        app_state["host_interconnect_ingress_count"] = 0
        app_state["host_interconnect_egress_count"] = 0
        app_state.pop("host_interconnect_session_id", None)
        _epa_dbg_set_vm_button(False)
        _refresh_status_panel(client_ref.get("client"))

    def _epa_dbg_reset(kernel_id: int = 0) -> dict:
        """Ensure epavm is running and reset the kernel slot."""
        _epa_dbg_launch()
        c = _epa_dbg_client()
        if not c:
            return {"ok": False, "error": "epavm not available"}
        try:
            result = c.reset(kernel_id)
            return {"ok": True, "result": result}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}

    def _epa_dbg_load_bundle(bundle_path: str, kernel_id: int = 0) -> dict:
        """Load a compiled .epabin bundle into the debug kernel."""
        _epa_dbg_launch()
        c = _epa_dbg_client()
        if not c:
            return {"ok": False, "error": "epavm not available"}
        try:
            load_result = c.load_bundle(kernel_id, bundle_path)
            return {"ok": True, "load": load_result}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}

    def _epa_dbg_load_asm(asm_text: str, kernel_id: int = 0) -> dict:
        """Load EPA assembly text directly into the debug kernel."""
        _epa_dbg_launch()
        c = _epa_dbg_client()
        if not c:
            return {"ok": False, "error": "epavm not available"}
        try:
            reset_result = c.reset(kernel_id)
            load_result = c.load_asm(kernel_id, asm_text)
            return {"ok": True, "reset": reset_result, "load": load_result}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}

    def _compiler_root():
        return Path(__file__).resolve().parent.parent / "libElaraParallelAssembly" / "e"

    def _compiler_binary():
        return _compiler_root() / ".." / "build" / "e" / "e2epa"

    def _semantic_binary():
        return _compiler_root() / ".." / "build" / "e" / "ec"

    def _bundle_binary():
        return _compiler_root() / ".." / "build" / "e" / "e2epabin"

    def _project_builder_root():
        return Path(__file__).resolve().parent.parent / "ElaraProjectBuilder"

    def _project_builder_binary():
        return _project_builder_root() / "build" / "elara-project-builder"

    def _ensure_project_builder():
        builder = _project_builder_binary()
        if builder.is_file():
            return builder

        subprocess.run(
            ["make", "-C", str(_project_builder_root()), "-j2"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return builder

    def _ensure_e2epa():
        compiler = _compiler_binary()
        if compiler.is_file():
            return compiler

        subprocess.run(
            ["make", "-C", str(_compiler_root()), "-j2"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return compiler

    def _ensure_ec():
        semantic = _semantic_binary()
        if semantic.is_file():
            return semantic
        _ensure_e2epa()
        return semantic

    def _ensure_e2epabin():
        bundle = _bundle_binary()
        if bundle.is_file():
            return bundle
        subprocess.run(
            ["make", "-C", str(_compiler_root()), "-j2"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return bundle

    def _project_meta(project_root: Path) -> dict:
        meta_path = project_root / ".elaraproject" / "project.json"
        try:
            return json.loads(meta_path.read_text(encoding="utf-8"))
        except Exception:
            return {}

    def _allocate_localhost_port() -> int:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("127.0.0.1", 0))
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            return int(s.getsockname()[1])

    def _host_debug_bridge_stop():
        server = host_debug_bridge.get("server")
        if server:
            try:
                server.shutdown()
            except Exception:
                pass
            try:
                server.server_close()
            except Exception:
                pass
        thread = host_debug_bridge.get("thread")
        if thread and thread.is_alive():
            thread.join(timeout=1.0)
        host_debug_bridge["server"] = None
        host_debug_bridge["thread"] = None
        host_debug_bridge["port"] = None
        host_debug_bridge["client_connected"] = False
        host_debug_bridge["client_socket"] = None
        host_debug_bridge["client_pid"] = None
        host_debug_bridge["last_host_pong"] = 0.0
        host_debug_bridge["last_ext_pong"] = 0.0

    def _external_logic_bridge_stop():
        server = external_logic_bridge.get("server")
        if server:
            try:
                server.shutdown()
            except Exception:
                pass
            try:
                server.server_close()
            except Exception:
                pass
        with external_logic_bridge["client_lock"]:
            sockets_to_close = list(external_logic_bridge.get("client_sockets", set()))
            external_logic_bridge["client_sockets"].clear()
        for sock in sockets_to_close:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except Exception:
                pass
            try:
                sock.close()
            except Exception:
                pass
        thread = external_logic_bridge.get("thread")
        if thread and thread.is_alive():
            thread.join(timeout=1.0)
        external_logic_bridge["server"] = None
        external_logic_bridge["thread"] = None
        external_logic_bridge["port"] = None
        host_debug_bridge["external_logic_connected"] = False
        host_debug_bridge["ext_logic_proxied"] = False
        host_debug_bridge["last_ext_pong"] = 0.0

    def _host_debug_bridge_send_json(payload: dict) -> bool:
        sock = host_debug_bridge.get("client_socket")
        if not sock:
            return False
        try:
            line = (json.dumps(payload) + "\n").encode("utf-8")
            with host_debug_bridge["client_send_lock"]:
                sock.sendall(line)
            return True
        except Exception:
            return False

    def _host_debug_bridge_ping_once() -> bool:
        if not host_debug_bridge.get("client_connected"):
            return False
        payload = {
            "kind": "ping",
            "session_id": str(app_state.get("debug_session_id", "") or ""),
            "id": f"host-ping-{int(time.time() * 1000)}",
        }
        return _host_debug_bridge_send_json(payload)

    def _status_ping_loop():
        while True:
            try:
                _host_debug_bridge_ping_once()
                if host_debug_bridge.get("external_logic_connected"):
                    host_debug_bridge["last_ext_pong"] = time.time()
                ui_c = client_ref.get("client")
                if ui_c:
                    _refresh_status_panel(ui_c)
            except Exception:
                pass
            time.sleep(1.5)

    def _ensure_status_ping_loop():
        if host_debug_bridge.get("ping_thread_started"):
            return
        host_debug_bridge["ping_thread_started"] = True
        threading.Thread(target=_status_ping_loop, daemon=True).start()

    def _host_debug_bridge_handle_message(raw_line: str):
        if not host_debug_bridge.get("client_connected"):
            host_debug_bridge["client_connected"] = True
            _refresh_status_panel(client_ref.get("client"))
        try:
            payload = json.loads(raw_line)
        except Exception:
            return
        kind = str(payload.get("kind", "") or "")
        session_id = str(payload.get("session_id", "") or "")
        expected_session = str(app_state.get("debug_session_id", "") or "")
        if expected_session and session_id and session_id != expected_session:
            return
        ui_c = client_ref.get("client")
        if kind == "register":
            prior_session = str(app_state.get("host_interconnect_session_id", "") or "")
            if session_id and session_id != prior_session:
                app_state["host_interconnect_ingress_count"] = 0
                app_state["host_interconnect_egress_count"] = 0
            if session_id:
                app_state["host_interconnect_session_id"] = session_id
            app_state["host_debug_registered"] = True
            try:
                host_debug_bridge["client_pid"] = int(payload.get("pid"))
            except Exception:
                host_debug_bridge["client_pid"] = None
            host_debug_bridge["last_host_pong"] = time.time()
            message = payload.get("message") or f"host registered pid={payload.get('pid', '?')}"
            if ui_c:
                _append_host_io_output(ui_c, f"[host-debug] {message}\n")
                _refresh_status_panel(ui_c)
            return
        if kind == "pong":
            host_debug_bridge["last_host_pong"] = time.time()
            if ui_c:
                _refresh_status_panel(ui_c)
            return
        if kind == "log":
            message = str(payload.get("message", "") or "")
            if ui_c and message:
                _append_host_io_output(ui_c, f"[C++ Host] {message}\n")
            return
        if kind == "ingress":
            app_state["host_interconnect_ingress_count"] = int(app_state.get("host_interconnect_ingress_count", 0) or 0) + 1
            if ui_c:
                kernel_id = str(payload.get("kernel", "") or "")
                _bump_kernel_interconnect_counter(ui_c, kernel_id, "ingress")
                kernel_id = str(payload.get("kernel", "") or "")
                worker = str(payload.get("worker", "") or "")
                type_name = str(payload.get("type", "") or "")
                seq = payload.get("seq")
                details = str(payload.get("details", "") or "")
                parts = [p for p in [
                    kernel_id,
                    worker,
                    type_name,
                    f"seq={seq}" if seq is not None else "",
                    details,
                ] if p]
                label = " ".join(parts) if parts else str(payload.get("message", "") or "host queued ingress")
                _append_host_io_output(ui_c, f"[ingress] {label}\n")
                _refresh_status_panel(ui_c)
            return
        if kind == "egress":
            app_state["host_interconnect_egress_count"] = int(app_state.get("host_interconnect_egress_count", 0) or 0) + 1
            if ui_c:
                kernel_id = str(payload.get("kernel", "") or "")
                _bump_kernel_interconnect_counter(ui_c, kernel_id, "egress")
                worker = str(payload.get("worker", "") or "")
                signal_type = str(payload.get("signal", "") or "")
                seq = payload.get("seq")
                details = str(payload.get("details", "") or "")
                parts = [p for p in [
                    worker,
                    signal_type,
                    f"seq={seq}" if seq is not None else "",
                    details,
                ] if p]
                label = " ".join(parts) if parts else str(payload.get("message", "") or "host received VM egress")
                _append_host_io_output(ui_c, f"[egress] {label}\n")
                _refresh_status_panel(ui_c)
            return
        if kind == "state":
            state_text = str(payload.get("status", "") or "")
            if ui_c and state_text:
                _append_host_io_output(ui_c, f"[host-state] {state_text}\n")
            app_state["host_debug_state"] = state_text
            return
        if kind == "ext_logic_listen":
            port_val = payload.get("port", 0)
            try:
                port_val = int(port_val)
            except Exception:
                port_val = 0
            host_debug_bridge["cpp_ext_logic_port"] = port_val
            if port_val > 0:
                host_debug_bridge["last_ext_pong"] = 0.0
            if ui_c:
                _append_host_io_output(ui_c, f"[ext-logic] C++ host ext_logic server on port {port_val}\n")
            return

    def _host_debug_bridge_mark_disconnected():
        host_debug_bridge["client_connected"] = False
        host_debug_bridge["client_socket"] = None
        host_debug_bridge["client_pid"] = None
        host_debug_bridge["external_logic_connected"] = False
        host_debug_bridge["cpp_ext_logic_port"] = 0
        host_debug_bridge["ext_logic_proxied"] = False
        host_debug_bridge["last_host_pong"] = 0.0
        host_debug_bridge["last_ext_pong"] = 0.0
        _refresh_status_panel(client_ref.get("client"))

    def _host_debug_launch_gui_sudo_kill(pid: int) -> bool:
        helpers = [
            ("gksudo", ["gksudo", f"kill -TERM {pid}"]),
            ("pkexec", ["pkexec", "kill", "-TERM", str(pid)]),
            ("kdesudo", ["kdesudo", f"kill -TERM {pid}"]),
        ]
        for binary, command in helpers:
            if shutil.which(binary):
                subprocess.Popen(command)
                return True
        return False

    def _host_debug_kill_connected(client):
        pid = host_debug_bridge.get("client_pid")
        if not pid:
            _append_host_io_output(client, "[host-debug] no connected external host PID to kill\n")
            _refresh_status_panel(client)
            return
        try:
            pid = int(pid)
        except Exception:
            _append_host_io_output(client, "[host-debug] invalid external host PID\n")
            _refresh_status_panel(client)
            return
        try:
            os.kill(pid, signal.SIGTERM)
            _append_host_io_output(client, f"[host-debug] sent SIGTERM to external host pid={pid}\n")
        except ProcessLookupError:
            _append_host_io_output(client, f"[host-debug] external host pid={pid} is already gone\n")
            host_debug_bridge["client_connected"] = False
            host_debug_bridge["client_pid"] = None
        except PermissionError:
            if _host_debug_launch_gui_sudo_kill(pid):
                _append_host_io_output(client, f"[host-debug] launched GUI sudo kill for external host pid={pid}\n")
            else:
                _append_host_io_output(client, "[host-debug] kill needs privileges, but no gksudo/pkexec/kdesudo helper was found\n")
        except Exception as exc:
            _append_host_io_output(client, f"[host-debug] failed to kill external host pid={pid}: {exc}\n")
        _refresh_status_panel(client)

    def _host_debug_bridge_ensure():
        if host_debug_bridge.get("server") is not None:
            return int(host_debug_bridge.get("port") or 0)

        class _HostDebugBridgeHandler(socketserver.StreamRequestHandler):
            def handle(self):
                host_debug_bridge["client_socket"] = self.request
                try:
                    while True:
                        line = self.rfile.readline()
                        if not line:
                            break
                        _host_debug_bridge_handle_message(line.decode("utf-8", errors="replace").strip())
                finally:
                    _host_debug_bridge_mark_disconnected()

        class _HostDebugBridgeServer(socketserver.ThreadingTCPServer):
            allow_reuse_address = True
            daemon_threads = True

        port = _allocate_localhost_port()
        server = _HostDebugBridgeServer(("127.0.0.1", port), _HostDebugBridgeHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host_debug_bridge["server"] = server
        host_debug_bridge["thread"] = thread
        host_debug_bridge["port"] = port
        try:
            bridge_info = {
                "host_debug_host": "127.0.0.1",
                "host_debug_port": port,
                "ui_rpc_host": args.host,
                "ui_rpc_port": int(args.port),
            }
            _bridge_info_path().write_text(json.dumps(bridge_info, indent=2), encoding="utf-8")
        except Exception:
            pass
        _refresh_status_panel(client_ref.get("client"))
        return port

    def _external_logic_bridge_ensure() -> int:
        if external_logic_bridge.get("server") is not None:
            return int(external_logic_bridge.get("port") or 0)

        def _recv_n(sock: socket.socket, n: int) -> bytes:
            buf = bytearray()
            while len(buf) < n:
                chunk = sock.recv(n - len(buf))
                if not chunk:
                    raise EOFError("connection closed")
                buf.extend(chunk)
            return bytes(buf)

        def _send_brpc(sock: socket.socket, payload: dict) -> None:
            body = _BRpcCodec.encode(payload)
            sock.sendall(struct.pack(">I", len(body)) + body)

        def _ext_dispatch(method: str, params: dict):
            if method == "ext.ping":
                return "pong"
            if method == "ext.register":
                name = (params or {}).get("name", "") or "unnamed"
                ui_c = client_ref.get("client")
                if ui_c:
                    _append_host_io_output(ui_c, f"[ext-logic] connected: {name}\n")
                return {"ok": True}
            raise RuntimeError(f"unknown method: {method}")

        def _recv_frame(sock) -> bytes:
            header = _recv_n(sock, 4)
            length = struct.unpack(">I", header)[0]
            if length > 16 * 1024 * 1024:
                raise RuntimeError("frame too large")
            return _recv_n(sock, length)

        def _send_frame(sock, body: bytes) -> None:
            sock.sendall(struct.pack(">I", len(body)) + body)

        class _ExtLogicHandler(socketserver.StreamRequestHandler):
            def handle(self):
                with external_logic_bridge["client_lock"]:
                    external_logic_bridge["client_sockets"].add(self.request)
                host_debug_bridge["external_logic_connected"] = True
                _refresh_status_panel(client_ref.get("client"))
                cpp_port = host_debug_bridge.get("cpp_ext_logic_port", 0)
                cpp_sock = None
                try:
                    if cpp_port:
                        try:
                            cpp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                            cpp_sock.settimeout(5.0)
                            cpp_sock.connect(("127.0.0.1", cpp_port))
                            cpp_sock.settimeout(None)
                            host_debug_bridge["ext_logic_proxied"] = True
                            _refresh_status_panel(client_ref.get("client"))
                            ui_c = client_ref.get("client")
                            if ui_c:
                                _append_host_io_output(ui_c, "[ext-logic] proxy active — IDE bridging Python ↔ C++\n")
                        except OSError:
                            cpp_sock = None

                    if cpp_sock:
                        # Bidirectional proxy: Python ↔ C++ with IDE intercepting ext.ping/ext.register
                        py_sock = self.request
                        stop_event = threading.Event()

                        def py_to_cpp():
                            try:
                                while not stop_event.is_set():
                                    frame = _recv_frame(py_sock)
                                    try:
                                        msg = _BRpcCodec.decode(frame)
                                    except Exception:
                                        break
                                    method = msg.get("method", "")
                                    req_id = msg.get("id")
                                    if req_id and method == "ext.ping":
                                        resp = _BRpcCodec.encode({"id": req_id, "ok": True, "result": "pong"})
                                        _send_frame(py_sock, resp)
                                    elif req_id and method == "ext.register":
                                        name = (msg.get("params") or {}).get("name", "") or "unnamed"
                                        ui_c = client_ref.get("client")
                                        if ui_c:
                                            _append_host_io_output(ui_c, f"[ext-logic] registered: {name}\n")
                                        resp = _BRpcCodec.encode({"id": req_id, "ok": True, "result": {"ok": True}})
                                        _send_frame(py_sock, resp)
                                    else:
                                        _send_frame(cpp_sock, frame)
                            except (EOFError, OSError):
                                pass
                            finally:
                                stop_event.set()
                                try: cpp_sock.shutdown(socket.SHUT_RDWR)
                                except Exception: pass

                        def cpp_to_py():
                            try:
                                while not stop_event.is_set():
                                    frame = _recv_frame(cpp_sock)
                                    _send_frame(py_sock, frame)
                            except (EOFError, OSError):
                                pass
                            finally:
                                stop_event.set()
                                try: py_sock.shutdown(socket.SHUT_RDWR)
                                except Exception: pass

                        t1 = threading.Thread(target=py_to_cpp, daemon=True)
                        t2 = threading.Thread(target=cpp_to_py, daemon=True)
                        t1.start(); t2.start()
                        t1.join(); t2.join()
                    else:
                        # No C++ side — handle locally
                        while True:
                            frame = _recv_frame(self.request)
                            try:
                                msg = _BRpcCodec.decode(frame)
                            except Exception:
                                break
                            if "id" in msg:
                                req_id = msg["id"]
                                try:
                                    result = _ext_dispatch(msg.get("method", ""), msg.get("params") or {})
                                    _send_brpc(self.request, {"id": req_id, "ok": True, "result": result})
                                except Exception as exc:
                                    _send_brpc(self.request, {"id": req_id, "ok": False,
                                               "error": {"code": "error", "message": str(exc)}})
                except (EOFError, OSError):
                    pass
                finally:
                    with external_logic_bridge["client_lock"]:
                        external_logic_bridge["client_sockets"].discard(self.request)
                    if cpp_sock:
                        try: cpp_sock.close()
                        except Exception: pass
                    host_debug_bridge["external_logic_connected"] = False
                    host_debug_bridge["ext_logic_proxied"] = False
                    _refresh_status_panel(client_ref.get("client"))

        class _ExtLogicServer(socketserver.ThreadingTCPServer):
            allow_reuse_address = True
            daemon_threads = True

        port = _allocate_localhost_port()
        server = _ExtLogicServer(("127.0.0.1", port), _ExtLogicHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        external_logic_bridge["server"] = server
        external_logic_bridge["thread"] = thread
        external_logic_bridge["port"] = port
        return port

    def _bridge_info_path() -> Path:
        return Path(tempfile.gettempdir()) / "elara-debug-bridge.json"

    def _debug_session_dir(project_root: Path | None = None) -> Path:
        if project_root:
            session_dir = project_root / ".elaraproject" / "debug-sessions"
        else:
            session_dir = Path(tempfile.gettempdir()) / "epa-ide-debug-sessions"
        session_dir.mkdir(parents=True, exist_ok=True)
        return session_dir

    def _build_debug_session_descriptor() -> dict:
        project_root_text = app_state.get("project_root", "")
        project_root = Path(project_root_text) if project_root_text else None
        project_name = app_state.get("project_name") or (project_root.name if project_root else "")
        bundle_path = str((project_root / "build" / "epa.bin")) if project_root else ""
        session_id = app_state.get("debug_session_id")
        if not session_id:
            session_id = f"{int(time.time())}-{uuid.uuid4().hex[:8]}"
            app_state["debug_session_id"] = session_id
        return {
            "session_id": session_id,
            "mode": "debug",
            "project_name": project_name,
            "project_root": str(project_root) if project_root else "",
            "bundle_path": bundle_path,
            "ui_rpc_host": args.host,
            "ui_rpc_port": int(args.port),
            "ai_rpc_host": args.host,
            "ai_rpc_port": int(args.ai_rpc_port),
            "epa_dbg_host": "127.0.0.1",
            "epa_dbg_port": int(_epa_dbg_port() or 0),
            "host_debug_host": "127.0.0.1",
            "host_debug_port": int(_host_debug_bridge_ensure() or 0),
            "ext_logic_host": "127.0.0.1",
            "ext_logic_port": int(_external_logic_bridge_ensure() or 0),
            "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }

    def _write_debug_session_descriptor() -> Path | None:
        project_root_text = app_state.get("project_root", "")
        project_root = Path(project_root_text) if project_root_text else None
        descriptor = _build_debug_session_descriptor()
        session_dir = _debug_session_dir(project_root)
        session_path = session_dir / f"{descriptor['session_id']}.json"
        content = json.dumps(descriptor, indent=2)
        session_path.write_text(content, encoding="utf-8")
        (session_dir / "latest.json").write_text(content, encoding="utf-8")
        app_state["debug_session_path"] = str(session_path)
        return session_path

    def _project_cpp_root(project_root: Path) -> Path:
        return project_root / "cpp"

    def _project_python_root(project_root: Path) -> Path:
        return project_root / "python"

    def _project_epa_root(project_root: Path) -> Path:
        return project_root / "epa"

    def _project_has_python_root(project_root: Path | None) -> bool:
        return bool(project_root and _project_python_root(project_root).is_dir())

    def _project_technologies(project_root: Path | None) -> list[str]:
        if not project_root:
            return []
        techs: list[str] = []
        try:
            meta = _project_meta(project_root)
            for tech in meta.get("technologies", []):
                if isinstance(tech, str) and tech not in techs:
                    techs.append(tech)
        except Exception:
            pass
        for tech, root_fn in (
            ("epa", _project_epa_root),
            ("cpp", _project_cpp_root),
            ("python", _project_python_root),
        ):
            try:
                if root_fn(project_root).is_dir() and tech not in techs:
                    techs.append(tech)
            except Exception:
                pass
        return techs

    def _project_add_menu_items() -> list[dict]:
        project_root_text = app_state.get("project_root", "")
        project_root = Path(project_root_text) if project_root_text else None
        techs = set(_project_technologies(project_root))
        items: list[dict] = []
        if "epa" in techs:
            items.append({"label": "New E File", "action": "project_add.new_file.E"})
        else:
            items.append({"label": "Add EPA Technology", "action": "project_add.add_tech.epa"})
        if "cpp" in techs:
            items.append({"label": "New C++ File", "action": "project_add.new_file.Cpp"})
        else:
            items.append({"label": "Add C++ Technology", "action": "project_add.add_tech.cpp"})
        if "python" in techs:
            items.append({"label": "New Python File", "action": "project_add.new_file.Python"})
        else:
            items.append({"label": "Add Python Technology", "action": "project_add.add_tech.python"})
        project_root = app_state.get("project_root", "")
        if project_root and (Path(project_root) / "3d_artifacts").is_dir():
            items.append({"label": "New 3D Artifact Template", "action": "project_add.new_file.Artifact3D"})
        return items

    def _write_project_meta(project_root: Path, meta: dict):
        meta_path = project_root / ".elaraproject" / "project.json"
        meta_path.parent.mkdir(parents=True, exist_ok=True)
        meta_path.write_text(json.dumps(meta, indent=2), encoding="utf-8")

    def _project_add_technology(client, tech: str, options: dict | None = None):
        options = options or {}
        project_root_text = app_state.get("project_root", "")
        if not project_root_text:
            raise RuntimeError("No project open.")
        project_root = Path(project_root_text)
        meta = _project_meta(project_root)
        techs = _project_technologies(project_root)
        if tech in techs:
            return
        project_name = meta.get("name", project_root.name)
        rpc_host = str(meta.get("rpc_host", "127.0.0.1"))
        rpc_port = int(meta.get("rpc_port", 18777))
        python_multi_cpu = bool(meta.get("python_multi_cpu", False))
        cpp_epa_vm_host = bool(options.get("cpp_epa_vm_host", meta.get("cpp_epa_vm_host", False)))
        cpp_epa_debug_rpc = bool(options.get("cpp_epa_debug_rpc", meta.get("cpp_epa_debug_rpc", False)))
        ui_template = str(options.get("ui_template", meta.get("ui_template", "tabbed-control-panel")))
        if ui_template == "epa-os":
            ui_template = "vulkan-surface"
        (project_root / "build").mkdir(parents=True, exist_ok=True)
        if tech == "epa":
            epa_root = _project_epa_root(project_root)
            epa_root.mkdir(parents=True, exist_ok=True)
            (project_root / "build" / "epa").mkdir(parents=True, exist_ok=True)
            entry_path = epa_root / "entry.e"
            if not entry_path.exists():
                entry_path.write_text(_e_file_content("entry.e", "root_node"), encoding="utf-8")
        elif tech == "cpp":
            builder = _ensure_project_builder()
            cpp_root = _project_cpp_root(project_root)
            env = os.environ.copy()
            env["LC_ALL"] = "C"
            subprocess.run(
                [
                    str(builder),
                    "--non-interactive",
                    "--app-kind", "ui",
                    "--ui-client-language", "cpp",
                    "--ui-template", ui_template,
                    "--epa-vm-host", "yes" if cpp_epa_vm_host else "no",
                    "--epa-debug-rpc", "yes" if cpp_epa_debug_rpc else "no",
                    "--address", rpc_host,
                    "--port", str(rpc_port),
                    "--name", project_name,
                    "--output", str(cpp_root),
                ],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=env,
            )
        elif tech == "python":
            builder = _ensure_project_builder()
            python_root = _project_python_root(project_root)
            env = os.environ.copy()
            env["LC_ALL"] = "C"
            subprocess.run(
                [
                    str(builder),
                    "--non-interactive",
                    "--app-kind", "ui",
                    "--ui-client-language", "python",
                    "--ui-template", ui_template,
                    "--multi-cpu-python", "yes" if python_multi_cpu else "no",
                    "--address", rpc_host,
                    "--port", str(rpc_port),
                    "--name", project_name,
                    "--output", str(python_root),
                ],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=env,
            )
        else:
            raise RuntimeError(f"Unknown technology: {tech}")

        updated_techs = [t for t in techs if t in ("epa", "cpp", "python")]
        if tech not in updated_techs:
            updated_techs.append(tech)
        meta["technologies"] = updated_techs
        if tech == "cpp":
            meta["ui_template"] = ui_template
            meta["cpp_epa_vm_host"] = cpp_epa_vm_host
            meta["cpp_epa_debug_rpc"] = cpp_epa_debug_rpc
        _write_project_meta(project_root, meta)
        _open_project(client, project_root)

    def _default_cpp_tech_state() -> dict:
        project_root_text = app_state.get("project_root", "")
        project_root = Path(project_root_text) if project_root_text else None
        meta = _project_meta(project_root) if project_root else {}
        ui_template = str(meta.get("ui_template", "tabbed-control-panel"))
        if ui_template == "epa-os":
            ui_template = "vulkan-surface"
        if ui_template not in ("vulkan-surface", "tabbed-control-panel", "rich-editor"):
            ui_template = "tabbed-control-panel"
        vm_host = bool(meta.get("cpp_epa_vm_host", False))
        debug_rpc = bool(meta.get("cpp_epa_debug_rpc", False)) if vm_host else False
        if ui_template == "vulkan-surface" and "cpp_epa_vm_host" not in meta:
            vm_host = False
            debug_rpc = False
        return {
            "ui_template": ui_template,
            "cpp_epa_vm_host": vm_host,
            "cpp_epa_debug_rpc": debug_rpc,
        }

    def _open_cpp_technology_wizard(client):
        cpp_tech_state.clear()
        cpp_tech_state.update(_default_cpp_tech_state())
        if client is None:
            return
        client.close_window("project-add-menu")
        client.open_window(
            "add-cpp-technology",
            "Add C++ Technology",
            460,
            330,
            build_cpp_technology_wizard(cpp_tech_state),
        )

    def _read_make_var(makefile: Path, name: str) -> str:
        try:
            text = makefile.read_text(encoding="utf-8", errors="replace")
        except Exception:
            return ""
        match = re.search(rf"(?m)^\s*{re.escape(name)}\s*[:?+]?=\s*(.+?)\s*$", text)
        return match.group(1).strip() if match else ""

    def _project_cpp_target(project_root: Path) -> str:
        cpp_root = _project_cpp_root(project_root)
        target = _read_make_var(cpp_root / "Makefile", "TARGET")
        if target:
            return target
        meta = _project_meta(project_root)
        return meta.get("name", project_root.name)

    def _project_cpp_binary(project_root: Path) -> Path:
        cpp_root = _project_cpp_root(project_root)
        return cpp_root / "build" / _project_cpp_target(project_root)

    def _project_cpp_gdb_args(project_root: Path) -> list[str]:
        meta = _project_meta(project_root)
        host = str(meta.get("rpc_host", "127.0.0.1"))
        port = str(meta.get("rpc_port", 18777))
        return [host, port]

    def _history_dir() -> Path | None:
        project_root = app_state.get("project_root", "")
        if not project_root:
            return None
        d = Path(project_root) / ".elaraproject" / "history"
        try:
            d.mkdir(parents=True, exist_ok=True)
        except Exception:
            return None
        return d

    def _save_history(tab_id: str):
        state = editor_state.get(tab_id)
        if not state:
            return
        d = _history_dir()
        if not d:
            return
        data = {
            "undo_stack": state.get("undo_stack", [])[-100:],
            "redo_stack": state.get("redo_stack", [])[-100:],
        }
        try:
            (d / f"{tab_id}.json").write_text(json.dumps(data), encoding="utf-8")
        except Exception:
            pass

    def _load_history(tab_id: str):
        state = editor_state.get(tab_id)
        if not state:
            return
        d = _history_dir()
        if not d:
            return
        try:
            data = json.loads((d / f"{tab_id}.json").read_text(encoding="utf-8"))
            state["undo_stack"] = data.get("undo_stack", [])
            state["redo_stack"] = data.get("redo_stack", [])
        except Exception:
            pass

    def _project_e_compile_units(project_root: Path) -> list[Path]:
        epa_root = project_root / "epa"
        if not epa_root.is_dir():
            return []
        entry = epa_root / "entry.e"
        if not entry.is_file():
            raise RuntimeError(f"Missing root kernel compile unit: {entry}")
        others = sorted(
            p for p in epa_root.rglob("*.e")
            if p.is_file() and p.name != "entry.e"
        )
        return [entry] + others

    def _run_subprocess(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess:
        return subprocess.run(
            cmd,
            cwd=str(cwd) if cwd else None,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env={**os.environ, "LC_ALL": "C"},
        )

    def _set_build_output(client, text: str):
        try:
            app_state["bottom_panel_visible"] = True
            _apply_bottom_panel_visibility(client, True)
            _set_bottom_view(client, "build")
            client.set_text("bottom.build_output", text)
            client.set_visible("bottom.build_output", True)
            client.scroll_to_bottom("bottom.build_output")
        except Exception:
            pass

    def _append_build_output(client, line: str):
        current = app_state.get("bottom_build_output", "")
        next_text = current + line
        app_state["bottom_build_output"] = next_text
        _set_build_output(client, next_text)

    def _clear_build_output(client):
        app_state["bottom_build_output"] = ""
        _set_build_output(client, "")

    def _set_host_io_output(client, text: str):
        try:
            app_state["bottom_panel_visible"] = True
            _apply_bottom_panel_visibility(client, True)
            _set_bottom_view(client, "host_io")
            client.set_text("bottom.host_io_output", text)
            client.set_visible("bottom.host_io_output", True)
            client.scroll_to_bottom("bottom.host_io_output")
        except Exception:
            pass

    def _append_host_io_output(client, line: str):
        current = app_state.get("bottom_host_io_output", "")
        next_text = current + line
        app_state["bottom_host_io_output"] = next_text
        _set_host_io_output(client, next_text)

    def _clear_host_io_output(client):
        app_state["bottom_host_io_output"] = ""
        _set_host_io_output(client, "")

    def _run_subprocess_streaming(cmd: list[str], client, cwd: Path | None = None) -> subprocess.CompletedProcess:
        proc = subprocess.Popen(
            cmd,
            cwd=str(cwd) if cwd else None,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env={**os.environ, "LC_ALL": "C"},
        )
        output_lines = []
        try:
            if proc.stdout is not None:
                for line in proc.stdout:
                    output_lines.append(line)
                    _append_build_output(client, line)
        finally:
            if proc.stdout is not None:
                proc.stdout.close()
        returncode = proc.wait()
        stdout_text = "".join(output_lines)
        if returncode != 0:
            raise subprocess.CalledProcessError(returncode, cmd, output=stdout_text, stderr=stdout_text)
        return subprocess.CompletedProcess(cmd, returncode, stdout_text, "")

    def _compile_epa_project(client):
        project_root_text = app_state.get("project_root", "")
        if not project_root_text:
            _set_build_output(client, "Compile E/EPA skipped: no project open.\n")
            return

        project_root = Path(project_root_text)
        meta = _project_meta(project_root)
        technologies = set(meta.get("technologies", []))
        if "epa" not in technologies:
            _set_build_output(client, f"Compile E/EPA skipped: project has no EPA technology.\n")
            return

        build_dir = project_root / "build"
        build_dir.mkdir(parents=True, exist_ok=True)
        units = _project_e_compile_units(project_root)
        bundle = _ensure_e2epabin()
        cmd = [str(bundle), "--out", str(build_dir / "epa.bin")] + [str(p) for p in units]

        _clear_build_output(client)
        _append_build_output(client, f"Compile E/EPA started: {project_root}\n")
        _append_build_output(client, "$ " + " ".join(cmd) + "\n")
        try:
            _run_subprocess_streaming(cmd, client, cwd=project_root)
            _append_build_output(client, f"Compile E/EPA complete: {build_dir / 'epa.bin'}\n")
            _open_project(client, project_root)
        except (subprocess.CalledProcessError, RuntimeError) as exc:
            message = ""
            if isinstance(exc, subprocess.CalledProcessError):
                message = (exc.stderr or exc.stdout or str(exc)).strip()
            else:
                message = str(exc)
            if message:
                _append_build_output(client, message + "\n")
            _append_build_output(client, "Compile E/EPA failed.\n")

    def _compile_current_file(client):
        tab_id = app_state.get("active_editor_tab", "")
        if not tab_id:
            _set_build_output(client, "Compile current file skipped: no active editor tab.\n")
            return
        entry = next((t for t in tab_list if t.get("tab_id") == tab_id), None)
        state = editor_state.get(tab_id)
        if not entry or not state:
            _set_build_output(client, "Compile current file skipped: active tab state not found.\n")
            return
        file_path = entry.get("path", "")
        if _ext_for_path(file_path) != ".e":
            _set_build_output(client, f"Compile current file skipped: {Path(file_path).name} is not an E file.\n")
            return

        source_dir = Path(file_path).parent if file_path else None
        _clear_build_output(client)
        _append_build_output(client, f"Compile current file: {file_path}\n")
        result = _compile_e_source(state.get("source_text", ""), source_dir)
        if result.get("message"):
            _append_build_output(client, result["message"] + "\n")
        if result.get("ok"):
            state["epa_text"] = result.get("epa_text", "")
            state["epa_block_map"] = result.get("epa_block_map", {})
            state["epa_map_text"] = result.get("epa_map_text", "")
            state["compile_error"] = ""
            state["epa_map_path"] = _persist_debug_map(tab_id, result.get("epa_map_text", ""))
            _append_build_output(client, "Compile current file complete.\n")
            _refresh_e_tab(client, tab_id)
        else:
            state["compile_error"] = result.get("message", "compile failed")
            _append_build_output(client, "Compile current file failed.\n")

    def _build_project(client, rebuild: bool = False):
        project_root_text = app_state.get("project_root", "")
        if not project_root_text:
            print(json.dumps({"build": "skipped", "reason": "no_project_open"}, indent=2), flush=True)
            _set_build_output(client, "Build skipped: no project open.\n")
            return

        project_root = Path(project_root_text)
        meta = _project_meta(project_root)
        technologies = set(meta.get("technologies", []))
        build_steps = []
        _clear_build_output(client)
        _append_build_output(client, f"Build started: {project_root}\n")
        if _epa_dbg_running():
            _append_build_output(client, "[build] stopping epavm before build\n")
            _epa_dbg_stop()

        try:
            if "epa" in technologies:
                bundle = _ensure_e2epabin()
                build_dir = project_root / "build"
                build_dir.mkdir(parents=True, exist_ok=True)
                units = _project_e_compile_units(project_root)
                cmd = [str(bundle), "--out", str(build_dir / "epa.bin")] + [str(p) for p in units]
                _append_build_output(client, "$ " + " ".join(cmd) + "\n")
                result = _run_subprocess_streaming(cmd, client, cwd=project_root)
                build_steps.append({
                    "step": "epa_bundle",
                    "command": cmd,
                    "stdout": result.stdout.strip(),
                    "stderr": result.stderr.strip(),
                    "outputs": {
                        "bundle": str(build_dir / "epa.bin"),
                        "epaasm_dir": str(build_dir / "epaasm"),
                        "blobs_dir": str(build_dir / "blobs"),
                    },
                })

            if "cpp" in technologies:
                cpp_root = project_root / "cpp"
                makefile = cpp_root / "Makefile"
                if not makefile.is_file():
                    raise RuntimeError(f"Missing C++ Makefile: {makefile}")
                _cpp_gdb_stop_session()
                if rebuild:
                    clean_cmd = ["make", "-C", str(cpp_root), "clean"]
                    _append_build_output(client, "$ " + " ".join(clean_cmd) + "\n")
                    clean_result = _run_subprocess_streaming(clean_cmd, client, cwd=cpp_root)
                    build_steps.append({
                        "step": "cpp_clean",
                        "command": clean_cmd,
                        "stdout": clean_result.stdout.strip(),
                        "stderr": clean_result.stderr.strip(),
                    })
                build_cmd = ["make", "-C", str(cpp_root), "BUILD_PROFILE=debug", "-j2"]
                _append_build_output(client, "$ " + " ".join(build_cmd) + "\n")
                cpp_result = _run_subprocess_streaming(build_cmd, client, cwd=cpp_root)
                build_steps.append({
                    "step": "cpp_build",
                    "command": build_cmd,
                    "stdout": cpp_result.stdout.strip(),
                    "stderr": cpp_result.stderr.strip(),
                    "outputs": {
                        "binary": str(_project_cpp_binary(project_root)),
                    },
                })

            print(json.dumps({
                "build": "ok",
                "project": str(project_root),
                "steps": build_steps,
            }, indent=2), flush=True)
            _append_build_output(client, "Build complete.\n")
            _open_project(client, project_root)
        except (subprocess.CalledProcessError, RuntimeError) as exc:
            message = ""
            command = None
            if isinstance(exc, subprocess.CalledProcessError):
                message = (exc.stderr or exc.stdout or str(exc)).strip()
                command = exc.cmd
            else:
                message = str(exc)
            print(json.dumps({
                "build": "failed",
                "project": str(project_root),
                "command": command,
                "message": message,
            }, indent=2), flush=True)
            if message:
                _append_build_output(client, message + "\n")
            _append_build_output(client, "Build failed.\n")

    def _clean_project():
        project_root_text = app_state.get("project_root", "")
        if not project_root_text:
            print(json.dumps({"clean": "skipped", "reason": "no_project_open"}, indent=2), flush=True)
            return
        project_root = Path(project_root_text)
        removed = []
        _cpp_gdb_stop_session()
        _epa_dbg_stop()
        build_dir = project_root / "build"
        if build_dir.exists():
            shutil.rmtree(build_dir)
            removed.append(str(build_dir))
        cpp_root = project_root / "cpp"
        makefile = cpp_root / "Makefile"
        if makefile.is_file():
            try:
                result = _run_subprocess(["make", "-C", str(cpp_root), "clean"], cwd=cpp_root)
                print(json.dumps({
                    "clean": "ok",
                    "project": str(project_root),
                    "removed": removed,
                    "cpp_clean": {
                        "stdout": result.stdout.strip(),
                        "stderr": result.stderr.strip(),
                    },
                }, indent=2), flush=True)
                return
            except subprocess.CalledProcessError as exc:
                print(json.dumps({
                    "clean": "failed",
                    "project": str(project_root),
                    "command": exc.cmd,
                    "message": (exc.stderr or exc.stdout or str(exc)).strip(),
                }, indent=2), flush=True)
                return
        print(json.dumps({
            "clean": "ok",
            "project": str(project_root),
            "removed": removed,
        }, indent=2), flush=True)

    def _extract_section_block(text: str, heading: str):
        lines = text.splitlines()
        start = None
        for idx, line in enumerate(lines):
            if line.strip() == heading:
                start = idx
                break
        if start is None:
            return ""
        end = len(lines)
        for idx in range(start + 1, len(lines)):
            line = lines[idx]
            if line and not line.startswith(" "):
                end = idx
                break
        return "\n".join(lines[start:end]).strip()

    def _replace_tree_nodes(client, target: str, nodes):
        document = json.dumps({"nodes": nodes}, separators=(",", ":"))
        client.call("ui.replaceChildren", {"target": target, "document": document})

    def _parse_tree_lines(block: str, root_label: str, root_id: str):
        root = {"id": root_id, "label": root_label, "expanded": True, "children": []}
        if not block.strip():
            root["children"].append({"id": f"{root_id}.empty", "label": "Unavailable"})
            return [root]
        stack = [(-1, root)]
        for index, raw_line in enumerate(block.splitlines()):
            if not raw_line.strip():
                continue
            indent = len(raw_line) - len(raw_line.lstrip(" "))
            node = {
                "id": f"{root_id}.{index}",
                "label": raw_line.strip(),
            }
            while len(stack) > 1 and indent <= stack[-1][0]:
                stack.pop()
            parent = stack[-1][1]
            parent.setdefault("children", []).append(node)
            stack.append((indent, node))
        return [root]

    def _build_trace_nodes(values: list, root_id: str):
        root = {"id": root_id, "label": "Stack (LIFO)", "expanded": True, "children": []}
        if not values:
            root["children"].append({"id": f"{root_id}.empty", "label": "Stack empty"})
        else:
            for i, v in enumerate(values):
                label = f"0x{v & 0xFFFFFFFF:08X}" + (" ← TOS" if i == 0 else "")
                root["children"].append({"id": f"{root_id}.{i}", "label": label})
        return [root]

    def _hex_u32(value) -> str:
        return f"0x{int(value) & 0xFFFFFFFF:08X}"

    def _chunk_hex_lines(hex_text: str, base_offset: int = 0, width: int = 16) -> list[str]:
        if not hex_text:
            return []
        try:
            data = bytes.fromhex(hex_text)
        except Exception:
            return [hex_text]
        lines = []
        for i in range(0, len(data), width):
            chunk = data[i:i + width]
            hex_part = " ".join(f"{b:02X}" for b in chunk)
            ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            lines.append(f"+0x{base_offset + i:04X}  {hex_part:<47}  {ascii_part}")
        return lines

    def _runtime_tree(root_id: str, label: str, children: list[dict]):
        return [{"id": root_id, "label": label, "expanded": True, "children": children or [{"id": f"{root_id}.empty", "label": "Unavailable"}]}]

    def _build_runtime_trace_nodes(snapshot_worker: dict | None, inspect: dict | None, root_id: str):
        if not snapshot_worker and not inspect:
            return _runtime_tree(root_id, "Trace", [{"id": f"{root_id}.empty", "label": "No worker selected"}])
        worker = snapshot_worker or {}
        eip = (inspect or {}).get("eip") or worker.get("eip") or {}
        regs = (inspect or {}).get("regs") or worker.get("regs") or []
        queues = (inspect or {}).get("queues") or {}
        flags = (inspect or {}).get("flags") or {}
        children = [
            {"id": f"{root_id}.wid", "label": f"wid = {worker.get('wid', (inspect or {}).get('wid', '?'))}"},
            {"id": f"{root_id}.eip", "label": f"eip = block_type {eip.get('block_type', '?')} block_id {eip.get('block_id', '?')} rel_pc +0x{int(eip.get('rel_pc', 0) or 0):X}"},
            {"id": f"{root_id}.regs", "label": "regs", "expanded": True, "children": [
                {"id": f"{root_id}.regs.{i}", "label": f"csc[{i}] = {_hex_u32(v)} ({int(v)})"} for i, v in enumerate(regs)
            ] or [{"id": f"{root_id}.regs.empty", "label": "Unavailable"}]},
            {"id": f"{root_id}.queues", "label": f"queues in={queues.get('inq_count', worker.get('inq_count', 0))} out={queues.get('outq_count', worker.get('outq_count', 0))}"},
            {"id": f"{root_id}.flags", "label": f"flags halted={int(bool(flags.get('halted', worker.get('halted'))))} blocked={int(bool(flags.get('blocked', worker.get('blocked'))))} faulted={int(bool(flags.get('faulted', worker.get('faulted'))))} waiting={int(bool(flags.get('waiting_for_data', worker.get('waiting_for_data'))))} running={int(bool(flags.get('at_running', worker.get('at_running'))))}"},
        ]
        return _runtime_tree(root_id, "Trace", children)

    def _build_runtime_stack_nodes(inspect: dict | None, root_id: str):
        if not inspect:
            return _runtime_tree(root_id, "Stack", [{"id": f"{root_id}.empty", "label": "No runtime stack data"}])
        stack = inspect.get("stack") or {}
        words = stack.get("words") or []
        depth = int(stack.get("depth", 0) or 0)
        start_index = int(stack.get("start_index", 0) or 0)
        children = [{"id": f"{root_id}.depth", "label": f"depth = {depth}"}]
        if not words:
            children.append({"id": f"{root_id}.empty", "label": "Stack empty"})
        else:
            stack_children = []
            for idx, value in enumerate(words):
                absolute_index = start_index + idx
                tos = " ← TOS" if absolute_index == depth - 1 else ""
                stack_children.append({"id": f"{root_id}.word.{absolute_index}", "label": f"[{absolute_index}] = {_hex_u32(value)} ({int(value)}){tos}"})
            children.append({"id": f"{root_id}.words", "label": "words", "expanded": True, "children": stack_children})
        return _runtime_tree(root_id, "Stack Interpretation", children)

    def _build_runtime_local_nodes(inspect: dict | None, root_id: str):
        if not inspect:
            return _runtime_tree(root_id, "Local Arena", [{"id": f"{root_id}.empty", "label": "No local data"}])
        locals_block = inspect.get("locals") or {}
        arena = inspect.get("local_arena") or {}
        values = locals_block.get("values") or []
        children = []
        children.append({"id": f"{root_id}.vm_locals", "label": "vm locals", "expanded": True, "children": [
            {"id": f"{root_id}.vm_locals.{i}", "label": f"local[{i}] = {_hex_u32(v)} ({int(v)})"} for i, v in enumerate(values)
        ] or [{"id": f"{root_id}.vm_locals.empty", "label": "Unavailable"}]})
        children.append({"id": f"{root_id}.arena_meta", "label": f"arena top={arena.get('top', 0)} cap={arena.get('cap', 0)} scope_depth={arena.get('scope_depth', 0)}"})
        preview_lines = _chunk_hex_lines(str(arena.get("preview_hex", "") or ""), int(arena.get("preview_from", 0) or 0))
        children.append({"id": f"{root_id}.arena_bytes", "label": f"arena bytes ({arena.get('preview_len', 0)} bytes)", "expanded": True, "children": [
            {"id": f"{root_id}.arena_bytes.{i}", "label": line} for i, line in enumerate(preview_lines)
        ] or [{"id": f"{root_id}.arena_bytes.empty", "label": "No arena bytes in preview"}]})
        return _runtime_tree(root_id, "Local Arena", children)

    def _build_runtime_ghs_nodes(inspect: dict | None, root_id: str):
        if not inspect:
            return _runtime_tree(root_id, "GHS Layout", [{"id": f"{root_id}.empty", "label": "No GHS data"}])
        ghs = inspect.get("ghs") or {}
        children = [
            {"id": f"{root_id}.present", "label": f"present = {ghs.get('present', False)}"},
            {"id": f"{root_id}.handle", "label": f"handle = 0x{int(ghs.get('handle', 0) or 0):016X}"},
            {"id": f"{root_id}.pool", "label": f"pool live={ghs.get('live_count', 0)} capacity={ghs.get('capacity', 0)}"},
        ]
        meta = ghs.get("meta") or {}
        if meta:
            children.append({"id": f"{root_id}.meta", "label": "meta", "expanded": True, "children": [
                {"id": f"{root_id}.meta.type", "label": f"type = {meta.get('type_name', '?')} ({meta.get('type', '?')})"},
                {"id": f"{root_id}.meta.owner", "label": f"owner = {meta.get('owner', '?')}"},
                {"id": f"{root_id}.meta.flags", "label": f"flags = {_hex_u32(meta.get('flags', 0))}"},
                {"id": f"{root_id}.meta.size", "label": f"size_bytes = {meta.get('size_bytes', 0)}"},
                {"id": f"{root_id}.meta.cap", "label": f"capacity = {meta.get('capacity', 0)}"},
                {"id": f"{root_id}.meta.gen", "label": f"generation = {meta.get('generation', 0)}"},
            ]})
        preview_lines = _chunk_hex_lines(str(ghs.get("preview_hex", "") or ""), 0)
        children.append({"id": f"{root_id}.bytes", "label": f"payload preview ({ghs.get('preview_len', 0)} bytes)", "expanded": True, "children": [
            {"id": f"{root_id}.bytes.{i}", "label": line} for i, line in enumerate(preview_lines)
        ] or [{"id": f"{root_id}.bytes.empty", "label": "No GHS payload preview"}]})
        return _runtime_tree(root_id, "GHS Layout", children)

    def _build_runtime_dynamic_nodes(inspect: dict | None, root_id: str):
        if not inspect:
            return _runtime_tree(root_id, "Dynamic Memory", [{"id": f"{root_id}.empty", "label": "No dynamic memory data"}])
        ghs = inspect.get("ghs") or {}
        arena = inspect.get("local_arena") or {}
        children = [
            {"id": f"{root_id}.summary", "label": f"GHS live={ghs.get('live_count', 0)} cap={ghs.get('capacity', 0)} current_valid={ghs.get('valid', False)}"},
            {"id": f"{root_id}.arena", "label": f"local arena top={arena.get('top', 0)} cap={arena.get('cap', 0)}"},
        ]
        return _runtime_tree(root_id, "Dynamic Memory", children)

    def _extract_debug_candidates(source_text: str):
        type_names = re.findall(r"^\s*type\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", source_text, flags=re.MULTILINE)
        worker_names = re.findall(r"^\s*worker\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", source_text, flags=re.MULTILINE)
        seen = set()
        ordered_types = []
        for name in type_names:
            if name not in seen:
                seen.add(name)
                ordered_types.append(name)
        seen.clear()
        ordered_workers = []
        for name in worker_names:
            if name not in seen:
                seen.add(name)
                ordered_workers.append(name)
        return ordered_types, ordered_workers

    def _first_marker_line(epa_text: str):
        lines = epa_text.splitlines()
        for idx, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith("WAIT_FOR_DATA"):
                return idx
        for idx, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith("ENTRY_START") or stripped.startswith("FUNC_START"):
                return idx
        for idx, line in enumerate(lines):
            stripped = line.strip()
            if stripped and not stripped.startswith(";"):
                return idx
        return 0

    def _debug_preview_text(epa_text: str, marker_line: int | None = None, radius: int = 5,
                            epa_map: list = None):
        if not epa_text.strip():
            return "# Debug Trace\n\nNo EPA output available.\n"
        lines = epa_text.splitlines()
        marker = _first_marker_line(epa_text) if marker_line is None else max(0, min(marker_line, len(lines) - 1))
        e_source_line = 0
        if epa_map and 0 <= marker < len(epa_map):
            e_source_line = epa_map[marker]
        start = max(0, marker - radius)
        end = min(len(lines), marker + radius + 1)
        header = [f"# Debug Trace", "", f"epa_line={marker + 1}"]
        if e_source_line > 0:
            header.append(f"e_source_line={e_source_line}")
        header.append("")
        width = len(str(end))
        body = []
        for idx in range(start, end):
            prefix = ">>" if idx == marker else "  "
            body.append(f"{prefix} {idx + 1:>{width}} | {lines[idx]}")
        return "\n".join(header + body) + "\n"

    def _analyze_e_source(source_text: str, ids: dict, source_dir: Path = None):
        semantic = _ensure_ec()
        with tempfile.TemporaryDirectory(prefix="epa-ide-ec-") as tmp:
            tmp_path = Path(tmp)
            if source_dir and source_dir.is_dir():
                source_path = source_dir / f"._epa_ide_buf_{os.getpid()}.e"
            else:
                source_path = tmp_path / "buffer.e"
            source_path.write_text(source_text, encoding="utf-8")
            try:
                proc = subprocess.run(
                    [str(semantic), str(source_path)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
            finally:
                if source_dir and source_path.exists():
                    source_path.unlink(missing_ok=True)
            if proc.returncode != 0:
                message = (proc.stderr or proc.stdout or "semantic analysis failed").strip()
                return {
                    "ok": False,
                    "message": message,
                    "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root"),
                    "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                    "local_nodes": _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root"),
                    "dynamic_nodes": _parse_tree_lines("", "Dynamic Memory", f"{ids['debug_dynamic']}.root"),
                }

            stdout = proc.stdout or ""
            ghs_block = _extract_section_block(stdout, "type-ghs-layouts")
            frame_block = _extract_section_block(stdout, "function-ghs-frames")
            local_lines = []
            stack_lines = []
            for line in frame_block.splitlines():
                stripped = line.strip()
                if "storage=local-scope-arena" in stripped:
                    local_lines.append(stripped)
                elif stripped.startswith("local ") or stripped.startswith("func "):
                    stack_lines.append(stripped)
            dynamic_block = _extract_section_block(stdout, "dynamic-memory")
            return {
                "ok": True,
                "message": "",
                "ghs_nodes": _parse_tree_lines(ghs_block or "No declared GHS layouts.", "GHS Layout", f"{ids['debug_ghs']}.root"),
                "stack_nodes": _parse_tree_lines("\n".join(stack_lines).strip() or "No stack frame data.", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                "local_nodes": _parse_tree_lines("\n".join(local_lines).strip() or "No local arena allocations.", "Local Arena", f"{ids['debug_local']}.root"),
                "dynamic_nodes": _parse_tree_lines(dynamic_block or "No dynamic allocations.", "Dynamic Memory", f"{ids['debug_dynamic']}.root"),
            }

    def _diagnostic_from_error(message: str):
        match = re.search(r"\bat (\d+):(\d+)\b", message)
        if not match:
            match = re.search(r"\b(\d+):(\d+)\b", message)
        if not match:
            return []

        line = max(0, int(match.group(1)) - 1)
        column = max(0, int(match.group(2)) - 1)
        token_match = re.search(r"near '([^']*)'", message)
        length = 1
        if token_match:
            token = token_match.group(1)
            if token:
                length = len(token)
        return [{
            "line": line,
            "column": column,
            "length": max(1, length),
            "message": message.strip(),
        }]

    def _compile_e_source(source_text: str, source_dir: Path = None):
        compiler = _ensure_e2epa()
        with tempfile.TemporaryDirectory(prefix="epa-ide-e2epa-") as tmp:
            tmp_path = Path(tmp)
            if source_dir and source_dir.is_dir():
                source_path = source_dir / f"._epa_ide_buf_{os.getpid()}.e"
            else:
                source_path = tmp_path / "buffer.e"
            output_path = tmp_path / "buffer.epaasm"
            map_path = tmp_path / "buffer.epamap"
            source_path.write_text(source_text, encoding="utf-8")
            try:
                proc = subprocess.run(
                    [str(compiler), str(source_path), str(output_path), str(map_path)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
            finally:
                if source_dir and source_path.exists():
                    source_path.unlink(missing_ok=True)
            if proc.returncode == 0:
                epa_text = output_path.read_text(encoding="utf-8") if output_path.exists() else ""
                epa_map_text = map_path.read_text(encoding="utf-8") if map_path.exists() else ""
                compiler_output = "\n".join(
                    part.strip() for part in (proc.stdout, proc.stderr) if part and part.strip()
                ).strip()
                epa_block_map: dict = {}  # {(block_type, block_id): [{"offset":..., "epa_line":..., "e_line":..., "epa_col":...}, ...]}
                if map_path.exists():
                    cur_block = None
                    for raw in map_path.read_text(encoding="utf-8").splitlines():
                        raw = raw.strip()
                        if not raw:
                            continue
                        if raw.startswith("B "):
                            parts = raw.split()
                            if len(parts) == 3:
                                try:
                                    cur_block = (int(parts[1]), int(parts[2]))
                                    epa_block_map[cur_block] = []
                                except ValueError:
                                    cur_block = None
                        elif cur_block is not None:
                            parts = raw.split()
                            if len(parts) == 2:
                                try:
                                    epa_block_map[cur_block].append({
                                        "offset": int(parts[0]),
                                        "epa_line": 0,
                                        "e_line": int(parts[1]),
                                        "epa_col": 1,
                                    })
                                except ValueError:
                                    pass
                            elif len(parts) == 3:
                                try:
                                    epa_block_map[cur_block].append({
                                        "offset": int(parts[0]),
                                        "epa_line": int(parts[1]),
                                        "e_line": int(parts[2]),
                                        "epa_col": 1,
                                    })
                                except ValueError:
                                    pass
                            elif len(parts) >= 4:
                                try:
                                    epa_block_map[cur_block].append({
                                        "offset": int(parts[0]),
                                        "epa_line": int(parts[1]),
                                        "e_line": int(parts[2]),
                                        "epa_col": int(parts[3]),
                                    })
                                except ValueError:
                                    pass
                return {
                    "ok": True,
                    "epa_text": epa_text,
                    "epa_map_text": epa_map_text,
                    "epa_block_map": epa_block_map,
                    "diagnostics": [],
                    "message": compiler_output,
                }
            message = (proc.stderr or proc.stdout or "compile failed").strip()
            return {
                "ok": False,
                "epa_text": "",
                "epa_map_text": "",
                "epa_block_map": {},
                "diagnostics": _diagnostic_from_error(message),
                "message": message,
            }

    def _debug_map_cache_path(tab_id: str) -> Path:
        return Path("/tmp/epa-ide-debug-maps") / f"{tab_id}.epamap"

    def _persist_debug_map(tab_id: str, map_text: str) -> str:
        if not tab_id or not map_text:
            return ""
        path = _debug_map_cache_path(tab_id)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(map_text, encoding="utf-8")
        return str(path)

    def _apply_editor_view(client, tab_id: str, set_focus: bool = False):
        state = editor_state.get(tab_id)
        if not state:
            return
        ids = _editor_ids(tab_id)
        view = state.get("view", "e")
        is_epa = view == "epa"
        client.set_visible(ids["source"], not is_epa)
        client.set_visible(ids["epa"], is_epa)
        client.set_visible(ids["debug_tabs"], True)
        client.set_enabled(ids["button_e"], view != "e")
        client.set_enabled(ids["button_epa"], view != "epa")
        client.set_read_only(ids["epa"], True)
        debug_on = state.get("debug", False)
        try:
            client.set_grid_column_exact_size(ids["debug_panel"], 1, 320 if debug_on else 0)
        except Exception:
            pass
        if set_focus:
            _focus_editor_widget(client, tab_id, state)

    def _refresh_debug_controls(client, tab_id: str):
        pass  # source/epa editors are the same widgets, no separate debug view to sync

    def _refresh_debug_sidebars(client, tab_id: str):
        state = editor_state.get(tab_id)
        if not state:
            return
        ids = _editor_ids(tab_id)
        _replace_tree_nodes(client, ids["debug"], state.get("trace_nodes", _build_trace_nodes([], f"{ids['debug']}.root")))
        _replace_tree_nodes(client, ids["debug_ghs"], state.get("ghs_nodes", _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root")))
        _replace_tree_nodes(client, ids["debug_stack"], state.get("stack_nodes", _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root")))
        _replace_tree_nodes(client, ids["debug_local"], state.get("local_nodes", _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root")))
        _replace_tree_nodes(client, ids["debug_dynamic"], state.get("dynamic_nodes", _parse_tree_lines("", "Dynamic Memory", f"{ids['debug_dynamic']}.root")))

    _WORKER_DEF_RE = re.compile(
        r"^\s*worker\s+([A-Za-z_]\w*)\s*\(\s*"  # capture worker name
        r"([A-Za-z_]\w*(?:\|[A-Za-z_]\w*)*)"    # capture type(s), possibly union A|B
        r"\s+[A-Za-z_]",                          # followed by the variable name
        re.MULTILINE,
    )

    def _workers_in_file(path: Path) -> list:
        """Return list of {"name": ..., "types": [...]} for each worker in a file."""
        try:
            src = path.read_text(encoding="utf-8", errors="replace")
        except Exception:
            return []
        result = []
        for m in _WORKER_DEF_RE.finditer(src):
            name = m.group(1)
            types = [t.strip() for t in m.group(2).split("|") if t.strip()]
            result.append({"name": name, "types": types})
        return result

    def _kernels_from_project() -> list:
        """Return list of {"id": kernel_id, "label": kernel_label, "path": str} from epa/*.e files."""
        project_root = app_state.get("project_root", "")
        epa_root = Path(project_root) / "epa" if project_root else None
        if not epa_root or not epa_root.is_dir():
            return []
        entry = epa_root / "entry.e"
        others = sorted(p for p in epa_root.rglob("*.e") if p.is_file() and p != entry)
        paths = ([entry] if entry.is_file() else []) + others
        result = []
        for path in paths:
            rel = path.relative_to(epa_root)
            kernel_id = ".".join(rel.with_suffix("").parts)
            kernel_label = str(rel.with_suffix(""))
            result.append({"id": kernel_id, "label": kernel_label, "path": str(path)})
        return result

    def _ingress_types_from_project(kernel_id: str = "", worker_name: str = "") -> list:
        """Scan epa/**/*.e and *.em files for worker parameter types, optionally filtered."""
        project_root = app_state.get("project_root", "")
        epa_root = Path(project_root) / "epa" if project_root else None
        seen: set = set()
        type_names: list = []

        def _collect_src(src: str, name_filter: str = ""):
            for m in _WORKER_DEF_RE.finditer(src):
                if name_filter and m.group(1) != name_filter:
                    continue
                for t in m.group(2).split("|"):
                    t = t.strip()
                    if t and t not in seen:
                        seen.add(t)
                        type_names.append(t)

        if kernel_id and epa_root:
            rel = kernel_id.replace(".", "/") + ".e"
            kpath = epa_root / rel
            if kpath.is_file():
                _collect_src(kpath.read_text(encoding="utf-8", errors="replace"), worker_name)
        elif epa_root and epa_root.is_dir():
            for path in sorted(epa_root.rglob("*")):
                if not path.is_file() or path.suffix not in (".e", ".em"):
                    continue
                try:
                    _collect_src(path.read_text(encoding="utf-8", errors="replace"))
                except Exception:
                    pass
            for state in editor_state.values():
                _collect_src(state.get("source_text", ""))

        return [{"id": n, "label": n} for n in type_names]

    KERNEL_WORKER_ID = "__epa_kernel_wid_0__"

    def _apply_combo_items(client, target: str, items: list, selected_id: str = ""):
        try:
            client.call("ui.setSectionJson", {
                "target": target,
                "section": "items",
                "value": items,
            })
            valid_ids = [str(it.get("id", "")) for it in items if isinstance(it, dict)]
            sel = str(selected_id or "")
            if not sel and valid_ids:
                sel = valid_ids[0]
            if sel and sel in valid_ids:
                client.set_text(target, sel)
        except Exception:
            pass

    def _kernel_worker_entries_for_kernel(kernel_tab_id: str, snapshot: dict | None = None) -> list[dict]:
        workers = app_state.get(f"debug_kernel_workers_{kernel_tab_id}", []) or []
        snapshot = snapshot or app_state.get("debug_kernel_snapshot_state", {}).get(kernel_tab_id) or {}
        snapshot_workers = {
            int(w.get("wid", -1)): w for w in (snapshot.get("workers", []) or [])
        }

        def _state_tags(state: dict) -> list[str]:
            tags: list[str] = []
            if state and state.get("debug_enabled") is False:
                tags.append("fast")
            if int(state.get("inq_count", 0) or 0) > 0:
                tags.append(f"q={int(state.get('inq_count', 0) or 0)}")
            if int(state.get("owned_ghs_count", 0) or 0) > 0:
                tags.append(f"g={int(state.get('owned_ghs_count', 0) or 0)}")
            if state.get("has_current_ghs"):
                tags.append("GHS")
            if state.get("faulted"):
                tags.append("FAULT")
            elif state.get("at_running"):
                tags.append("run")
            elif state.get("waiting_for_data"):
                tags.append("wait")
            return tags

        entries: list[dict] = []
        for idx, worker in enumerate(workers, start=1):
            label = worker.get("name", f"worker_{idx}")
            state = dict(snapshot_workers.get(idx) or {})
            state.setdefault(
                "debug_enabled",
                app_state.setdefault("debug_worker_debug_state", {}).get(_worker_debug_cache_key(kernel_tab_id, idx), True),
            )
            tags = _state_tags(state)
            if tags:
                label += " [" + " ".join(tags) + "]"
            entries.append({"id": worker.get("name", label), "label": label, "wid": idx})
        kernel_label = "kernel (wid=0)"
        kernel_state = dict(snapshot_workers.get(0) or {})
        kernel_state.setdefault(
            "debug_enabled",
            app_state.setdefault("debug_worker_debug_state", {}).get(_worker_debug_cache_key(kernel_tab_id, 0), True),
        )
        kernel_tags = _state_tags(kernel_state)
        if kernel_tags:
            kernel_label += " [" + " ".join(kernel_tags) + "]"
        entries.append({"id": KERNEL_WORKER_ID, "label": kernel_label, "wid": 0})
        return entries

    def _worker_combo_items_for_kernel(kernel_tab_id: str, snapshot: dict | None = None) -> list[dict]:
        entries = _kernel_worker_entries_for_kernel(kernel_tab_id, snapshot)
        items: list[dict] = []
        for entry in entries:
            items.append({"id": entry.get("id", ""), "label": entry.get("label", "")})
        return items

    def _refresh_kernel_worker_combo(client, kernel_tab_id: str, snapshot: dict | None = None):
        if not client or not kernel_tab_id:
            return
        entries = _kernel_worker_entries_for_kernel(kernel_tab_id, snapshot)
        items = [{"id": entry.get("id", ""), "label": entry.get("label", "")} for entry in entries]
        selected_id = str(app_state.get(f"debug_kernel_worker_{kernel_tab_id}", "") or "")
        valid_ids = [str(it.get("id", "")) for it in items if isinstance(it, dict)]
        if valid_ids and selected_id not in valid_ids:
            selected_id = valid_ids[0]
            app_state[f"debug_kernel_worker_{kernel_tab_id}"] = selected_id
        selected_wid = None
        for entry in entries:
            if str(entry.get("id", "")) == selected_id:
                try:
                    selected_wid = int(entry.get("wid", 0) or 0)
                except Exception:
                    selected_wid = 0
                break
        if selected_wid is not None:
            app_state[f"debug_kernel_worker_wid_{kernel_tab_id}"] = selected_wid
        _apply_combo_items(
            client,
            f"nav.debug.kernel.{kernel_tab_id}.worker",
            items,
            selected_id,
        )

    def _kernel_worker_combo_selected(kernel_tab_id: str, payload: dict, snapshot: dict | None = None) -> tuple[str, int | None]:
        entries = _kernel_worker_entries_for_kernel(kernel_tab_id, snapshot)
        worker_id = str(payload.get("action") or payload.get("id") or "")
        if worker_id:
            worker_id = KERNEL_WORKER_ID if worker_id == "kernel (wid=0)" else worker_id
            for entry in entries:
                if str(entry.get("id", "")) == worker_id:
                    try:
                        return worker_id, int(entry.get("wid", 0) or 0)
                    except Exception:
                        return worker_id, 0
            return worker_id, None
        return "", None

    def _apply_ingress_types_combo(client, items):
        _apply_combo_items(client, "nav.debug.ingress_type", items)
        cur = app_state.get("debug_ingress_type", "")
        valid_ids = [it["id"] for it in items]
        if items and (not cur or cur not in valid_ids):
            first_type = items[0]["id"]
            app_state["debug_ingress_type"] = first_type
            _refresh_ingress_profiles_list(client, first_type)

    def _refresh_debug_panel(client):
        """Rebuild nav.debug.kernels, kernel combo, and ingress type combo."""
        kernels = _kernels_from_project()

        # Kernel list rows
        children = []
        if kernels:
            list_ui = UiDocumentBuilder()
            for k in kernels:
                _build_kernel_row_widgets(list_ui, k["id"], k["label"])
            for k in kernels:
                child_dict = list_ui.widget_dict(f"nav.debug.kernel.{k['id']}")
                child_dict["entry"] = {"height": 52}
                children.append(child_dict)

        try:
            client.call("ui.replaceChildren", {
                "target": "nav.debug.kernels",
                "document": json.dumps({"children": children}, separators=(",", ":"), ensure_ascii=False),
            })
        except Exception:
            pass

        # Populate per-kernel worker combos
        project_root = app_state.get("project_root", "")
        epa_root = Path(project_root) / "epa" if project_root else None
        for k in kernels:
            if not epa_root:
                break
            kpath = epa_root / (k["id"].replace(".", "/") + ".e")
            workers = _workers_in_file(kpath)
            app_state[f"debug_kernel_workers_{k['id']}"] = workers
            try:
                _refresh_kernel_worker_combo(client, k["id"])
                _apply_cached_kernel_indicator_state(client, k["id"])
                _apply_cached_kernel_queue_state(client, k["id"])
                _set_kernel_worker_debug_checkbox(client, k["id"])
            except Exception:
                pass

        # Kernel combo
        kernel_items = [{"id": k["id"], "label": k["label"]} for k in kernels]
        _apply_combo_items(client, "nav.debug.ingress_kernel", kernel_items)

        # Worker combo: use currently selected kernel, default to first kernel
        sel_kernel = app_state.get("debug_ingress_kernel", "")
        if not sel_kernel and kernels:
            sel_kernel = kernels[0]["id"]
            app_state["debug_ingress_kernel"] = sel_kernel
        _refresh_ingress_worker_combo(client, sel_kernel)

        # Type combo: serve cache immediately, refresh in background
        cached = app_state.get("debug_ingress_types_cache")
        if cached is not None:
            _apply_ingress_types_combo(client, cached)

        def _bg_refresh_types():
            sel_k = app_state.get("debug_ingress_kernel", "")
            sel_w = app_state.get("debug_ingress_worker", "")
            items = _ingress_types_from_project(sel_k, sel_w)
            app_state["debug_ingress_types_cache"] = items
            c = client_ref.get("client")
            if c:
                _apply_ingress_types_combo(c, items)

        import threading
        threading.Thread(target=_bg_refresh_types, daemon=True).start()

    def _refresh_ingress_worker_combo(client, kernel_id: str):
        """Populate the worker combo for the given kernel_id."""
        workers = []
        if kernel_id:
            project_root = app_state.get("project_root", "")
            epa_root = Path(project_root) / "epa" if project_root else None
            if epa_root:
                rel = kernel_id.replace(".", "/") + ".e"
                kpath = epa_root / rel
                workers = _workers_in_file(kpath)
        items = [{"id": w["name"], "label": w["name"]} for w in workers]
        _apply_combo_items(client, "nav.debug.ingress_worker", items)

    _TYPE_DEF_RE = re.compile(
        r"\btype\s+([A-Za-z_]\w*)\s*\(([^)]*)\)",
        re.DOTALL,
    )
    _FIELD_RE = re.compile(r"\b([A-Za-z_]\w*(?:\[\])?)\s+([A-Za-z_]\w*)\s*(?:,|$)")

    def _parse_type_defs() -> dict:
        """Scan all .e/.em files in epa/ and open editor buffers, returning {TypeName: [{type,name}, ...]}."""
        project_root = app_state.get("project_root", "")
        epa_root = Path(project_root) / "epa" if project_root else None
        result: dict = {}

        def _collect_src(src: str):
            for m in _TYPE_DEF_RE.finditer(src):
                type_name = m.group(1)
                params_str = m.group(2)
                fields = []
                for fm in _FIELD_RE.finditer(params_str):
                    fields.append({"type": fm.group(1), "name": fm.group(2)})
                if type_name not in result:
                    result[type_name] = fields

        if epa_root and epa_root.is_dir():
            for path in sorted(epa_root.rglob("*")):
                if not path.is_file() or path.suffix not in (".e", ".em"):
                    continue
                try:
                    _collect_src(path.read_text(encoding="utf-8", errors="replace"))
                except Exception:
                    continue

        for state in editor_state.values():
            src = state.get("source_text", "")
            if src:
                _collect_src(src)
        return result

    def _type_field_names(type_defs: dict, type_name: str) -> list[str]:
        return [str(f.get("name", "")) for f in (type_defs.get(type_name, []) or []) if str(f.get("name", ""))]

    def _encode_profile_byte_field(field_name: str, spec: object) -> tuple[bytes, dict]:
        import struct as _struct

        if isinstance(spec, str):
            text = spec.strip()
            if text.startswith("hex:"):
                return bytes.fromhex(text[4:]), {}
            return text.encode("utf-8"), {}

        if not isinstance(spec, dict):
            return b"", {}

        fmt = str(spec.get("format", "") or "")
        if fmt == "raw_hex":
            return bytes.fromhex(str(spec.get("hex", "") or "")), {}

        if fmt == "flat_v1":
            items = spec.get("items", []) or []
            computed = dict(spec.get("computed_fields", {}) or {})
            blob = bytearray()
            for item in items:
                if not isinstance(item, dict):
                    continue
                item_type = str(item.get("type", "") or "")
                if item_type == "i32":
                    value = int(item.get("value", 0) or 0)
                    blob.extend(_struct.pack("<I", value & 0xFFFFFFFF))
                    continue
                if item_type == "bytes_utf8":
                    text = str(item.get("value", "") or "")
                    data = text.encode("utf-8")
                    if bool(item.get("length_prefix", False)):
                        blob.extend(_struct.pack("<I", len(data) & 0xFFFFFFFF))
                    blob.extend(data)
                    if bool(item.get("pad4", False)):
                        padded_size = (len(data) + 3) & ~3
                        if padded_size > len(data):
                            blob.extend(b"\x00" * (padded_size - len(data)))
                    continue
                if item_type == "raw_hex":
                    blob.extend(bytes.fromhex(str(item.get("hex", "") or "")))
                    continue
            if f"{field_name}_size" not in computed:
                computed[f"{field_name}_size"] = len(blob)
            if field_name == "payload" and "payload_size" not in computed:
                computed["payload_size"] = len(blob)
            return bytes(blob), computed

        if fmt == "boot_block_devices_v1":
            devices = spec.get("devices", []) or []
            blob = bytearray()
            blob.extend(_struct.pack("<I", 1))
            blob.extend(_struct.pack("<I", len(devices)))
            for device in devices:
                drive_id = int(device.get("drive_id", 0) or 0)
                mount_path = str(device.get("mount_path", "") or "")
                mount_bytes = mount_path.encode("utf-8")
                padded_size = (len(mount_bytes) + 3) & ~3
                blob.extend(_struct.pack("<I", drive_id & 0xFFFFFFFF))
                blob.extend(_struct.pack("<I", len(mount_bytes) & 0xFFFFFFFF))
                blob.extend(mount_bytes)
                if padded_size > len(mount_bytes):
                    blob.extend(b"\x00" * (padded_size - len(mount_bytes)))
            return bytes(blob), {
                "version": 1,
                "device_count": len(devices),
                f"{field_name}_size": len(blob),
                "payload_size": len(blob) if field_name == "payload" else len(blob),
            }

        return b"", {}

    def _profiles_dir(type_name: str) -> Path | None:
        project_root = app_state.get("project_root", "")
        if not project_root:
            return None
        return Path(project_root) / "epa" / "profiles" / type_name

    def _profiles_for_type(type_name: str) -> list:
        """Return list of {"id": name, "label": name} for saved profiles of type_name."""
        d = _profiles_dir(type_name)
        if not d or not d.is_dir():
            return []
        items = []
        for p in sorted(d.glob("*.json")):
            name = p.stem
            items.append({"id": name, "label": name})
        return items

    def _save_ingress_profile(type_name: str, profile_name: str, field_values: dict):
        d = _profiles_dir(type_name)
        if d is None:
            return
        d.mkdir(parents=True, exist_ok=True)
        out = {"type": type_name, "name": profile_name, "fields": field_values}
        (d / f"{profile_name}.json").write_text(
            json.dumps(out, indent=2), encoding="utf-8"
        )

    _IND_OFF = "off"
    _IND_GREEN = "green"
    _IND_RED = "red"
    _IND_ORANGE = "orange"

    _IND_COLORS = {
        _IND_OFF: "#666666",
        _IND_GREEN: "#2da44e",
        _IND_RED: "#d73a49",
        _IND_ORANGE: "#e36209",
    }

    _EPA_FAULT_RE = re.compile(
        r"\[EPA-FAULT\]\s+kernel=(entry|func)\s+wid=(\d+)\s+"
        r"(entry|func)\[(\d+)\]\s+pc=(\d+)\s+op=([A-Z0-9_]+)",
        re.IGNORECASE,
    )

    def _set_kernel_dot(client, kernel_tab_id: str, dot_name: str, state: str):
        if not client:
            return
        color = _IND_COLORS.get(state, _IND_COLORS[_IND_OFF])
        try:
            client.call(
                "ui.setForegroundColor",
                {
                    "target": f"nav.debug.kernel.{kernel_tab_id}.{dot_name}",
                    "color": color,
                },
            )
        except Exception:
            pass

    def _set_kernel_load_indicator(client, kernel_tab_id: str, symbol: str):
        state = app_state.setdefault("debug_kernel_indicator_state", {}).setdefault(kernel_tab_id, {})
        state["load"] = symbol
        if client:
            _set_kernel_dot(client, kernel_tab_id, "load_ind", symbol)

    def _set_kernel_run_indicator(client, kernel_tab_id: str, symbol: str):
        state = app_state.setdefault("debug_kernel_indicator_state", {}).setdefault(kernel_tab_id, {})
        state["run"] = symbol
        if client:
            _set_kernel_dot(client, kernel_tab_id, "run_ind", symbol)

    def _apply_cached_kernel_indicator_state(client, kernel_tab_id: str):
        if not client or not kernel_tab_id:
            return
        state = app_state.get("debug_kernel_indicator_state", {}).get(kernel_tab_id, {})
        _set_kernel_dot(client, kernel_tab_id, "load_ind", state.get("load", _IND_OFF))
        _set_kernel_dot(client, kernel_tab_id, "run_ind", state.get("run", _IND_OFF))

    def _kernel_tab_id_from_bundle(bundle_path: str) -> str:
        if not bundle_path:
            return ""
        try:
            p = Path(bundle_path)
            epa_build = Path(app_state.get("project_root", "")) / "build" / "epa"
            rel = p.relative_to(epa_build).with_suffix("")
            return ".".join(rel.parts)
        except Exception:
            return ""

    def _bundle_path_for_kernel_id(kernel_id: str) -> str:
        project_root = app_state.get("project_root", "")
        if not project_root or not kernel_id:
            return ""
        root = Path(project_root)
        candidates = [
            root / "build" / "epa" / (kernel_id.replace(".", "/") + ".epabin"),
            root / "build" / "epa" / (kernel_id.replace(".", "/") + ".epa.bin"),
            root / "build" / "epa.bin",
        ]
        for candidate in candidates:
            if candidate.is_file():
                return str(candidate)
        return str(candidates[0])

    def _kernel_tab_id_for_runtime_kernel(runtime_kernel_id: str) -> str:
        runtime_kernel_id = str(runtime_kernel_id or "").strip()
        if not runtime_kernel_id:
            return ""
        kernel_ids = [str(k.get("id", "") or "") for k in _kernels_from_project()]
        if runtime_kernel_id in kernel_ids:
            return runtime_kernel_id
        for kernel_id in kernel_ids:
            if runtime_kernel_id.endswith("." + kernel_id):
                return kernel_id
        tail = runtime_kernel_id.rsplit(".", 1)[-1]
        if tail in kernel_ids:
            return tail
        return ""

    def _bump_kernel_interconnect_counter(client, runtime_kernel_id: str, direction: str):
        kernel_tab_id = _kernel_tab_id_for_runtime_kernel(runtime_kernel_id)
        if not kernel_tab_id:
            return
        queue_state = app_state.setdefault("debug_kernel_queue_state", {}).setdefault(
            kernel_tab_id,
            {
                "total_packets": 0,
                "worker_packets": {},
                "lifetime_ingress": 0,
                "lifetime_egress": 0,
                "prev_worker_inq": {},
            },
        )
        if direction == "ingress":
            queue_state["lifetime_ingress"] = int(queue_state.get("lifetime_ingress", 0) or 0) + 1
        elif direction == "egress":
            queue_state["lifetime_egress"] = int(queue_state.get("lifetime_egress", 0) or 0) + 1
        if client:
            _apply_cached_kernel_queue_state(client, kernel_tab_id)

    def _selected_worker_wid_for_kernel(kernel_tab_id: str) -> int | None:
        sel_worker_name = app_state.get(f"debug_kernel_worker_{kernel_tab_id}", "")
        if sel_worker_name == KERNEL_WORKER_ID:
            return 0
        stored_wid = app_state.get(f"debug_kernel_worker_wid_{kernel_tab_id}")
        if stored_wid is not None:
            try:
                return int(stored_wid)
            except Exception:
                pass
        entries = _kernel_worker_entries_for_kernel(kernel_tab_id)
        if not entries:
            return 0
        for entry in entries:
            if str(entry.get("id", "")) == str(sel_worker_name):
                try:
                    return int(entry.get("wid", 0) or 0)
                except Exception:
                    return 0
        return 1

    def _worker_debug_cache_key(kernel_tab_id: str, wid: int | None) -> str:
        return f"{kernel_tab_id}:{0 if wid is None else int(wid)}"

    def _selected_worker_debug_enabled(kernel_tab_id: str, snapshot: dict | None = None) -> bool:
        wid = _selected_worker_wid_for_kernel(kernel_tab_id)
        if wid is None:
            return True
        snapshot = snapshot or app_state.get("debug_kernel_snapshot_state", {}).get(kernel_tab_id) or {}
        for worker in (snapshot.get("workers", []) or []):
            try:
                if int(worker.get("wid", -1)) == int(wid) and "debug_enabled" in worker:
                    return bool(worker.get("debug_enabled"))
            except Exception:
                continue
        if _epa_dbg_running():
            return True
        cache = app_state.setdefault("debug_worker_debug_state", {})
        return bool(cache.get(_worker_debug_cache_key(kernel_tab_id, wid), True))

    def _kernel_all_workers_debug_enabled(kernel_tab_id: str, snapshot: dict | None = None) -> bool:
        snapshot = snapshot or app_state.get("debug_kernel_snapshot_state", {}).get(kernel_tab_id) or {}
        workers = list(snapshot.get("workers", []) or [])
        if workers:
            any_worker = False
            for worker in workers:
                try:
                    wid = int(worker.get("wid", -1))
                except Exception:
                    continue
                if wid < 0:
                    continue
                any_worker = True
                if "debug_enabled" in worker:
                    if not bool(worker.get("debug_enabled")):
                        return False
                    continue
                cache = app_state.setdefault("debug_worker_debug_state", {})
                if not bool(cache.get(_worker_debug_cache_key(kernel_tab_id, wid), True)):
                    return False
            return any_worker
        worker_wids = _kernel_worker_wids(kernel_tab_id, snapshot)
        if not worker_wids:
            return False
        cache = app_state.setdefault("debug_worker_debug_state", {})
        return all(bool(cache.get(_worker_debug_cache_key(kernel_tab_id, wid), True)) for wid in worker_wids)

    def _refresh_kernel_all_workers_button(client, kernel_tab_id: str, snapshot: dict | None = None):
        if not client or not kernel_tab_id:
            return
        label = "None" if _kernel_all_workers_debug_enabled(kernel_tab_id, snapshot) else "All"
        try:
            client.set_text(f"nav.debug.kernel.{kernel_tab_id}.all_workers", label)
        except Exception:
            pass

    def _set_kernel_worker_debug_checkbox_local(client, kernel_tab_id: str, snapshot: dict | None = None):
        if not client or not kernel_tab_id:
            return
        try:
            client.set_checked(
                f"nav.debug.kernel.{kernel_tab_id}.debug",
                _selected_worker_debug_enabled(kernel_tab_id, snapshot),
            )
            _refresh_kernel_all_workers_button(client, kernel_tab_id, snapshot)
        except Exception:
            pass

    def _set_kernel_worker_debug_checkbox(client, kernel_tab_id: str, snapshot: dict | None = None):
        if not client or not kernel_tab_id:
            return
        if _epa_dbg_running():
            _refresh_selected_worker_debug_from_backend(client, kernel_tab_id, snapshot)
            return
        _set_kernel_worker_debug_checkbox_local(client, kernel_tab_id, snapshot)

    def _refresh_selected_worker_debug_from_backend(client, kernel_tab_id: str, snapshot: dict | None = None):
        if not client or not kernel_tab_id:
            return
        dbg_c = _epa_dbg_client()
        if not dbg_c or not _epa_dbg_running():
            _set_kernel_worker_debug_checkbox_local(client, kernel_tab_id, snapshot)
            return
        wid = _selected_worker_wid_for_kernel(kernel_tab_id)
        if wid is None:
            wid = 0
        worker_name = str(app_state.get(f"debug_kernel_worker_{kernel_tab_id}", "") or "")
        try:
            resp = dbg_c.get_worker_debug(wid, path_id=kernel_tab_id)
            enabled = bool((resp or {}).get("debug_enabled", True))
            _epa_dbg_log(
                f"[debug-state-read] kernel={kernel_tab_id} worker={worker_name or '<unset>'} "
                f"wid={wid} -> {enabled} resp={resp}"
            )
            app_state.setdefault("debug_worker_debug_state", {})[
                _worker_debug_cache_key(kernel_tab_id, wid)
            ] = enabled
            snapshot = snapshot or app_state.get("debug_kernel_snapshot_state", {}).get(kernel_tab_id)
            if snapshot:
                for worker in (snapshot.get("workers", []) or []):
                    try:
                        if int(worker.get("wid", -1)) == int(wid):
                            worker["debug_enabled"] = enabled
                            break
                    except Exception:
                        continue
            client.set_checked(f"nav.debug.kernel.{kernel_tab_id}.debug", enabled)
            _refresh_kernel_all_workers_button(client, kernel_tab_id, snapshot)
        except Exception as exc:
            _epa_dbg_log(f"[debug-state-read-error] {kernel_tab_id} wid={wid}: {exc}")
            _set_kernel_worker_debug_checkbox_local(client, kernel_tab_id, snapshot)

    def _sync_cached_worker_debug_states(dbg_c):
        if not dbg_c:
            return
        for key, enabled in list((app_state.get("debug_worker_debug_state") or {}).items()):
            try:
                kernel_tab_id, wid_text = str(key).rsplit(":", 1)
                dbg_c.set_worker_debug(int(wid_text), bool(enabled), path_id=kernel_tab_id)
            except Exception:
                pass

    def _kernel_worker_wids(kernel_tab_id: str, snapshot: dict | None = None) -> list[int]:
        snapshot = snapshot or app_state.get("debug_kernel_snapshot_state", {}).get(kernel_tab_id) or {}
        worker_wids: list[int] = []
        for worker in (snapshot.get("workers", []) or []):
            try:
                wid = int(worker.get("wid", -1))
            except Exception:
                continue
            if wid >= 0 and wid not in worker_wids:
                worker_wids.append(wid)
        if not worker_wids:
            workers = app_state.get(f"debug_kernel_workers_{kernel_tab_id}", []) or []
            worker_wids = list(range(1, len(workers) + 1))
            worker_wids.append(0)
        return worker_wids

    def _set_kernel_queue_badge(client, kernel_tab_id: str, total_inq: int, sel_inq: int):
        try:
            qs = app_state.get("debug_kernel_queue_state", {}).get(kernel_tab_id, {})
            lt_in  = int(qs.get("lifetime_ingress", 0))
            lt_out = int(qs.get("lifetime_egress",  0))
            client.set_text(f"nav.debug.kernel.{kernel_tab_id}.queue",
                            f"{total_inq} / {sel_inq} ({lt_in}, {lt_out})")
        except Exception:
            pass

    def _clear_kernel_queue_state(client, kernel_tab_id: str):
        if not kernel_tab_id:
            return
        queue_state = app_state.setdefault("debug_kernel_queue_state", {})
        queue_state[kernel_tab_id] = {"total_packets": 0, "worker_packets": {},
                                       "lifetime_ingress": 0, "lifetime_egress": 0,
                                       "prev_worker_inq": {}}
        if client:
            _set_kernel_queue_badge(client, kernel_tab_id, 0, 0)

    def _apply_cached_kernel_queue_state(client, kernel_tab_id: str):
        if not client or not kernel_tab_id:
            return
        queue_state = app_state.get("debug_kernel_queue_state", {}).get(kernel_tab_id, {})
        total_packets = int(queue_state.get("total_packets", queue_state.get("total_inq", 0)) or 0)
        worker_packets = queue_state.get("worker_packets", queue_state.get("worker_inq", {})) or {}
        sel_wid = _selected_worker_wid_for_kernel(kernel_tab_id)
        sel_packets = int(worker_packets.get(sel_wid, 0) or 0) if sel_wid is not None else 0
        _set_kernel_queue_badge(client, kernel_tab_id, total_packets, sel_packets)

    def _update_kernel_queue_state_from_snapshot(client, kernel_tab_id: str, snapshot: dict):
        if not kernel_tab_id:
            return
        app_state.setdefault("debug_kernel_snapshot_state", {})[kernel_tab_id] = snapshot
        debug_state_cache = app_state.setdefault("debug_worker_debug_state", {})
        workers = snapshot.get("workers", [])
        kernel = snapshot.get("kernel", {}) or {}
        total_queued = sum(int(w.get("inq_count", 0) or 0) for w in workers)
        total_packets = total_queued + int(kernel.get("ghs_live_count", 0) or 0)
        worker_packets = {}
        prev_state = app_state.get("debug_kernel_queue_state", {}).get(kernel_tab_id, {})
        prev_worker_inq = prev_state.get("prev_worker_inq", {})
        lifetime_ingress = int(prev_state.get("lifetime_ingress", 0))
        lifetime_egress  = int(prev_state.get("lifetime_egress",  0))
        new_worker_inq = {}
        for w in workers:
            if "debug_enabled" in w:
                try:
                    debug_state_cache[_worker_debug_cache_key(kernel_tab_id, int(w.get("wid", 0) or 0))] = bool(w.get("debug_enabled"))
                except Exception:
                    pass
            wid = w.get("wid")
            cur_inq = int(w.get("inq_count", 0) or 0)
            worker_packets[wid] = cur_inq + int(w.get("owned_ghs_count", 0) or 0)
            if wid in prev_worker_inq:
                delta = cur_inq - int(prev_worker_inq[wid])
                if delta > 0:
                    lifetime_ingress += delta
            new_worker_inq[wid] = cur_inq
        app_state.setdefault("debug_kernel_queue_state", {})[kernel_tab_id] = {
            "total_packets": total_packets,
            "worker_packets": worker_packets,
            "lifetime_ingress": lifetime_ingress,
            "lifetime_egress":  lifetime_egress,
            "prev_worker_inq":  new_worker_inq,
        }
        if client:
            sel_wid = _selected_worker_wid_for_kernel(kernel_tab_id)
            sel_packets = worker_packets.get(sel_wid, 0) if sel_wid is not None else 0
            _set_kernel_queue_badge(client, kernel_tab_id, total_packets, sel_packets)
            _refresh_kernel_worker_combo(client, kernel_tab_id, snapshot)
            _refresh_kernel_all_workers_button(client, kernel_tab_id, snapshot)
            _refresh_selected_worker_debug_from_backend(client, kernel_tab_id, snapshot)

    def _update_kernel_indicator_from_snapshot(client, kernel_tab_id: str, snapshot: dict):
        """Set health and blocked indicators from the selected worker snapshot."""
        if not kernel_tab_id:
            return
        sel_wid = _selected_worker_wid_for_kernel(kernel_tab_id)

        health_color = _IND_OFF
        blocked_color = _IND_OFF
        if sel_wid is not None:
            for w in snapshot.get("workers", []):
                if w.get("wid") == sel_wid:
                    if w.get("faulted"):
                        health_color = _IND_RED
                    elif w.get("halted"):
                        health_color = _IND_OFF
                    else:
                        health_color = _IND_GREEN
                    blocked_color = _IND_RED if (w.get("blocked") or w.get("waiting_for_data")) else _IND_GREEN
                    break
        _set_kernel_load_indicator(client, kernel_tab_id, health_color)
        _set_kernel_run_indicator(client, kernel_tab_id, blocked_color)

    def _selected_worker_from_snapshot(kernel_tab_id: str, snapshot: dict) -> dict | None:
        sel_wid = _selected_worker_wid_for_kernel(kernel_tab_id)
        if sel_wid is None:
            return None
        for worker in snapshot.get("workers", []):
            if int(worker.get("wid", -1)) == int(sel_wid):
                return worker
        return None

    def _refresh_runtime_debug_sidebars(client, kernel_tab_id: str, snapshot: dict | None = None):
        if not client or not kernel_tab_id:
            return
        stid = _editor_tab_id_for_kernel(kernel_tab_id)
        st = editor_state.get(stid)
        if not st:
            return
        dbg_c = _epa_dbg_client()
        if not dbg_c:
            return
        snapshot = snapshot or app_state.get("debug_kernel_snapshot_state", {}).get(kernel_tab_id) or {}
        selected_worker = _selected_worker_from_snapshot(kernel_tab_id, snapshot)
        sel_wid = _selected_worker_wid_for_kernel(kernel_tab_id)
        inspect = None
        if sel_wid is not None:
            try:
                inspect = dbg_c.inspect_worker(
                    wid=sel_wid,
                    path_id=kernel_tab_id,
                    stack_words=48,
                    arena_bytes=160,
                    ghs_bytes=160,
                )
            except Exception as exc:
                _epa_dbg_log(f"[inspect-error] {kernel_tab_id} wid={sel_wid}: {exc}")
        ids = _editor_ids(stid)
        st["trace_nodes"] = _build_runtime_trace_nodes(selected_worker, inspect, f"{ids['debug']}.root")
        st["ghs_nodes"] = _build_runtime_ghs_nodes(inspect, f"{ids['debug_ghs']}.root")
        st["stack_nodes"] = _build_runtime_stack_nodes(inspect, f"{ids['debug_stack']}.root")
        st["local_nodes"] = _build_runtime_local_nodes(inspect, f"{ids['debug_local']}.root")
        st["dynamic_nodes"] = _build_runtime_dynamic_nodes(inspect, f"{ids['debug_dynamic']}.root")
        _refresh_debug_sidebars(client, stid)

    def _active_debug_kernel_id() -> str:
        kernel_tab_id = str(app_state.get("debug_active_kernel", "") or "")
        if kernel_tab_id:
            return kernel_tab_id
        return _kernel_tab_id_from_bundle(app_state.get("debug_kernel_loaded", ""))

    def _update_queue_counters(client, snapshot: dict, kernel_tab_id: str | None = None):
        """Update kernel row queue badge with 'total / selected-worker' inq counts."""
        kernel_tab_id = kernel_tab_id or _active_debug_kernel_id()
        if not kernel_tab_id:
            return
        _update_kernel_queue_state_from_snapshot(client, kernel_tab_id, snapshot)
        _update_eip_marker(client, kernel_tab_id, snapshot)

    _debug_refresh_lock = threading.Lock()
    _debug_refresh_state = {
        "running": False,
        "request": None,
        "seq": 0,
    }

    def _apply_kernel_debug_snapshot(client, kernel_tab_id: str, snapshot: dict, refresh_sidebars: bool = False):
        if not client or not kernel_tab_id or not isinstance(snapshot, dict) or not snapshot:
            return
        _update_queue_counters(client, snapshot, kernel_tab_id)
        _update_kernel_indicator_from_snapshot(client, kernel_tab_id, snapshot)
        if refresh_sidebars:
            _refresh_runtime_debug_sidebars(client, kernel_tab_id, snapshot)

    def _schedule_all_kernel_debug_state_refresh(client, dbg_c, active_kernel_tab_id: str = "", active_snapshot: dict | None = None):
        """Coalesce all-kernel debug state refreshes onto one background worker."""
        if not client or not dbg_c:
            return
        if active_kernel_tab_id and active_snapshot:
            _apply_kernel_debug_snapshot(client, active_kernel_tab_id, active_snapshot, refresh_sidebars=True)

        with _debug_refresh_lock:
            _debug_refresh_state["seq"] = int(_debug_refresh_state.get("seq", 0)) + 1
            _debug_refresh_state["request"] = {
                "client": client,
                "dbg_c": dbg_c,
                "active_kernel_tab_id": active_kernel_tab_id,
                "active_snapshot": active_snapshot,
                "seq": _debug_refresh_state["seq"],
            }
            if _debug_refresh_state.get("running"):
                return
            _debug_refresh_state["running"] = True

        def _worker():
            while True:
                with _debug_refresh_lock:
                    request = _debug_refresh_state.get("request")
                    _debug_refresh_state["request"] = None
                if not request:
                    with _debug_refresh_lock:
                        if _debug_refresh_state.get("request") is None:
                            _debug_refresh_state["running"] = False
                            return
                    continue

                req_client = request.get("client")
                req_dbg_c = request.get("dbg_c")
                req_active = str(request.get("active_kernel_tab_id", "") or "")
                req_active_snapshot = request.get("active_snapshot")
                req_seq = int(request.get("seq", 0) or 0)
                for k in _kernels_from_project():
                    ktid = str(k.get("id", "") or "")
                    if not ktid:
                        continue
                    if ktid == req_active and req_active_snapshot:
                        continue
                    with _debug_refresh_lock:
                        if req_seq != int(_debug_refresh_state.get("seq", 0) or 0):
                            break
                    try:
                        snap = req_dbg_c.snapshot(0, path_id=ktid)
                    except Exception as exc:
                        _epa_dbg_log(f"[snapshot-error] {ktid}: {exc}")
                        continue
                    with _debug_refresh_lock:
                        if req_seq != int(_debug_refresh_state.get("seq", 0) or 0):
                            break
                    _apply_kernel_debug_snapshot(req_client, ktid, snap, refresh_sidebars=False)

                with _debug_refresh_lock:
                    if _debug_refresh_state.get("request") is None:
                        _debug_refresh_state["running"] = False
                        return

        threading.Thread(target=_worker, daemon=True).start()

    def _editor_tab_id_for_kernel(kernel_id: str) -> str:
        project_root = app_state.get("project_root", "")
        if not project_root or not kernel_id:
            return ""
        file_path = str(Path(project_root) / "epa" / (kernel_id.replace(".", "/") + ".e"))
        return _tab_id_for_path(file_path)

    _KERNEL_DEF_RE = re.compile(r"^\s*kernel\s*\(", re.MULTILINE)

    def _block_decl_lines(source_text: str) -> dict:
        """Return {(0, block_id): 1-based-line} for kernel entry and each worker in source_text.

        block_id 0 = kernel(...), 1 = first worker, 2 = second worker, etc.
        Entries are used as fallback when the block map has no source line for a given offset.
        """
        lines = source_text.splitlines()
        result = {}
        worker_idx = 1
        for lineno, line in enumerate(lines, start=1):
            stripped = line.lstrip()
            if stripped.startswith("kernel") and re.match(r"kernel\s*\(", stripped):
                result[(0, 0)] = lineno
            elif stripped.startswith("worker") and re.match(r"worker\s+[A-Za-z_]", stripped):
                result[(0, worker_idx)] = lineno
                worker_idx += 1
        return result

    def _eip_to_map_entry(epa_block_map: dict, block_type: int, block_id: int, rel_pc: int) -> dict:
        """Return the closest map entry at or before rel_pc within the current block."""
        entries = epa_block_map.get((int(block_type), int(block_id)), [])
        result = {}
        for entry in entries:
            offset = int(entry.get("offset", -1))
            if offset <= rel_pc:
                result = entry
            else:
                break
        if not result and entries:
            result = entries[0]
        return result

    def _resolve_eip_marker(st: dict, block_type: int, block_id: int, rel_pc: int) -> dict:
        """Resolve EPA-first marker info, then translate to E using the .epamap entry."""
        epa_block_map = st.get("epa_block_map", {})
        source_text = st.get("source_text", "")
        decl_lines = _block_decl_lines(source_text) if source_text else {}
        marker = _eip_to_map_entry(epa_block_map, block_type, block_id, rel_pc)
        src_line = int(marker.get("e_line", 0) or 0)
        epa_line = int(marker.get("epa_line", 0) or 0)
        if src_line == 0 and decl_lines:
            src_line = int(decl_lines.get((int(block_type), int(block_id)), 0) or 0)
        return {
            "marker": marker,
            "epa_line": epa_line,
            "src_line": src_line,
            "epa_col": int(marker.get("epa_col", 1) or 1),
        }

    def _editor_widget_tab_and_kind(target: str) -> tuple[str, str]:
        if not target:
            return "", ""
        for tab_id in editor_state.keys():
            ids = _editor_ids(tab_id)
            if target == ids["source"]:
                return tab_id, "source"
            if target == ids["epa"]:
                return tab_id, "epa"
        for entry in tab_list:
            tab_id = entry.get("tab_id", "")
            if not tab_id:
                continue
            ids = _editor_ids(tab_id)
            if target == ids["source"]:
                return tab_id, "source"
        return "", ""

    def _set_editor_breakpoint_line(tab_id: str, kind: str, line: int, enabled: bool):
        state = editor_state.get(tab_id)
        if not state:
            return
        key = "source_breakpoint_lines" if kind == "source" else "epa_breakpoint_lines"
        lines = set(int(v) for v in state.get(key, []) if int(v) >= 0)
        if enabled:
            lines.add(int(line))
        else:
            lines.discard(int(line))
        state[key] = sorted(lines)

    def _breakpoint_locations_for_kernel(kernel_tab_id: str) -> list[dict]:
        stid = _editor_tab_id_for_kernel(kernel_tab_id)
        st = editor_state.get(stid) if stid else None
        if not st:
            return []
        locations: list[dict] = []
        seen: set[tuple[int, int, int]] = set()

        def _add_matching(kind: str, line0: int):
            line1 = int(line0) + 1
            for key, entries in (st.get("epa_block_map", {}) or {}).items():
                try:
                    block_type, block_id = int(key[0]), int(key[1])
                except Exception:
                    continue
                for entry in entries or []:
                    marker_line = int(entry.get("e_line" if kind == "source" else "epa_line", 0) or 0)
                    if marker_line != line1:
                        continue
                    rel_pc = int(entry.get("offset", 0) or 0)
                    loc_key = (block_type, block_id, rel_pc)
                    if loc_key in seen:
                        return
                    seen.add(loc_key)
                    locations.append({"block_type": block_type, "block_id": block_id, "rel_pc": rel_pc})
                    return

        for line in st.get("source_breakpoint_lines", []) or []:
            _add_matching("source", int(line))
        for line in st.get("epa_breakpoint_lines", []) or []:
            _add_matching("epa", int(line))
        return locations

    def _sync_editor_breakpoints_for_run(dbg_c, kernel_id: int, kernel_tab_id: str):
        if not dbg_c or not kernel_tab_id:
            return
        locations = _breakpoint_locations_for_kernel(kernel_tab_id)
        dbg_c.breakpoint_clear_all(kernel_id)
        for loc in locations:
            dbg_c.call("epa.debug.breakpointAdd", {
                "kernel_id": kernel_id,
                "block_type": int(loc["block_type"]),
                "block_id": int(loc["block_id"]),
                "rel_pc": int(loc["rel_pc"]),
            })
        if locations:
            _epa_dbg_log(f"[breakpoints] synced {len(locations)} for {kernel_tab_id}")

    def _format_fault_marker_detail(line: str) -> str:
        """Translate an EPA fault line into current-kernel EPA/E source coordinates when possible."""
        match = _EPA_FAULT_RE.search(line or "")
        if not match:
            return ""
        block_label = match.group(3).lower()
        block_type = 0 if block_label == "entry" else 1
        block_id = int(match.group(4))
        rel_pc = int(match.group(5))
        op = match.group(6)
        kernel_tab_id = _active_debug_kernel_id()
        stid = _editor_tab_id_for_kernel(kernel_tab_id)
        st = editor_state.get(stid) if stid else None
        if not st:
            return f"[fault] {block_label}[{block_id}] +0x{rel_pc:x} op={op}"
        resolved = _resolve_eip_marker(st, block_type, block_id, rel_pc)
        epa_line = int(resolved.get("epa_line", 0) or 0)
        src_line = int(resolved.get("src_line", 0) or 0)
        return (
            f"[fault] {kernel_tab_id or '?'} {block_label}[{block_id}] +0x{rel_pc:x} "
            f"op={op} -> EPA:{epa_line if epa_line > 0 else '?'} "
            f"E:{src_line if src_line > 0 else '?'}"
        )

    def _update_eip_marker(client, kernel_tab_id: str, snapshot: dict):
        """Move the EIP marker in both E and EPA editors for the selected worker."""
        if not client or not kernel_tab_id:
            return
        sel_wid = _selected_worker_wid_for_kernel(kernel_tab_id)
        if sel_wid is None:
            return
        for w in snapshot.get("workers", []):
            if w.get("wid") != sel_wid:
                continue
            eip = w.get("eip", {})
            block_type = eip.get("block_type", 0)
            block_id = eip.get("block_id", 0)
            rel_pc = eip.get("rel_pc", 0)
            stid = _editor_tab_id_for_kernel(kernel_tab_id)
            st = editor_state.get(stid)
            if not st:
                return
            if not st.get("epa_block_map") or not st.get("epa_text", ""):
                _refresh_e_tab(client, stid)
                st = editor_state.get(stid)
                if not st:
                    return
            resolved = _resolve_eip_marker(st, block_type, block_id, rel_pc)
            src_line = resolved["src_line"]
            epa_line = resolved["epa_line"]
            epa_col = int(resolved.get("epa_col", 1) or 1)
            try:
                if src_line > 0:
                    client.set_eip_line(_editor_ids(stid)["source"], max(0, src_line - 1))
                if epa_line > 0:
                    client.fire("ui.setEipPosition", {
                        "target": _editor_ids(stid)["epa"],
                        "line": max(0, epa_line - 1),
                        "column": max(0, epa_col - 1),
                    })
            except Exception as exc:
                _epa_dbg_log(
                    f"[marker-error] {kernel_tab_id} "
                    f"entry[{block_id}] +0x{int(rel_pc):x} "
                    f"-> EPA:{epa_line if epa_line > 0 else '?'}:{epa_col if epa_col > 0 else '?'} "
                    f"E:{src_line if src_line > 0 else '?'}: {exc}"
                )
                return
            _epa_dbg_log(
                f"[marker] {kernel_tab_id} "
                f"{'entry' if int(block_type) == 0 else 'func'}[{block_id}] +0x{int(rel_pc):x} "
                f"-> EPA:{epa_line if epa_line > 0 else '?'}:{epa_col if epa_col > 0 else '?'} "
                f"E:{src_line if src_line > 0 else '?'}"
            )
            return

    def _activate_kernel_editor_tab(client, kernel_tab_id: str):
        project_root = app_state.get("project_root", "")
        if not client or not project_root or not kernel_tab_id:
            return
        file_path = str(Path(project_root) / "epa" / (kernel_tab_id.replace(".", "/") + ".e"))
        if not Path(file_path).is_file():
            return
        _open_file_tab(client, file_path, True)

    def _jump_to_selected_worker_eip(client, kernel_tab_id: str, snapshot: dict | None = None):
        if not client or not kernel_tab_id:
            return
        snapshot = snapshot or app_state.get("debug_kernel_snapshot_state", {}).get(kernel_tab_id)
        if not snapshot:
            try:
                dbg_c = _epa_dbg_client()
                if dbg_c:
                    snapshot = dbg_c.snapshot(0, path_id=kernel_tab_id)
                    app_state.setdefault("debug_kernel_snapshot_state", {})[kernel_tab_id] = snapshot
            except Exception:
                snapshot = None
        if not snapshot:
            return
        _activate_kernel_editor_tab(client, kernel_tab_id)
        _update_eip_marker(client, kernel_tab_id, snapshot)

    def _selected_worker_snapshot(kernel_tab_id: str, snapshot: dict) -> dict | None:
        sel_wid = _selected_worker_wid_for_kernel(kernel_tab_id)
        if sel_wid is None:
            return None
        for worker in snapshot.get("workers", []):
            if worker.get("wid") == sel_wid:
                return worker
        return None

    def _format_step_eip_log(kernel_tab_id: str, snapshot: dict) -> str:
        worker = _selected_worker_snapshot(kernel_tab_id, snapshot)
        if not worker:
            return f"[step] {kernel_tab_id}: no selected worker snapshot"
        eip = worker.get("eip", {}) or {}
        block_type = int(eip.get("block_type", 0) or 0)
        block_id = int(eip.get("block_id", 0) or 0)
        rel_pc = int(eip.get("rel_pc", 0) or 0)
        block_label = "entry" if block_type == 0 else "func" if block_type == 1 else f"type{block_type}"
        marker_suffix = ""
        stid = _editor_tab_id_for_kernel(kernel_tab_id)
        st = editor_state.get(stid) if stid else None
        if st:
            resolved = _resolve_eip_marker(st, block_type, block_id, rel_pc)
            epa_line = int(resolved.get("epa_line", 0) or 0)
            epa_col = int(resolved.get("epa_col", 1) or 1)
            src_line = int(resolved.get("src_line", 0) or 0)
            if epa_line > 0 or src_line > 0:
                marker_suffix = (
                    f" -> EPA:{epa_line if epa_line > 0 else '?'}:{epa_col if epa_col > 0 else '?'} "
                    f"E:{src_line if src_line > 0 else '?'}"
                )
        return (
            f"[step] {kernel_tab_id} "
            f"wid={worker.get('wid', '?')} "
            f"{block_label}[{block_id}] +0x{rel_pc:x}"
            f"{marker_suffix}"
        )

    def _debug_step_mode_for_kernel(kernel_tab_id: str) -> str:
        stid = _editor_tab_id_for_kernel(kernel_tab_id)
        st = editor_state.get(stid) if stid else None
        view = str((st or {}).get("view", "e") or "e").lower()
        return "epa" if view == "epa" else "e"

    def _selected_worker_marker_state(kernel_tab_id: str, snapshot: dict) -> dict:
        worker = _selected_worker_snapshot(kernel_tab_id, snapshot)
        if not worker:
            return {}
        eip = worker.get("eip", {}) or {}
        block_type = int(eip.get("block_type", 0) or 0)
        block_id = int(eip.get("block_id", 0) or 0)
        rel_pc = int(eip.get("rel_pc", 0) or 0)
        stid = _editor_tab_id_for_kernel(kernel_tab_id)
        st = editor_state.get(stid) if stid else None
        resolved = _resolve_eip_marker(st, block_type, block_id, rel_pc) if st else {}
        return {
            "wid": int(worker.get("wid", 0) or 0),
            "block_type": block_type,
            "block_id": block_id,
            "rel_pc": rel_pc,
            "epa_line": int(resolved.get("epa_line", 0) or 0),
            "src_line": int(resolved.get("src_line", 0) or 0),
            "waiting_for_data": bool(worker.get("waiting_for_data")),
            "faulted": bool(worker.get("faulted")),
            "halted": bool(worker.get("halted")),
        }

    def _step_boundary_crossed(step_mode: str, before: dict, after: dict) -> bool:
        if not before or not after:
            return True
        if (
            before.get("block_type") != after.get("block_type")
            or before.get("block_id") != after.get("block_id")
        ):
            return True
        if step_mode == "epa":
            before_line = int(before.get("epa_line", 0) or 0)
            after_line = int(after.get("epa_line", 0) or 0)
            if before_line > 0 and after_line > 0:
                return before_line != after_line
        else:
            before_line = int(before.get("src_line", 0) or 0)
            after_line = int(after.get("src_line", 0) or 0)
            if before_line > 0 and after_line > 0:
                return before_line != after_line
        return int(before.get("rel_pc", -1)) != int(after.get("rel_pc", -1))

    def _step_stalled_reason(step_mode: str, before: dict, after: dict) -> str:
        if not after:
            return "no selected worker snapshot"
        if after.get("faulted"):
            return "faulted"
        if after.get("halted"):
            return "halted"
        if after.get("waiting_for_data") and not _step_boundary_crossed(step_mode, before, after):
            return "waiting_for_data"
        return ""

    def _dbg_step_for_active_view(dbg_c, kernel_id: int, kernel_tab_id: str, wid: int, max_ticks: int = 4096) -> tuple[dict, str]:
        stid = _editor_tab_id_for_kernel(kernel_tab_id)
        st = editor_state.get(stid) if stid else None
        map_path = str((st or {}).get("epa_map_path", "") or "")
        step_mode = _debug_step_mode_for_kernel(kernel_tab_id)
        if map_path:
            resp = dbg_c.step_boundary(
                wid=wid,
                map_path=map_path,
                step_mode=step_mode,
                path_id=kernel_tab_id,
                max_ticks=max_ticks,
            )
            snap = resp.get("snapshot", {}) if isinstance(resp, dict) else {}
            stop_reason = str(resp.get("stop_reason", "")) if isinstance(resp, dict) else ""
            return snap, stop_reason
        try:
            resp = dbg_c.step(kernel_id, ticks=1, path_id=kernel_tab_id, wid=wid)
            snap = resp.get("snapshot", {}) if isinstance(resp, dict) else {}
            stop_reason = str(resp.get("stop_reason", "")) if isinstance(resp, dict) else ""
            return snap, stop_reason
        except Exception as exc:
            is_step_complete = (
                getattr(exc, "code", "") == "step_failed"
                and "step complete returning to host" in str(exc)
            )
            if is_step_complete:
                try:
                    return dbg_c.snapshot(kernel_id, path_id=kernel_tab_id), "step"
                except Exception:
                    return {}, "step"
            raise

    def _drain_epa_dbg_events(client, dbg_c, kernel_id: int = 0, kernel_tab_id: str = ""):
        if not client or not dbg_c:
            return
        try:
            payload = dbg_c.events(kernel_id, clear=True)
        except Exception as exc:
            _epa_dbg_log(f"[event-error] {kernel_tab_id or 'kernel'}: {exc}")
            return
        events = payload.get("events", []) if isinstance(payload, dict) else payload
        if not isinstance(events, list):
            return
        for ev in events:
            if not isinstance(ev, dict):
                continue
            kind = str(ev.get("kind", "") or "")
            wid = ev.get("wid")
            message = str(ev.get("message", "") or "").strip()
            block_type = ev.get("block_type")
            block_id = ev.get("block_id")
            rel_pc = ev.get("rel_pc")
            loc = ""
            if block_type is not None and block_id is not None and rel_pc is not None:
                loc = f" bt={block_type} bid={block_id} +0x{int(rel_pc or 0):x}"
            if kind == "egress":
                details = f" {message}" if message else ""
                _append_host_io_output(client, f"[egress] {kernel_tab_id} wid={wid}{details}{loc}\n")
                qs = app_state.setdefault("debug_kernel_queue_state", {}).setdefault(kernel_tab_id, {})
                qs["lifetime_egress"] = int(qs.get("lifetime_egress", 0)) + 1
            elif kind == "signal":
                details = f" {message}" if message else ""
                _append_host_io_output(client, f"[host_signal] {kernel_tab_id} wid={wid}{details}{loc}\n")
            elif kind == "log" and message:
                _append_host_io_output(client, f"[host] {message}\n")

    def _retry_after_forced_bundle_load(dbg_c, bundle_path: str, kernel_id: int, kernel_tab_id: str) -> bool:
        result = _epa_dbg_load_bundle(bundle_path, kernel_id=kernel_id)
        if not result.get("ok"):
            _epa_dbg_log(f"[error] forced load_bundle failed: {result.get('error', 'unknown')}")
            return False
        app_state["debug_kernel_loaded"] = bundle_path
        app_state["debug_active_kernel"] = kernel_tab_id
        return True

    def _reset_all_kernel_debug_state(client):
        for kernel in _kernels_from_project():
            kernel_id = kernel["id"]
            _set_kernel_load_indicator(client, kernel_id, _IND_OFF)
            _set_kernel_run_indicator(client, kernel_id, _IND_OFF)
            _clear_kernel_queue_state(client, kernel_id)
        app_state["debug_kernel_snapshot_state"] = {}

    def _start_debug_vm(client, force_restart: bool = False) -> bool:
        if force_restart and _epa_dbg_running():
            _epa_dbg_stop()
        _epa_dbg_launch()
        dbg_c = _epa_dbg_client()
        if not dbg_c:
            return False
        try:
            session_path = _write_debug_session_descriptor()
            if session_path:
                _append_build_output(client, f"[debug-session] {session_path}\n")
        except Exception as exc:
            _append_build_output(client, f"[debug-session-error] {exc}\n")

        kernels = _kernels_from_project()
        failures = []
        last_loaded = ""
        if client:
            _reset_all_kernel_debug_state(client)

        bundle_targets: dict[str, list[str]] = {}
        for kernel in kernels:
            kernel_id = kernel["id"]
            bundle_path = _bundle_path_for_kernel_id(kernel_id)
            if not bundle_path or not Path(bundle_path).is_file():
                failures.append(kernel_id)
                _set_kernel_load_indicator(client, kernel_id, _IND_RED)
                continue
            bundle_targets.setdefault(bundle_path, []).append(kernel_id)

        for bundle_path, kernel_ids in bundle_targets.items():
            result = _epa_dbg_load_bundle(bundle_path, kernel_id=0)
            if result.get("ok"):
                for kernel_id in kernel_ids:
                    _set_kernel_load_indicator(client, kernel_id, _IND_GREEN)
                last_loaded = bundle_path
            else:
                for kernel_id in kernel_ids:
                    failures.append(kernel_id)
                    _set_kernel_load_indicator(client, kernel_id, _IND_RED)

        if last_loaded:
            app_state["debug_kernel_loaded"] = last_loaded
        else:
            app_state.pop("debug_kernel_loaded", None)
        app_state.pop("debug_active_kernel", None)
        app_state["debug_vm_started"] = True
        app_state["host_interconnect_ingress_count"] = 0
        app_state["host_interconnect_egress_count"] = 0
        _epa_dbg_set_vm_button(True)

        if failures:
            detail = f"{len(failures)} kernel load failure(s)"
            state = "error" if len(failures) == len(kernels) and kernels else "running"
            _epa_dbg_set_vm_status(state, detail)
        else:
            _epa_dbg_set_vm_status("running", "ready")
        return True

    def _refresh_ingress_profiles_list(client, type_name: str):
        items = _profiles_for_type(type_name) if type_name else []
        app_state["debug_ingress_profiles_cache"] = items
        cur = app_state.get("debug_ingress_selected_profile", "")
        valid_ids = {it["id"] for it in items}
        if items and (not cur or cur not in valid_ids):
            app_state["debug_ingress_selected_profile"] = items[0]["id"]
        elif not items:
            app_state["debug_ingress_selected_profile"] = ""
        try:
            client.replace_list_items("nav.debug.ingress_profiles", items)
        except Exception:
            pass

    def _open_ingress_profile_editor(client, type_name: str):
        try:
            type_defs = _parse_type_defs()
            fields = _type_field_names(type_defs, type_name)
            ingress_editor_state.clear()
            ingress_editor_state["type_name"] = type_name
            ingress_editor_state["fields"] = fields
            ingress_editor_state["field_values"] = {f: "0" for f in fields}
            ingress_editor_state["profile_name"] = ""
            ingress_editor_state["selected_field"] = fields[0] if fields else ""
            doc = build_ingress_profile_editor(type_name, fields)
            client.open_window("ingress-profile-editor", f"New {type_name} Profile", 640, 520, doc)
        except Exception as exc:
            print(json.dumps({"ingress_profile_editor_error": str(exc)}), flush=True)

    def _refresh_e_tab(client, tab_id: str, expected_seq: int | None = None, focus: bool = False):
        state = editor_state.get(tab_id)
        if not state:
            return
        ids = _editor_ids(tab_id)
        source_text = state.get("source_text", "")
        tab_entry = next((t for t in tab_list if t.get("tab_id") == tab_id), None)
        source_dir = Path(tab_entry["path"]).parent if tab_entry and tab_entry.get("path") else None
        try:
            result = _compile_e_source(source_text, source_dir)
        except Exception as exc:
            result = {
                "ok": False,
                "epa_text": "",
                "diagnostics": [],
                "message": str(exc),
            }
        if expected_seq is not None:
            current = editor_state.get(tab_id)
            if not current or current.get("compile_seq") != expected_seq:
                return
        state["epa_text"] = result["epa_text"]
        state["epa_block_map"] = result.get("epa_block_map", {})
        state["epa_map_path"] = _persist_debug_map(tab_id, result.get("epa_map_text", "")) if result.get("ok") else ""
        state["compile_error"] = result["message"]
        state["available_types"], state["available_workers"] = _extract_debug_candidates(source_text)
        state["trace_nodes"] = _build_trace_nodes([], f"{ids['debug']}.root")
        if result["ok"]:
            try:
                semantic = _analyze_e_source(source_text, ids, source_dir)
            except Exception as exc:
                semantic = {
                    "ok": False,
                    "message": str(exc),
                    "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root"),
                    "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                    "local_nodes": _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root"),
                    "dynamic_nodes": _parse_tree_lines("", "Dynamic Memory", f"{ids['debug_dynamic']}.root"),
                }
        else:
            semantic = {
                "ok": False,
                "message": result["message"],
                "ghs_nodes": _parse_tree_lines("Unavailable while the source has compile errors.", "GHS Layout", f"{ids['debug_ghs']}.root"),
                "stack_nodes": _parse_tree_lines("Unavailable while the source has compile errors.", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                "local_nodes": _parse_tree_lines("Unavailable while the source has compile errors.", "Local Arena", f"{ids['debug_local']}.root"),
                "dynamic_nodes": _parse_tree_lines("Unavailable while the source has compile errors.", "Dynamic Memory", f"{ids['debug_dynamic']}.root"),
            }
        state["ghs_nodes"] = semantic["ghs_nodes"]
        state["stack_nodes"] = semantic["stack_nodes"]
        state["local_nodes"] = semantic["local_nodes"]
        state["dynamic_nodes"] = semantic["dynamic_nodes"]
        epa_text_out = result["epa_text"] if result["ok"] else ""
        client.set_text(ids["epa"], epa_text_out)
        client.set_code_editor_diagnostics(ids["source"], result["diagnostics"])
        _apply_editor_view(client, tab_id, set_focus=focus)
        _refresh_debug_controls(client, tab_id)
        if app_state.get("active_editor_tab") == tab_id:
            _refresh_debug_sidebars(client, tab_id)

    def _init_editor_state():
        editor_state.clear()
        for tab_id, title, source_text in INITIAL_E_TABS:
            ids = _editor_ids(tab_id)
            editor_state[tab_id] = {
                "title": title,
                "source_text": source_text,
                "epa_text": "",
                "epa_block_map": {},
                "epa_map_path": "",
                "view": "e",
                "compile_error": "",
                "compile_seq": 0,
                "trace_nodes": None,
                "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{ids['debug_ghs']}.root"),
                "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{ids['debug_stack']}.root"),
                "local_nodes": _parse_tree_lines("", "Local Arena", f"{ids['debug_local']}.root"),
                "dynamic_nodes": _parse_tree_lines("", "Dynamic Memory", f"{ids['debug_dynamic']}.root"),
                "available_types": [],
                "available_workers": [],
                "undo_stack": [],
                "redo_stack": [],
                "last_undo_time": 0.0,
                "in_undo_redo": False,
            }

    def _wizard_navigate(client, path: str):
        """Navigate the wizard folder browser to path and refresh the list."""
        import os
        nav_state["path"] = path
        items = _folder_items(path)
        try:
            client.replace_list_items("wizard.folder_list", items)
            client.set_text("wizard.path_display", path)
            client.set_text("wizard.error", "")
        except Exception:
            pass

    def _new_file_navigate(client, path: str):
        new_file_nav_state["path"] = path
        items = _folder_items(path)
        print(f"[navigate] replacing list with {len(items)} items for {path!r}", flush=True)
        try:
            client.replace_list_items("new_file.folder_list", items)
            client.set_text("new_file.path_display", path)
            client.set_text("new_file.error", "")
            print(f"[navigate] done", flush=True)
        except Exception as e:
            print(f"[navigate error] {e}", flush=True)
            try:
                snap = client.snapshot()
                windows = (snap or {}).get("windows", [])
                for w in windows:
                    wid = w.get("window_id")
                    def _collect_ids(node, out):
                        if isinstance(node, dict):
                            nid = node.get("id")
                            if nid:
                                out.append(nid)
                            for v in node.values():
                                _collect_ids(v, out)
                        elif isinstance(node, list):
                            for v in node:
                                _collect_ids(v, out)
                    ids = []
                    _collect_ids(w.get("snapshot", {}), ids)
                    print(f"[navigate error] window={wid!r} registered_ids={ids}", flush=True)
            except Exception as snap_e:
                print(f"[navigate error] snapshot failed: {snap_e}", flush=True)

    def _open_file_navigate(client, path: str):
        """Navigate the open-file dialog to path and refresh the list."""
        open_file_nav_state["path"] = path
        items = _open_file_items(path)
        try:
            client.replace_list_items("dialog.files", items)
            client.set_text("dialog.location", path)
            client.set_text("dialog.breadcrumb", _breadcrumb_for(path))
            client.set_text("dialog.file_status", f"{len(items)} items")
        except Exception:
            pass

    def _open_project_navigate(client, path: str):
        """Navigate the open-project dialog to path and refresh the list."""
        open_project_nav_state["path"] = path
        items = _folder_items(path)
        try:
            client.replace_list_items("open_project.folder_list", items)
            client.set_text("open_project.path_display", path)
            client.set_text("open_project.hint", "Double-click a folder to navigate or open as project")
        except Exception:
            pass

    def _close_open_project_window(client):
        last_error = None
        for window_id in ("open-project", "open_project"):
            try:
                client.close_window(window_id)
                print(json.dumps({"open_project_window_closed": window_id}), flush=True)
                return True
            except Exception as exc:
                last_error = exc

        if last_error is not None:
            print(json.dumps({"open_project_window_close_failed": str(last_error)}), flush=True)
        return False

    def _persist_editor_session_state(allow_empty_tabs: bool = False):
        if app_state.get("_restoring_session"):
            return
        project_root = str(app_state.get("project_root", "") or "")
        if not project_root:
            return
        previous_state = _load_ide_state()
        previous_projects = previous_state.get("projects", {}) if isinstance(previous_state, dict) else {}
        previous_session = previous_projects.get(project_root, {}) if isinstance(previous_projects, dict) else {}
        if not previous_session and previous_state.get("last_project") == project_root:
            legacy_session = previous_state.get("session", {})
            if isinstance(legacy_session, dict):
                previous_session = legacy_session
        tabs = []
        for t in sorted(tab_list, key=lambda item: int(item.get("index", 0) or 0)):
            path = str(t.get("path", "") or "")
            if not path:
                continue
            tabs.append({
                "path": path,
                "preview": bool(t.get("preview", False)),
            })
        active_tab_id = app_state.get("active_editor_tab", "")
        active_path = ""
        for t in tab_list:
            if t.get("tab_id") == active_tab_id:
                active_path = str(t.get("path", "") or "")
                break
        if not tabs and not allow_empty_tabs:
            saved_tabs = previous_session.get("open_tabs", [])
            if isinstance(saved_tabs, list) and saved_tabs:
                tabs = saved_tabs
                active_path = str(previous_session.get("active_path", "") or "")
        updates = {
            "projects": {
                project_root: {
                    "open_tabs": tabs,
                    "active_path": active_path,
                    "nav_view": app_state.get("nav_view", "files"),
                    "bottom_view": app_state.get("bottom_view", "build"),
                    "cpp_breakpoints": {
                        path: sorted(int(line) for line in line_map.keys())
                        for path, line_map in (cpp_gdb_state.get("breakpoints_by_path", {}) or {}).items()
                        if line_map
                    },
                }
            },
            "last_project": project_root,
        }
        _save_ide_state(updates)

    def _project_session_state(project_root: str, state: dict | None = None) -> dict:
        state = state if isinstance(state, dict) else _load_ide_state()
        projects = state.get("projects", {}) if isinstance(state, dict) else {}
        if isinstance(projects, dict):
            session = projects.get(project_root, {})
            if isinstance(session, dict) and session:
                return session
        if state.get("last_project") == project_root:
            legacy_session = state.get("session", {})
            if isinstance(legacy_session, dict):
                return legacy_session
        return {}

    _NAV_SOURCE_EXTS: dict[str, set[str]] = {
        "epa":    {".e"},
        "cpp":    {".cpp", ".c", ".h", ".hpp", ".cc", ".cxx", ".hxx"},
        "python": {".py"},
    }

    def _dir_node(path: Path) -> dict:
        node: dict = {"id": str(path), "label": path.name, "expanded": True, "children": []}
        try:
            entries = sorted(path.iterdir(), key=lambda p: (p.is_file(), p.name.lower()))
            for entry in entries:
                if entry.name.startswith("."):
                    continue
                if entry.is_dir():
                    node["children"].append(_dir_node(entry))
                else:
                    node["children"].append({"id": str(entry), "label": entry.name})
        except PermissionError:
            pass
        return node

    def _collect_source_paths(tech_dir: Path, tech: str) -> set[str]:
        exts = _NAV_SOURCE_EXTS.get(tech, set())
        result: set[str] = set()
        try:
            for p in tech_dir.rglob("*"):
                if p.is_file() and not p.name.startswith(".") and p.suffix in exts:
                    result.add(str(p))
        except Exception:
            pass
        return result

    def _filter_tree_nodes(nodes: list, source_paths: set[str]) -> list:
        out = []
        for node in nodes:
            if "children" in node:
                filtered = _filter_tree_nodes(node["children"], source_paths)
                if filtered:
                    out.append({**node, "children": filtered})
            else:
                if node.get("id", "") in source_paths:
                    out.append(node)
        return out

    def _write_source_cache(project_path: Path, technologies: list):
        cache: dict[str, list[str]] = {}
        for tech in technologies:
            tech_dir = project_path / tech
            if not tech_dir.is_dir():
                continue
            exts = _NAV_SOURCE_EXTS.get(tech, set())
            sources: list[str] = []
            try:
                for p in tech_dir.rglob("*"):
                    if p.is_file() and not p.name.startswith(".") and p.suffix in exts:
                        sources.append(str(p.relative_to(project_path)))
            except Exception:
                pass
            cache[tech] = sorted(sources)
        try:
            cache_path = project_path / ".elaraproject" / "source_files.json"
            cache_path.parent.mkdir(parents=True, exist_ok=True)
            cache_path.write_text(json.dumps(cache, indent=2), encoding="utf-8")
        except Exception:
            pass

    _NAV_TECH_TREE_MAP = [
        ("epa",    "nav.tree.epa",    "nav.add_tech_wrap.epa"),
        ("cpp",    "nav.tree.cpp",    "nav.add_tech_wrap.cpp"),
        ("python", "nav.tree.python", "nav.add_tech_wrap.python"),
    ]
    _NAV_ARTIFACTS_TECH = "3d_artifacts"
    _NAV_LEVEL_PATHS_TECH = "level_paths"
    _NAV_3D_TREE_ID = "nav.tree.3d"
    _NAV_3D_TAB_INDEX = len(_NAV_TECH_TREE_MAP)

    def _three_d_nav_tab_json() -> str:
        ui = UiDocumentBuilder()
        ui.create_grid("nav.tech_panel.3d")
        ui.add_grid_column_fill("nav.tech_panel.3d")
        ui.add_grid_row_fill("nav.tech_panel.3d")
        ui.create_tree_view(_NAV_3D_TREE_ID)
        ui.set_property_number(_NAV_3D_TREE_ID, "font_size", 14)
        ui.set_section_json(_NAV_3D_TREE_ID, "nodes", [])
        ui.place_grid_child("nav.tech_panel.3d", _NAV_3D_TREE_ID, 0, 0)
        return json.dumps({
            "title": "3D",
            "button_glyph": "+",
            "button_action": "",
            "child": ui.widget_json("nav.tech_panel.3d", indent=None),
        }, separators=(",", ":"))

    def _sync_three_d_nav_tab(client, has_three_d: bool):
        present = bool(app_state.get("nav_3d_tab_present", False))
        if has_three_d and not present:
            try:
                client.call("ui.addTab", {
                    "target": "nav.file_tabs",
                    **json.loads(_three_d_nav_tab_json()),
                })
                app_state["nav_3d_tab_present"] = True
            except Exception:
                pass
        elif not has_three_d and present:
            try:
                client.fire("ui.removeTab", {"target": "nav.file_tabs", "index": _NAV_3D_TAB_INDEX})
            except Exception:
                pass
            app_state["nav_3d_tab_present"] = False

    def _artifact3d_color(template: dict, material_id: str, fallback: tuple[float, float, float]) -> tuple[float, float, float]:
        material = (template.get("materials") or {}).get(material_id, {})
        color = material.get("base_color") if isinstance(material, dict) else None
        if isinstance(color, list) and len(color) >= 3:
            try:
                return (float(color[0]), float(color[1]), float(color[2]))
            except Exception:
                pass
        return fallback

    def _artifact3d_preview_commands(path: Path) -> tuple[list[dict], list[str]]:
        try:
            viewer_state = app_state.get("artifact3d_viewer", {})
            camera = viewer_state.get("camera") if isinstance(viewer_state, dict) else None
            _, commands, errors = load_runtime_preview(path, camera)
            return commands, errors
        except Exception as exc:
            return ([
                {"op": "clear", "r": 0.04, "g": 0.02, "b": 0.02},
                {"op": "text", "x": 28, "y": 36, "text": path.name, "size": 22, "r": 0.96, "g": 0.96, "b": 0.96},
                {"op": "text", "x": 28, "y": 66, "text": "E3D runtime preview failed", "size": 16, "r": 0.96, "g": 0.42, "b": 0.38},
                {"op": "text", "x": 28, "y": 92, "text": str(exc), "size": 14, "r": 0.94, "g": 0.72, "b": 0.70},
            ], [str(exc)])

    def _open_artifact3d_viewer(client, path: str):
        artifact_path = Path(path)
        app_state["artifact3d_viewer"] = {
            "path": str(artifact_path),
            "camera": {"yaw_deg": 18.0, "pitch_deg": 8.0, "distance": 4.8},
            "dragging": False,
            "last_xy": None,
        }
        commands, errors = _artifact3d_preview_commands(artifact_path)
        ui = UiDocumentBuilder()
        ui.create_window("3D Artifact Viewer", 1040, 760, "org.elara.ui.epa-ide.artifact-viewer")
        ui.set_theme_mode("dark")
        ui.create_grid("artifact.viewer.shell")
        ui.add_grid_column_fill("artifact.viewer.shell")
        ui.add_grid_row_fill("artifact.viewer.shell")
        ui.set_root_content("artifact.viewer.shell")
        ui.create_vulkan_surface("artifact.viewer.surface", commands, kernel_name="artifact.preview")
        ui.set_property_number("artifact.viewer.surface", "virtual_width", 1000)
        ui.set_property_number("artifact.viewer.surface", "virtual_height", 720)
        overlay = artifact_path.name if not errors else f"{artifact_path.name}  ({len(errors)} validation issue(s))"
        ui.set_property_string("artifact.viewer.surface", "overlay_text", overlay)
        ui.place_grid_child("artifact.viewer.shell", "artifact.viewer.surface", 0, 0)
        client.open_window("artifact-3d-viewer", f"3D Artifact: {artifact_path.name}", 1040, 760, ui)
        try:
            client.call("ui.subscribe", {"target": "artifact.viewer.surface", "actions": ["mouseDown", "mouseMove", "mouseUp"]})
        except Exception:
            pass

    def _refresh_artifact3d_viewer(client):
        viewer_state = app_state.get("artifact3d_viewer", {})
        if not isinstance(viewer_state, dict):
            return
        path = viewer_state.get("path")
        if not isinstance(path, str) or not path:
            return
        commands, errors = _artifact3d_preview_commands(Path(path))
        try:
            client.call("ui.setSectionJson", {"target": "artifact.viewer.surface", "section": "commands", "value": commands})
        except Exception:
            return
        overlay = Path(path).name if not errors else f"{Path(path).name}  ({len(errors)} validation issue(s))"
        try:
            client.fire("ui.setProperty", {"target": "artifact.viewer.surface", "property": "overlay_text", "value": overlay})
        except Exception:
            pass

    def _level_path_tool_button_text(tool_id: str, current_tool: str) -> str:
        label_map = {"square": "Open Space", "cube": "Closed Space", "line": "Connecting Corridor"}
        prefix = ">" if tool_id == current_tool else " "
        return f"{prefix} {label_map.get(tool_id, tool_id.title())}"

    def _set_level_path_tool_labels(client):
        viewer_state = app_state.get("level_path_viewer", {})
        if not isinstance(viewer_state, dict):
            return
        current_tool = str(viewer_state.get("tool", "select"))
        for tool_id in ("square", "cube", "line"):
            try:
                client.fire("ui.setProperty", {
                    "target": f"level_path.tool.{tool_id}",
                    "property": "text",
                    "value": _level_path_tool_button_text(tool_id, current_tool),
                })
            except Exception:
                pass

    def _level_path_preview_commands() -> tuple[list[dict], list[str]]:
        viewer_state = app_state.get("level_path_viewer", {})
        if not isinstance(viewer_state, dict):
            return ([
                {"op": "clear", "r": 0.04, "g": 0.02, "b": 0.02},
                {"op": "text", "x": 28, "y": 36, "text": "Level path viewer not initialized", "size": 18, "r": 0.96, "g": 0.42, "b": 0.38},
            ], ["viewer not initialized"])
        doc = viewer_state.get("doc")
        if not isinstance(doc, dict):
            return ([
                {"op": "clear", "r": 0.04, "g": 0.02, "b": 0.02},
                {"op": "text", "x": 28, "y": 36, "text": "Level path document unavailable", "size": 18, "r": 0.96, "g": 0.42, "b": 0.38},
            ], ["document unavailable"])
        editor_state = {
            "tool": str(viewer_state.get("tool", "select")),
            "pending_line": viewer_state.get("pending_line"),
            "view": str(viewer_state.get("view", "top")),
            "zoom": float(viewer_state.get("zoom", 52.0) or 52.0),
            "center": list(viewer_state.get("center", [0.0, 0.0])),
            "active_item": (viewer_state.get("drag") or {}).get("item"),
        }
        try:
            return build_level_path_preview(doc, editor_state)
        except Exception as exc:
            return ([
                {"op": "clear", "r": 0.04, "g": 0.02, "b": 0.02},
                {"op": "text", "x": 28, "y": 36, "text": Path(str(viewer_state.get('path', 'level path'))).name, "size": 22, "r": 0.96, "g": 0.96, "b": 0.96},
                {"op": "text", "x": 28, "y": 66, "text": "Level path preview failed", "size": 16, "r": 0.96, "g": 0.42, "b": 0.38},
                {"op": "text", "x": 28, "y": 92, "text": str(exc), "size": 14, "r": 0.94, "g": 0.72, "b": 0.70},
            ], [str(exc)])

    def _open_level_path_viewer(client, path: str):
        level_path = Path(path)
        doc = load_level_path_document(level_path)
        app_state["level_path_viewer"] = {
            "path": str(level_path),
            "doc": doc,
            "tool": "select",
            "pending_line": None,
            "view": "top",
            "zoom": 52.0,
            "center": [0.0, 0.0],
            "drag": None,
        }
        commands, errors = _level_path_preview_commands()
        ui = UiDocumentBuilder()
        ui.create_window("Level Path Designer", 1280, 820, "org.elara.ui.epa-ide.level-path-viewer")
        ui.set_theme_mode("dark")
        ui.create_grid("level_path.viewer.shell")
        ui.add_grid_column_exact("level_path.viewer.shell", 180)
        ui.add_grid_column_fill("level_path.viewer.shell")
        ui.add_grid_row_fill("level_path.viewer.shell")
        ui.set_root_content("level_path.viewer.shell")
        ui.create_grid("level_path.viewer.sidebar")
        ui.add_grid_column_fill("level_path.viewer.sidebar")
        ui.add_grid_row_exact("level_path.viewer.sidebar", 36)
        ui.add_grid_row_exact("level_path.viewer.sidebar", 18)
        ui.add_grid_row_exact("level_path.viewer.sidebar", 44)
        ui.add_grid_row_exact("level_path.viewer.sidebar", 44)
        ui.add_grid_row_exact("level_path.viewer.sidebar", 44)
        ui.add_grid_row_fill("level_path.viewer.sidebar")
        ui.create_label("level_path.viewer.title", "Space Tools", 18)
        ui.create_label("level_path.viewer.hint", "Wheel zooms. Drag items to move in the current view plane.", 12)
        ui.create_button("level_path.tool.square", _level_path_tool_button_text("square", "select"), "level_path.tool.square")
        ui.create_button("level_path.tool.cube", _level_path_tool_button_text("cube", "select"), "level_path.tool.cube")
        ui.create_button("level_path.tool.line", _level_path_tool_button_text("line", "select"), "level_path.tool.line")
        ui.place_grid_child("level_path.viewer.sidebar", "level_path.viewer.title", 0, 0)
        ui.place_grid_child("level_path.viewer.sidebar", "level_path.viewer.hint", 0, 1)
        ui.place_grid_child("level_path.viewer.sidebar", "level_path.tool.square", 0, 2)
        ui.place_grid_child("level_path.viewer.sidebar", "level_path.tool.cube", 0, 3)
        ui.place_grid_child("level_path.viewer.sidebar", "level_path.tool.line", 0, 4)
        ui.place_grid_child("level_path.viewer.shell", "level_path.viewer.sidebar", 0, 0)
        ui.create_vulkan_surface("level_path.viewer.surface", commands, kernel_name="level_path.preview")
        ui.set_property_number("level_path.viewer.surface", "virtual_width", 1180)
        ui.set_property_number("level_path.viewer.surface", "virtual_height", 820)
        overlay = level_path.name if not errors else f"{level_path.name}  ({len(errors)} validation issue(s))"
        ui.set_property_string("level_path.viewer.surface", "overlay_text", overlay)
        ui.place_grid_child("level_path.viewer.shell", "level_path.viewer.surface", 1, 0)
        client.open_window("level-path-viewer", f"Level Path: {level_path.name}", 1280, 820, ui)
        try:
            client.call("ui.subscribe", {"target": "level_path.viewer.surface", "actions": ["mouseDown", "mouseMove", "mouseUp", "mouseWheel"]})
        except Exception:
            pass

    def _refresh_level_path_viewer(client):
        viewer_state = app_state.get("level_path_viewer", {})
        if not isinstance(viewer_state, dict):
            return
        commands, errors = _level_path_preview_commands()
        try:
            client.call("ui.setSectionJson", {"target": "level_path.viewer.surface", "section": "commands", "value": commands})
        except Exception:
            return
        overlay = Path(str(viewer_state.get("path", "level_path"))).name
        if errors:
            overlay = f"{overlay}  ({len(errors)} validation issue(s))"
        try:
            client.fire("ui.setProperty", {"target": "level_path.viewer.surface", "property": "overlay_text", "value": overlay})
        except Exception:
            pass
        _set_level_path_tool_labels(client)

    def _snapshot_widget_bounds(client, target: str) -> dict | None:
        try:
            snap = client.call("ui.snapshotWidget", {"target": target})
        except Exception:
            return None
        if not isinstance(snap, dict):
            return None
        bounds = snap.get("bounds")
        if isinstance(bounds, dict):
            return bounds
        return None

    def _level_path_surface_event_to_virtual(client, x: float, y: float) -> tuple[float, float]:
        if x < 0.0 or y < 0.0:
            return (x, y)
        bounds = _snapshot_widget_bounds(client, "level_path.viewer.surface")
        if not isinstance(bounds, dict):
            return (x, y)
        try:
            width = float(bounds.get("width", 0.0) or 0.0)
            height = float(bounds.get("height", 0.0) or 0.0)
        except Exception:
            return (x, y)
        if width <= 1.0 or height <= 1.0:
            return (x, y)
        return (
            x * (1180.0 / width),
            y * (820.0 / height),
        )

    def _populate_nav_trees(client, project_path: Path, technologies: list):
        source_only = app_state.get("nav_filter_source_only", False)
        all_nodes: list = []
        per_tech: dict = {}
        for tech, tree_id, wrap_id in _NAV_TECH_TREE_MAP:
            tech_dir = project_path / tech
            has_tech = tech in technologies and tech_dir.is_dir()
            if has_tech:
                root_node = _dir_node(tech_dir)
                tech_nodes = root_node.get("children", [])
                if source_only:
                    src_paths = _collect_source_paths(tech_dir, tech)
                    tech_nodes = _filter_tree_nodes(tech_nodes, src_paths)
            else:
                tech_nodes = []
            per_tech[tech] = tech_nodes
            all_nodes.extend(tech_nodes)
            try:
                doc = json.dumps({"nodes": tech_nodes}, separators=(",", ":"))
                client.call("ui.replaceChildren", {"target": tree_id, "document": doc})
            except Exception:
                pass
            try:
                client.fire("ui.setVisible", {"target": tree_id, "visible": has_tech})
                client.fire("ui.setVisible", {"target": wrap_id, "visible": not has_tech})
            except Exception:
                pass
        artifacts_dir = project_path / _NAV_ARTIFACTS_TECH
        level_paths_dir = project_path / _NAV_LEVEL_PATHS_TECH
        has_artifacts = artifacts_dir.is_dir()
        has_level_paths = level_paths_dir.is_dir()
        _sync_three_d_nav_tab(client, has_artifacts or has_level_paths)
        artifact_nodes = _dir_node(artifacts_dir).get("children", []) if has_artifacts else []
        level_path_nodes = _dir_node(level_paths_dir).get("children", []) if has_level_paths else []
        per_tech[_NAV_ARTIFACTS_TECH] = artifact_nodes
        per_tech[_NAV_LEVEL_PATHS_TECH] = level_path_nodes
        if has_artifacts or has_level_paths:
            tree_nodes = []
            if has_artifacts:
                tree_nodes.append({
                    "id": str(artifacts_dir),
                    "label": "3D Artifacts",
                    "expanded": True,
                    "children": artifact_nodes,
                })
                all_nodes.extend(artifact_nodes)
            if has_level_paths:
                tree_nodes.append({
                    "id": str(level_paths_dir),
                    "label": "Level Paths",
                    "expanded": True,
                    "children": level_path_nodes,
                })
                all_nodes.extend(level_path_nodes)
            try:
                doc = json.dumps({"nodes": tree_nodes}, separators=(",", ":"))
                client.call("ui.replaceChildren", {"target": _NAV_3D_TREE_ID, "document": doc})
            except Exception:
                pass
        app_state["nav_tree_nodes"] = all_nodes
        app_state["nav_tree_nodes_per_tech"] = per_tech
        try:
            client.set_text("nav.filter_toggle", "⊟" if source_only else "≡")
        except Exception:
            pass

    def _clear_editor_tabs(client, persist_empty: bool = False):
        for entry in sorted(list(tab_list), key=lambda item: int(item.get("index", 0) or 0), reverse=True):
            tab_id = entry.get("tab_id", "")
            if tab_id in editor_state:
                try:
                    _save_history(tab_id)
                except Exception:
                    pass
            try:
                client.fire("ui.removeTab", {"target": "editor.tabs", "index": int(entry.get("index", 0) or 0)})
            except Exception:
                pass
        tab_list.clear()
        editor_state.clear()
        app_state["active_editor_tab"] = ""
        try:
            client.set_visible_batch([("editor.tabs", False), ("editor.welcome", True)])
        except Exception:
            pass
        if persist_empty:
            _persist_editor_session_state(allow_empty_tabs=True)

    def _restore_project_session(client, project_path: str, session: dict | None = None):
        session = session if isinstance(session, dict) else _project_session_state(project_path)
        if not session:
            return
        cpp_breakpoints = session.get("cpp_breakpoints", {})
        if isinstance(cpp_breakpoints, dict):
            restored_breakpoints = {}
            for path, lines in cpp_breakpoints.items():
                if not isinstance(lines, list):
                    continue
                restored_lines = {}
                for line in lines:
                    try:
                        line0 = int(line)
                    except Exception:
                        continue
                    if line0 >= 0:
                        restored_lines[line0] = ""
                if restored_lines:
                    restored_breakpoints[str(path)] = restored_lines
            cpp_gdb_state["breakpoints_by_path"] = restored_breakpoints
        app_state["_restoring_session"] = True
        try:
            open_tabs = session.get("open_tabs", [])
            if isinstance(open_tabs, list):
                for entry in open_tabs:
                    if isinstance(entry, str):
                        path = entry
                        preview = False
                    elif isinstance(entry, dict):
                        path = str(entry.get("path", "") or "")
                        preview = bool(entry.get("preview", False))
                    else:
                        continue
                    if path and Path(path).is_file():
                        _open_file_tab(client, path, make_permanent=not preview)

            active_path = str(session.get("active_path", "") or "")
            active = next((t for t in tab_list if active_path and t.get("path") == active_path), None)
            if active:
                app_state["active_editor_tab"] = active["tab_id"]
                try:
                    client.fire("ui.setActiveTab", {"target": "editor.tabs", "index": active["index"]})
                except Exception:
                    pass
        finally:
            app_state.pop("_restoring_session", None)

        if tab_list:
            try:
                client.set_visible_batch([("editor.welcome", False), ("editor.tabs", True)])
            except Exception:
                pass
        _persist_editor_session_state()

    def _open_project(client, project_path, restore_session: bool = False):
        """Open a project: initialise state, populate nav trees, update UI chrome."""
        project_path = Path(project_path)
        previous_project = str(app_state.get("project_root", "") or "")
        project_changed = bool(previous_project and previous_project != str(project_path))
        if project_changed:
            _persist_editor_session_state()
        _cpp_gdb_stop_session()
        _host_debug_bridge_stop()
        _external_logic_bridge_stop()
        _save_ide_state({"last_project": str(project_path)})
        meta_path = project_path / ".elaraproject" / "project.json"
        try:
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
        except Exception:
            meta = {}
        project_name = meta.get("name", project_path.name)
        technologies = meta.get("technologies", [])

        _write_source_cache(project_path, technologies)
        _populate_nav_trees(client, project_path, technologies)
        try:
            client.set_text("nav.project_title", project_name)
        except Exception:
            pass
        try:
            client.set_window_title(f"EPA-IDE : {project_name}")
        except Exception:
            pass
        try:
            client.configure_menu_bar_chrome(
                "app.menu",
                custom_chrome=not _use_system_window_header(),
                window_title=f"EPA-IDE : {project_name}",
            )
        except Exception:
            pass

        app_state.update({
            "project_root": str(project_path),
            "project_name": project_name,
        })
        if project_changed or restore_session:
            if restore_session and not project_changed:
                _persist_editor_session_state()
            _clear_editor_tabs(client, persist_empty=False)
        terminal_state["cwd"] = str(project_path)
        terminal_state["output"] = f"Terminal ready.\nCWD: {terminal_state['cwd']}\n$ "
        terminal_state.pop("spawned", None)
        try:
            client.fire("ui.setVisible", {"target": "nav.no_project", "visible": False})
            client.fire("ui.setVisible", {"target": "nav.file_tabs", "visible": True})
            client.fire("ui.setVisible", {"target": "app.toolbar", "visible": True})
            _set_project_toolbar_enabled(client, True)
        except Exception:
            pass
        app_state.pop("debug_kernel_loaded", None)
        app_state.pop("debug_active_kernel", None)
        app_state.pop("debug_session_id", None)
        app_state.pop("debug_session_path", None)
        app_state["debug_vm_started"] = False
        app_state["debug_kernel_indicator_state"] = {}
        app_state["debug_kernel_queue_state"] = {}
        app_state["debug_kernel_snapshot_state"] = {}
        _epa_dbg_set_vm_button(False)
        _set_cpp_status_text(client, "No GDB session attached.")
        _set_cpp_thread_items(client, [])
        _set_cpp_registers_text(client, [])
        _set_cpp_memory_text(client, [])
        _python_dbg_stop(client)
        _close_open_project_window(client)
        if restore_session:
            session = _project_session_state(str(project_path))
            nav_view = str(session.get("nav_view", "") or "")
            if nav_view in _NAV_PANELS:
                app_state["nav_view"] = nav_view
            bottom_view = str(session.get("bottom_view", "") or "")
            if bottom_view:
                app_state["bottom_view"] = bottom_view
            _restore_project_session(client, str(project_path), session)
            _switch_nav_view(client, app_state.get("nav_view", "files"))
            _set_bottom_view(client, app_state.get("bottom_view", "build"))

    _NAV_PANELS = {
        "files":  "nav.panel",
        "search": "nav.search_panel",
        "repo":   "nav.repo_panel",
        "issues": "nav.issues_panel",
        "debug":  "nav.debug_panel",
    }

    def _switch_nav_view(client, view: str):
        if view not in _NAV_PANELS:
            return
        app_state["nav_view"] = view
        _persist_editor_session_state()
        for v, panel in _NAV_PANELS.items():
            try:
                client.fire("ui.setVisible", {"target": panel, "visible": v == view})
            except Exception:
                pass
        if view == "repo":
            _refresh_repo_panel(client)
            _refresh_ev_panel(client)
        elif view == "issues":
            _refresh_issues_panel(client)
        elif view == "debug":
            _refresh_debug_panel(client)

    def _apply_right_panel_visibility(client, visible: bool):
        width = _layout_value(app_state.get("right_panel_width"), 320)
        try:
            shell = client.get_grid_layout_state("app.shell")
            columns = shell.get("columns") or []
            if len(columns) > 3:
                current_width = columns[3].get("computed_size", 0)
                if visible:
                    saved_width = _layout_value(app_state.get("right_panel_width"), 320)
                    width = saved_width
                elif current_width and current_width >= 120:
                    width = _layout_value(current_width, 320)
                    app_state["right_panel_width"] = width
                    _save_ide_state({"layout": {"ai_width": width}})
        except Exception:
            pass

        try:
            client.set_grid_column_exact_size("app.shell", 3, width if visible else 0)
        except Exception:
            try:
                client.call(
                    "ui.setGridColumnExactSize",
                    {"target": "app.shell", "index": 3, "size": width if visible else 0},
                )
            except Exception:
                pass

        try:
            client.set_visible("ai.panel", visible)
        except Exception:
            pass

    def _toggle_right_panel(client):
        visible = not bool(app_state.get("right_panel_visible", True))
        app_state["right_panel_visible"] = visible
        _save_ide_state({"ui": {"right_panel_visible": visible}})
        _apply_right_panel_visibility(client, visible)

    def _apply_bottom_panel_visibility(client, visible: bool):
        height = _layout_value(app_state.get("bottom_panel_height"), 220)
        try:
            center = client.get_grid_layout_state("app.center")
            rows = center.get("rows") or []
            if len(rows) > 1:
                current_height = rows[1].get("computed_size", 0)
                if visible:
                    saved_height = _layout_value(app_state.get("bottom_panel_height"), 220)
                    height = saved_height
                elif current_height and current_height >= 120:
                    height = _layout_value(current_height, 220)
                    app_state["bottom_panel_height"] = height
                    _save_ide_state({"layout": {"bottom_height": height}})
        except Exception:
            pass

        try:
            client.set_grid_row_exact_size("app.center", 1, height if visible else 0)
        except Exception:
            try:
                client.call(
                    "ui.setGridRowExactSize",
                    {"target": "app.center", "index": 1, "size": height if visible else 0},
                )
            except Exception:
                pass

        try:
            client.set_visible("bottom.panel", visible)
        except Exception:
            pass

    def _toggle_bottom_panel(client):
        visible = not bool(app_state.get("bottom_panel_visible", False))
        app_state["bottom_panel_visible"] = visible
        _save_ide_state({"ui": {"bottom_panel_visible": visible}})
        _apply_bottom_panel_visibility(client, visible)

    def _set_bottom_view(client, view: str):
        app_state["bottom_view"] = view
        _persist_editor_session_state()
        show_build = view == "build"
        show_host_io = view == "host_io"
        show_console = view == "console"
        show_terminal = view == "terminal"
        show_status = view == "status"
        try:
            client.set_visible_batch([
                ("bottom.build_output", show_build),
                ("bottom.host_io_output", show_host_io),
                ("bottom.console_panel", show_console),
                ("bottom.terminal_panel", show_terminal),
                ("bottom.status_panel", show_status),
            ])
            if show_console:
                client.set_focus("bottom.console_input")
            if show_terminal:
                _ensure_terminal_spawned(client)
                client.set_focus("bottom.terminal_widget")
            if show_status:
                _refresh_status_panel(client)
        except Exception:
            pass

    def _ensure_terminal_spawned(client):
        if not terminal_state.get("spawned"):
            try:
                cwd = terminal_state.get("cwd") or app_state.get("project_root") or os.getcwd()
                client.spawn_terminal_shell("bottom.terminal_widget", cwd)
                terminal_state["spawned"] = True
            except Exception:
                pass

    def _terminal_cwd() -> str:
        cwd = terminal_state.get("cwd") or app_state.get("project_root") or os.getcwd()
        try:
            path = Path(cwd).expanduser()
            if path.is_dir():
                return str(path)
        except Exception:
            pass
        return os.getcwd()

    def _open_full_terminal():
        import shutil
        cwd = _terminal_cwd()
        import shlex
        for cmd in [
            ["lxterminal", f"--working-directory={cwd}"],
            ["gnome-terminal", f"--working-directory={cwd}"],
            ["xfce4-terminal", f"--default-working-directory={cwd}"],
            ["xterm", "-e", "bash", "-c", "cd " + shlex.quote(cwd) + " && exec bash"],
        ]:
            if shutil.which(cmd[0]):
                subprocess.Popen(cmd, close_fds=True)
                return

    def _terminal_append(client, text: str):
        terminal_state["output"] = (terminal_state.get("output", "") + text)[-24000:]
        try:
            client.set_text("bottom.terminal_output", terminal_state["output"])
            client.scroll_to_bottom("bottom.terminal_output")
        except Exception:
            pass

    def _run_terminal_command(client, command: str):
        command = command.strip()
        cwd = _terminal_cwd()
        terminal_state["cwd"] = cwd

        if not command:
            _terminal_append(client, "\n$ ")
            return

        _terminal_append(client, f"{command}\n")

        if command == "clear":
            terminal_state["output"] = "$ "
            try:
                client.set_text("bottom.terminal_output", terminal_state["output"])
            except Exception:
                pass
            return

        if command.startswith("cd"):
            target = command[2:].strip() or str(Path.home())
            next_cwd = Path(target).expanduser()
            if not next_cwd.is_absolute():
                next_cwd = Path(cwd) / next_cwd
            try:
                resolved = next_cwd.resolve()
                if resolved.is_dir():
                    terminal_state["cwd"] = str(resolved)
                    _terminal_append(client, f"CWD: {terminal_state['cwd']}\n$ ")
                else:
                    _terminal_append(client, f"cd: no such directory: {target}\n$ ")
            except Exception as exc:
                _terminal_append(client, f"cd: {exc}\n$ ")
            return

        def _worker(cmd=command, run_cwd=cwd):
            try:
                proc = subprocess.run(
                    cmd,
                    cwd=run_cwd,
                    shell=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    timeout=120,
                    env=os.environ.copy(),
                )
                output = proc.stdout or ""
                if proc.returncode:
                    output += f"[exit {proc.returncode}]\n"
            except subprocess.TimeoutExpired:
                output = "[command timed out after 120s]\n"
            except Exception as exc:
                output = f"{exc}\n"

            def _update():
                _terminal_append(client, output + "$ ")
            _deferred(_update)

        threading.Thread(target=_worker, daemon=True).start()

    # -- Console helpers -------------------------------------------------------

    def _console_append(client, text: str):
        console_state["output"] = (console_state.get("output", "") + text)[-32000:]
        try:
            client.set_text("bottom.console_output", console_state["output"])
            client.scroll_to_bottom("bottom.console_output")
        except Exception:
            pass

    def _console_format_help(help_obj: dict) -> str:
        lines = []
        overview = help_obj.get("overview", "")
        if overview:
            lines.append(overview)
            lines.append("")
        conn = help_obj.get("connection", {})
        if conn:
            lines.append("Connection:")
            for k, v in conn.items():
                lines.append(f"  {k}: {v}")
            lines.append("")
        methods = help_obj.get("methods", {})
        if methods:
            lines.append("Methods:")
            for name, info in methods.items():
                desc = info.get("desc", "") if isinstance(info, dict) else str(info)
                lines.append(f"  {name}")
                if desc:
                    lines.append(f"    {desc}")
                params = info.get("params", {}) if isinstance(info, dict) else {}
                if params:
                    for pname, pdesc in params.items():
                        lines.append(f"    param {pname}: {pdesc}")
                result = info.get("result") if isinstance(info, dict) else None
                if result is not None:
                    result_str = json.dumps(result) if not isinstance(result, str) else result
                    lines.append(f"    returns: {result_str}")
        return "\n".join(lines)

    def _console_parse_command(raw: str):
        """Parse a console command line into (method, params_dict).

        Accepted forms:
          help
          ping
          get_file_content tab_id=tab.abc123
          get_file_content {"tab_id": "tab.abc123"}
          {"method": "ping", "params": {}}
        """
        raw = raw.strip()
        if not raw:
            return None, None
        # Raw JSON object
        if raw.startswith("{"):
            try:
                obj = json.loads(raw)
                return obj.get("method", ""), obj.get("params") or {}
            except json.JSONDecodeError as exc:
                return None, f"JSON parse error: {exc}"
        # method [args...]
        parts = raw.split(None, 1)
        method = parts[0]
        rest = parts[1].strip() if len(parts) > 1 else ""
        if not rest:
            return method, {}
        # JSON object argument
        if rest.startswith("{"):
            try:
                return method, json.loads(rest)
            except json.JSONDecodeError as exc:
                return None, f"JSON parse error: {exc}"
        # key=value pairs
        params = {}
        for token in rest.split():
            if "=" in token:
                k, _, v = token.partition("=")
                # Try to parse value as JSON scalar, fall back to string
                try:
                    params[k] = json.loads(v)
                except json.JSONDecodeError:
                    params[k] = v
            else:
                params[token] = True
        return method, params

    def _run_console_command(client, raw: str):
        raw = raw.strip()
        if not raw:
            return

        hist = console_state.setdefault("history", [])
        if not hist or hist[-1] != raw:
            hist.append(raw)
        console_state["history_pos"] = -1

        _console_append(client, f"> {raw}\n")

        if raw in ("clear", "cls"):
            console_state["output"] = ""
            try:
                client.set_text("bottom.console_output", "")
            except Exception:
                pass
            return

        method, params = _console_parse_command(raw)
        if method is None:
            _console_append(client, f"Error: {params}\n\n")
            return

        srv = _ai_rpc_server_ref[0]
        if srv is None:
            _console_append(client, "Error: AI RPC server not yet started.\n\n")
            return

        def _worker(m=method, p=params):
            try:
                resp = srv._dispatch({"method": m, "params": p})
            except Exception as exc:
                resp = {"ok": False, "error": str(exc)}

            if not resp.get("ok"):
                text = f"Error: {resp.get('error', '?')}\n\n"
            else:
                result = resp.get("result")
                if m == "help" and isinstance(result, dict):
                    text = _console_format_help(result) + "\n\n"
                elif isinstance(result, (dict, list)):
                    text = json.dumps(result, indent=2, ensure_ascii=False) + "\n\n"
                else:
                    text = str(result) + "\n\n"

            _deferred(lambda t=text: _console_append(client, t))

        threading.Thread(target=_worker, daemon=True).start()

    def _refresh_repo_panel(client):
        project_root = app_state.get("project_root", "")
        if not project_root:
            try:
                client.set_text("nav.repo.status", "No project open")
                client.replace_list_items("nav.repo.changes", [])
            except Exception:
                pass
            return
        import subprocess
        try:
            result = subprocess.run(
                ["git", "-C", project_root, "status", "--porcelain"],
                capture_output=True, text=True, timeout=5
            )
            lines = [l for l in result.stdout.splitlines() if l.strip()]
        except Exception:
            lines = []
        items = []
        for line in lines:
            status = line[:2].strip()
            path = line[3:].strip()
            label = f"[{status}]  {path}"
            items.append({"id": f"repo.file.{path}", "label": label})
        summary = f"{len(items)} change{'s' if len(items) != 1 else ''}" if items else "No changes"
        try:
            client.set_text("nav.repo.status", summary)
            client.replace_list_items("nav.repo.changes", items)
        except Exception:
            pass

    ev_state = {"commit_msg": ""}

    def _ev_repo() -> "ProjectRepo | None":
        if not _EV_AVAILABLE:
            return None
        project_root = app_state.get("project_root", "")
        if not project_root:
            return None
        repo_dir = Path(project_root) / EV_PROJECT_DIR
        if not repo_dir.is_dir():
            return None
        try:
            return ProjectRepo(Path(project_root))
        except Exception:
            return None

    def _refresh_ev_panel(client):
        repo = _ev_repo()
        has_repo = repo is not None
        try:
            client.set_visible("nav.ev_repo_content",    has_repo)
            client.set_visible("nav.ev_tab_setup_panel", not has_repo)
        except Exception:
            pass
        if not has_repo:
            try:
                project_root = app_state.get("project_root", "")
                if project_root and not ev_setup_state.get("name"):
                    suggested = Path(project_root).name
                    ev_setup_state["name"] = suggested
                    client.set_text("nav.ev_tab_setup.name", suggested)
                client.set_text("nav.ev_tab_setup.error", "")
            except Exception:
                pass
            return
        try:
            branch = repo.current_branch()
            status = repo.status()
        except Exception as exc:
            try:
                client.set_text("nav.ev.branch_label", f"Error: {exc}")
                client.set_text("nav.ev.status", "")
                client.replace_list_items("nav.ev.changes", [])
            except Exception:
                pass
            return
        added    = status.get("added", [])
        modified = status.get("modified", [])
        deleted  = status.get("deleted", [])
        total = len(added) + len(modified) + len(deleted)
        summary = f"{total} change{'s' if total != 1 else ''}" if total else "No changes"
        items = []
        for p in added:
            items.append({"id": f"ev.file.{p}", "label": f"A  {p}"})
        for p in modified:
            items.append({"id": f"ev.file.{p}", "label": f"M  {p}"})
        for p in deleted:
            items.append({"id": f"ev.file.{p}", "label": f"D  {p}"})
        try:
            client.set_text("nav.ev.branch_label", f"Branch: {branch}")
            client.set_text("nav.ev.status", summary)
            client.replace_list_items("nav.ev.changes", items)
        except Exception:
            pass

    ev_bugs_state = {"selected_id": None, "filter": "all", "new_title": ""}
    ev_setup_state = {"name": "", "server": "", "remote_root": "", "branch": "main"}

    def _issues_list_items() -> list:
        repo = _ev_repo()
        if not repo:
            return []
        try:
            bugs = repo.list_bugs()
        except Exception:
            return [{"id": "issues.error", "label": "Error loading bugs"}]
        filt = ev_bugs_state.get("filter", "all")
        items = []
        for bug in bugs:
            status = bug.get("status", "open")
            if filt == "open" and status != "open":
                continue
            if filt == "closed" and status != "closed":
                continue
            icon = "✓" if status == "closed" else "●"
            severity = bug.get("severity", "medium")
            bug_id = bug.get("bug_id", "")
            title = bug.get("title", "")
            items.append({"id": f"issue.{bug_id}", "label": f"{icon}  {bug_id}  [{severity}]  {title}"})
        if not items:
            items.append({"id": "issues.empty", "label": f"No {filt if filt != 'all' else ''} bugs".strip()})
        return items

    def _refresh_issues_panel(client):
        repo = _ev_repo()
        has_repo = repo is not None
        try:
            client.set_visible("nav.ev_issues_content", has_repo)
            client.set_visible("nav.ev_setup_panel",    not has_repo)
            if has_repo:
                client.replace_list_items("nav.issues.list", _issues_list_items())
            else:
                project_root = app_state.get("project_root", "")
                if project_root and not ev_setup_state.get("name"):
                    suggested = Path(project_root).name
                    ev_setup_state["name"] = suggested
                    client.set_text("nav.ev_setup.name", suggested)
                client.set_text("nav.ev_setup.error", "")
        except Exception:
            pass

    def _update_status_panel_dot(client, section: str,
                                dot1_state: str, label1: str,
                                dot2_state: str, label2: str):
        """Update a status section's two indicator dots and labels."""
        if not client:
            return
        color1 = _IND_COLORS.get(dot1_state, _IND_COLORS[_IND_OFF])
        color2 = _IND_COLORS.get(dot2_state, _IND_COLORS[_IND_OFF])
        try:
            client.fire("ui.setForegroundColor",
                        {"target": f"bottom.status.{section}.dot",  "color": color1})
            client.fire("ui.setForegroundColor",
                        {"target": f"bottom.status.{section}.dot2", "color": color2})
            client.set_text(f"bottom.status.{section}.label",  label1)
            client.set_text(f"bottom.status.{section}.label2", label2)
        except Exception:
            pass

    def _update_host_status_ping(client, ping1_state: str, ping2_state: str):
        if not client:
            return
        color1 = _IND_COLORS.get(ping1_state, _IND_COLORS[_IND_OFF])
        color2 = _IND_COLORS.get(ping2_state, _IND_COLORS[_IND_OFF])
        try:
            client.fire("ui.setForegroundColor",
                        {"target": "bottom.status.host.ping", "color": color1})
            client.fire("ui.setForegroundColor",
                        {"target": "bottom.status.host.ping2", "color": color2})
        except Exception:
            pass

    def _refresh_status_panel(client):
        """Recompute and display current connection status for all three columns."""
        if not client:
            return

        # ── EPA DBG ──────────────────────────────────────────────────────────
        epa_proc = _epa_dbg.get("proc")
        epa_client = _epa_dbg.get("client")
        epa_port = _epa_dbg.get("port")
        if not epa_proc or epa_proc.poll() is not None:
            epa_dot1, epa_lbl1 = _IND_RED,    "Offline"
            epa_dot2, epa_lbl2 = _IND_OFF,    "—"
            epa_port_text = "Port: —"
        elif not epa_client or not epa_client.connected:
            epa_dot1, epa_lbl1 = _IND_ORANGE, "Listening"
            epa_dot2, epa_lbl2 = _IND_RED,    "IDE not connected"
            epa_port_text = f"Port: {epa_port}"
        else:
            epa_dot1, epa_lbl1 = _IND_GREEN,  "Running"
            epa_dot2, epa_lbl2 = _IND_GREEN,  "IDE connected"
            epa_port_text = f"Port: {epa_port}"
        ide_connected = bool(epa_client and epa_client.connected)

        # ── Host interconnect ─────────────────────────────────────────────────
        # The host interconnect is part of the runtime contract, so keep the
        # listener present even when Python/GDB logic is optional or stopped.
        if host_debug_bridge.get("server") is None:
            try:
                _host_debug_bridge_ensure()
            except Exception:
                pass
        if external_logic_bridge.get("server") is None:
            try:
                _external_logic_bridge_ensure()
            except Exception:
                pass
        bridge_server = host_debug_bridge.get("server")
        bridge_port = host_debug_bridge.get("port")
        bridge_connected = host_debug_bridge.get("client_connected", False)
        ext_logic_connected = host_debug_bridge.get("external_logic_connected", False)
        ext_logic_proxied   = host_debug_bridge.get("ext_logic_proxied", False)
        now_ts = time.time()
        host_ping_fresh = (now_ts - float(host_debug_bridge.get("last_host_pong", 0.0) or 0.0)) < 3.5
        ext_ping_fresh = (now_ts - float(host_debug_bridge.get("last_ext_pong", 0.0) or 0.0)) < 3.5
        if not bridge_server:
            host_dot1, host_lbl1 = _IND_RED,    "Offline"
            host_dot2, host_lbl2 = _IND_OFF,    "—"
            host_port_text = "Port: —"
        else:
            host_ingress_count = int(app_state.get("host_interconnect_ingress_count", 0) or 0)
            host_egress_count = int(app_state.get("host_interconnect_egress_count", 0) or 0)
            # dot1: EPA host bridge availability
            if ide_connected and bridge_connected:
                host_dot1, host_lbl1 = _IND_GREEN,  f"EPA host  in={host_ingress_count}"
            elif ide_connected or bridge_connected:
                host_dot1, host_lbl1 = _IND_ORANGE, f"EPA host  in={host_ingress_count}"
            else:
                host_dot1, host_lbl1 = _IND_OFF,    f"EPA host  in={host_ingress_count}"
            # dot2: External logic — grey → orange (Python only) → green (proxied to C++)
            if ext_logic_proxied:
                host_dot2, host_lbl2 = _IND_GREEN,  f"External logic  out={host_egress_count}"
            elif ext_logic_connected:
                host_dot2, host_lbl2 = _IND_ORANGE, f"External logic  out={host_egress_count}"
            else:
                host_dot2, host_lbl2 = _IND_OFF,    f"External logic  out={host_egress_count}"
            host_port_text = f"Port: {bridge_port}" if bridge_port else "Port: —"
        host_ping1 = _IND_GREEN if host_ping_fresh and bridge_connected else (_IND_RED if bridge_connected else _IND_OFF)
        host_ping2 = _IND_GREEN if (ext_logic_connected and ext_ping_fresh) else (_IND_RED if ext_logic_connected else _IND_OFF)

        # ── C++ / GDB ────────────────────────────────────────────────────────
        gdb_proc = cpp_gdb_state.get("proc")
        gdb_status = str(cpp_gdb_state.get("status", "") or "")
        if not gdb_proc:
            cpp_dot1, cpp_lbl1 = _IND_RED, "GDB"
            cpp_dot2, cpp_lbl2 = _IND_OFF, "Target"
            cpp_port_text = "PID: —"
        elif gdb_proc.poll() is not None:
            cpp_dot1, cpp_lbl1 = _IND_RED, "GDB"
            cpp_dot2, cpp_lbl2 = _IND_RED, "Target exited"
            cpp_port_text = f"PID: {gdb_proc.pid}"
        elif "waiting for gdb prompt" in gdb_status.lower() or "launch" in gdb_status.lower():
            cpp_dot1, cpp_lbl1 = _IND_ORANGE, "GDB"
            cpp_dot2, cpp_lbl2 = _IND_ORANGE, "Target"
            cpp_port_text = f"PID: {gdb_proc.pid}"
        else:
            cpp_dot1, cpp_lbl1 = _IND_GREEN, "GDB"
            cpp_dot2, cpp_lbl2 = _IND_GREEN, "Target running" if cpp_gdb_state.get("running") else "Target stopped"
            cpp_port_text = f"PID: {gdb_proc.pid}"

        # ── Python ────────────────────────────────────────────────────────────
        py_started = python_dbg_state.get("started", False)
        py_port = args.ai_rpc_port if hasattr(args, "ai_rpc_port") and args.ai_rpc_port else None
        if not py_started:
            py_dot1, py_lbl1 = _IND_RED,   "Not started"
            py_dot2, py_lbl2 = _IND_OFF,   "—"
        else:
            py_dot1, py_lbl1 = _IND_GREEN, "Started"
            py_dot2, py_lbl2 = _IND_GREEN, "Streaming active"
        py_port_text = f"Port: {py_port}" if py_port else "Port: —"

        try:
            client.set_text("bottom.status.epa.port",    epa_port_text)
            client.set_text("bottom.status.host.port",   host_port_text)
            client.set_text("bottom.status.cpp.port",    cpp_port_text)
            client.set_text("bottom.status.python.port", py_port_text)
        except Exception:
            pass

        _update_status_panel_dot(client, "epa",    epa_dot1,  epa_lbl1,  epa_dot2,  epa_lbl2)
        _update_status_panel_dot(client, "host",   host_dot1, host_lbl1, host_dot2, host_lbl2)
        _update_host_status_ping(client, host_ping1, host_ping2)
        _update_status_panel_dot(client, "cpp",    cpp_dot1,  cpp_lbl1,  cpp_dot2,  cpp_lbl2)
        _update_status_panel_dot(client, "python", py_dot1,   py_lbl1,   py_dot2,   py_lbl2)

    def _run_search(client, query: str):
        project_root = app_state.get("project_root", "")
        if not query.strip() or not project_root:
            try:
                client.replace_list_items("nav.search.results", [])
            except Exception:
                pass
            return
        import subprocess
        try:
            result = subprocess.run(
                ["grep", "-rn", "--include=*.e", "--include=*.cpp",
                 "--include=*.h", "--include=*.py", "-m", "5",
                 "-F", query, project_root],
                capture_output=True, text=True, timeout=10
            )
            raw_lines = result.stdout.splitlines()[:200]
        except Exception:
            raw_lines = []
        items = []
        for raw in raw_lines:
            parts = raw.split(":", 2)
            if len(parts) >= 3:
                file_path, lineno, text = parts[0], parts[1], parts[2].strip()
                rel = file_path[len(project_root):].lstrip("/")
                label = f"{rel}:{lineno}  {text[:60]}"
                items.append({"id": f"search.result.{file_path}:{lineno}", "label": label})
        try:
            client.replace_list_items("nav.search.results", items)
        except Exception:
            pass

    def _tab_id_for_path(path: str) -> str:
        import hashlib
        return "tab." + hashlib.md5(path.encode()).hexdigest()[:8]

    def _ext_for_path(path: str) -> str:
        return Path(path).suffix.lower()

    def _open_file_tab(client, file_path: str, make_permanent: bool = False):
        tab_id = _tab_id_for_path(file_path)
        ext = _ext_for_path(file_path)
        title = Path(file_path).name

        existing = next((t for t in tab_list if t["path"] == file_path), None)
        if existing:
            if make_permanent and existing["preview"]:
                existing["preview"] = False
            app_state["active_editor_tab"] = existing["tab_id"]
            try:
                client.fire("ui.setActiveTab", {"target": "editor.tabs", "index": existing["index"]})
                client.set_visible_batch([("editor.welcome", False), ("editor.tabs", True)])
            except Exception:
                pass
            _persist_editor_session_state()
            return

        preview_entry = next((t for t in tab_list if t["preview"]), None)
        if preview_entry and not make_permanent:
            insert_index = preview_entry["index"]
            try:
                client.fire("ui.removeTab", {"target": "editor.tabs", "index": insert_index})
            except Exception:
                pass
            tab_list.remove(preview_entry)
            if tab_id in editor_state:
                del editor_state[tab_id]
            for t in tab_list:
                if t["index"] >= insert_index:
                    t["index"] -= 1
        else:
            insert_index = len(tab_list)

        try:
            source_text = Path(file_path).read_text(encoding="utf-8", errors="replace")
        except Exception:
            source_text = ""

        is_preview = not make_permanent
        close_action = f"tab.close.{tab_id}"

        if _editor_language_for_path(file_path) == "cpp":
            tab_ui = UiDocumentBuilder()
            tab_ui.create_tabs("editor.tabs")
            _create_cpp_tab(tab_ui, tab_id, title, source_text)
            child_json = tab_ui.widget_json(tab_id + ".container", indent=None)
        elif ext == ".py":
            tab_ui = UiDocumentBuilder()
            tab_ui.create_tabs("editor.tabs")
            _create_python_tab(tab_ui, tab_id, title, source_text)
            child_json = tab_ui.widget_json(tab_id + ".container", indent=None)
        elif ext == ".e":
            tab_ui = UiDocumentBuilder()
            tab_ui.create_tabs("editor.tabs")
            _create_e_tab(tab_ui, tab_id, title, source_text)
            child_json = tab_ui.widget_json(tab_id + ".container", indent=None)
            editor_state[tab_id] = {
                "title": title,
                "source_text": source_text,
                "epa_text": "",
                "epa_block_map": {},
                "epa_map_path": "",
                "view": "e",
                "compile_error": "",
                "compile_seq": 0,
                "trace_nodes": None,
                "ghs_nodes": _parse_tree_lines("", "GHS Layout", f"{tab_id}.debug_ghs.root"),
                "stack_nodes": _parse_tree_lines("", "Stack Interpretation", f"{tab_id}.debug_stack.root"),
                "local_nodes": _parse_tree_lines("", "Local Arena", f"{tab_id}.debug_local.root"),
                "dynamic_nodes": _parse_tree_lines("", "Dynamic Memory", f"{tab_id}.debug_dynamic.root"),
                "available_types": [],
                "available_workers": [],
                "undo_stack": [],
                "redo_stack": [],
                "last_undo_time": 0.0,
                "in_undo_redo": False,
            }
            _load_history(tab_id)
        else:
            tab_ui = UiDocumentBuilder()
            tab_ui.create_code_editor(tab_id + ".container", source_text)
            tab_ui.set_property_number(tab_id + ".container", "font_size", 13)
            tab_ui.set_property_string(tab_id + ".container", "language", _editor_language_for_path(file_path))
            child_json = tab_ui.widget_json(tab_id + ".container", indent=None)

        try:
            client.call("ui.addTab", {
                "target": "editor.tabs",
                "title": title,
                "button_glyph": "x",
                "button_action": close_action,
                "child": child_json,
            })
            tab_list.insert(insert_index, {
                "tab_id": tab_id,
                "path": file_path,
                "index": insert_index,
                "preview": is_preview,
            })
            app_state["active_editor_tab"] = tab_id
            for t in tab_list:
                if t is not tab_list[insert_index] and t["index"] >= insert_index:
                    t["index"] += 1
            client.fire("ui.setActiveTab", {"target": "editor.tabs", "index": insert_index})
            client.set_visible_batch([("editor.welcome", False), ("editor.tabs", True)])
            if ext == ".e":
                _refresh_e_tab(client, tab_id)
                _cpp_gdb_refresh_ui(client)
            _focus_editor_widget(client, tab_id, editor_state.get(tab_id))
            _persist_editor_session_state()
        except Exception:
            pass

    def _restore_editor_session_state(client):
        state = _load_ide_state()
        last_project = str(state.get("last_project", "") or "") if isinstance(state, dict) else ""
        if last_project and Path(last_project).is_dir():
            _open_project(client, last_project, restore_session=True)

        if app_state.get("project_root"):
            _switch_nav_view(client, app_state.get("nav_view", "files"))
            _set_bottom_view(client, app_state.get("bottom_view", "build"))
        if tab_list:
            try:
                client.set_visible_batch([("editor.welcome", False), ("editor.tabs", True)])
            except Exception:
                pass
        _persist_editor_session_state()

    def _ai_format_history(extra_assistant_text=None) -> str:
        """Return JSON-encoded messages for the chat dialog widget."""
        if not ai_state["messages"] and extra_assistant_text is None:
            return json.dumps({"messages": [{"role": "assistant", "display": (
                "How can I help you build today?\n\n"
                "I can help you with E language, EPA assembly, kernel design, "
                "C++ host integration, and project structure.\n\n"
                "Tip: toggle File below to include your open file as context."
            )}]})
        msgs = []
        for msg in ai_state["messages"]:
            if msg["role"] == "user":
                display = msg["content"].split("\n\n---\n\n")[0]
                msgs.append({"role": "user", "display": display})
            else:
                msgs.append({"role": "assistant", "display": msg["content"]})
        if extra_assistant_text is not None:
            msgs.append({"role": "assistant", "display": extra_assistant_text or "..."})
        return json.dumps({"messages": msgs})

    def _ai_build_context() -> str:
        """Return a context block to append to the API user message."""
        sections = []
        if ai_state.get("ctx_file"):
            tid = app_state.get("active_editor_tab", "")
            if tid and tid in editor_state:
                st = editor_state[tid]
                title = st.get("title", "file")
                src = st.get("source_text", "")
                if src:
                    ext = Path(title).suffix.lower()
                    lang = {".e": "e", ".cpp": "cpp", ".h": "cpp", ".py": "python"}.get(ext, "")
                    sections.append(f"Current file: `{title}`\n\n```{lang}\n{src}\n```")
        if ai_state.get("ctx_project"):
            name = app_state.get("project_name", "")
            root = app_state.get("project_root", "")
            if name:
                sections.append(f"Project: **{name}**\nPath: `{root}`")
        return "\n\n".join(sections)

    def _ai_model_provider(model_id: str) -> str:
        return "codex" if str(model_id or "").startswith("codex:") else "claude"

    def _ai_model_name(model_id: str) -> str:
        model_id = str(model_id or "")
        return model_id.split(":", 1)[1] if model_id.startswith("codex:") else model_id

    def _persist_ai_config():
        _save_ide_state({
            "ai": {
                "model": ai_state.get("model", "codex:gpt-5"),
                "codex_cli": ai_state.get("codex_cli", "codex"),
                "codex_args": ai_state.get("codex_args", ""),
            }
        })

    def _apply_ai_config_ui(client):
        try:
            client.set_text("ai.model", ai_state.get("model", "codex:gpt-5"))
            client.set_text("ai.codex_cli", ai_state.get("codex_cli", "codex"))
            client.set_text("ai.codex_args", ai_state.get("codex_args", ""))
        except Exception:
            pass

    def _ai_codex_argv(prompt: str) -> tuple[list[str], str, bool]:
        cli_text = str(ai_state.get("codex_cli", "codex") or "codex").strip()
        arg_template = str(ai_state.get("codex_args", "") or "").strip()
        base = shlex.split(cli_text) if cli_text else ["codex"]
        cwd = str(app_state.get("project_root", "") or Path.cwd())
        model_name = _ai_model_name(ai_state.get("model", "codex:gpt-5"))
        replacements = {
            "{cwd}": cwd,
            "{model}": model_name,
            "{prompt}": prompt,
        }
        args = []
        prompt_inserted = False
        for part in shlex.split(arg_template):
            value = part
            for key, replacement in replacements.items():
                if key in value:
                    value = value.replace(key, replacement)
            if "{prompt}" in part:
                prompt_inserted = True
            args.append(value)
        if not prompt_inserted:
            use_stdin = "-" in args
            if not use_stdin:
                args.append(prompt)
        else:
            use_stdin = False
        return base + args, cwd, use_stdin

    def _ai_codex_prompt(api_content: str) -> str:
        history = []
        for msg in ai_state["messages"]:
            role = "User" if msg["role"] == "user" else "Assistant"
            history.append(f"{role}:\n{msg['content']}")
        return (
            f"{ANTHROPIC_SYSTEM_PROMPT}\n\n"
            "You are running from the EPA-IDE right sidebar via the Codex CLI. "
            "Answer the latest user request directly and concisely.\n\n"
            + "\n\n".join(history)
        )

    def _ai_stream_codex(prompt: str, cancel_event: threading.Event, update_response) -> str:
        argv, cwd, use_stdin = _ai_codex_argv(prompt)
        response_text = ""
        try:
            proc = subprocess.Popen(
                argv,
                cwd=cwd if Path(cwd).is_dir() else None,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                stdin=subprocess.PIPE if use_stdin else subprocess.DEVNULL,
                text=True,
                bufsize=1,
            )
            ai_state["process"] = proc
            if use_stdin and proc.stdin is not None:
                proc.stdin.write(prompt)
                proc.stdin.close()
        except FileNotFoundError:
            return (
                f"Codex CLI was not found: `{argv[0]}`\n\n"
                "Set the Codex path in the sidebar, for example an absolute path to the `codex` binary."
            )
        except Exception as exc:
            return f"Failed to launch Codex CLI: {exc}"

        last_update = time.monotonic()
        try:
            assert proc.stdout is not None
            while True:
                if cancel_event.is_set():
                    response_text += "\n\n[stopped]"
                    try:
                        proc.terminate()
                    except Exception:
                        pass
                    break
                chunk = proc.stdout.readline()
                if chunk:
                    response_text += chunk
                    now = time.monotonic()
                    if now - last_update >= 0.08:
                        update_response(response_text)
                        last_update = now
                    continue
                if proc.poll() is not None:
                    break
                time.sleep(0.03)
            rc = proc.wait(timeout=2.0)
            if rc != 0 and "[stopped]" not in response_text:
                response_text += f"\n\n[codex exited with status {rc}]"
        except Exception as exc:
            response_text += f"\n\nCodex CLI stream failed: {exc}"
        finally:
            ai_state["process"] = None
        return response_text.strip() or "(Codex returned no output.)"

    def _ai_send():
        """Background thread: send user message to the selected AI provider and stream response."""
        message_text = ai_state.get("input_text", "").strip()
        if not message_text:
            return
        c = client_ref.get("client")
        if not c:
            return

        context = _ai_build_context()
        api_content = f"{message_text}\n\n---\n\n{context}" if context else message_text
        ai_state["messages"].append({"role": "user", "content": api_content})
        ai_state["input_text"] = ""

        cancel_event = threading.Event()
        ai_state["cancel_event"] = cancel_event
        ai_state["generating"] = True

        try:
            c.set_text("ai.history", _ai_format_history(extra_assistant_text=""))
            c.set_text("ai.input", "")
            c.fire("ui.setVisible", {"target": "ai.stop", "visible": True})
            c.fire("ui.setVisible", {"target": "ai.send", "visible": False})
        except Exception:
            pass

        response_text = ""
        try:
            def _update_response(text: str):
                try:
                    c.set_text("ai.history", _ai_format_history(extra_assistant_text=text))
                except Exception:
                    pass

            if _ai_model_provider(ai_state.get("model", "")) == "codex":
                response_text = _ai_stream_codex(_ai_codex_prompt(api_content), cancel_event, _update_response)
            else:
                import anthropic as _ant
                aclient = _ant.Anthropic()
                last_update = time.monotonic()
                with aclient.messages.stream(
                    model=ai_state.get("model", "claude-sonnet-4-6"),
                    max_tokens=4096,
                    system=ANTHROPIC_SYSTEM_PROMPT,
                    messages=[{"role": m["role"], "content": m["content"]}
                              for m in ai_state["messages"]],
                ) as stream:
                    for chunk in stream.text_stream:
                        if cancel_event.is_set():
                            response_text += "\n\n[stopped]"
                            break
                        response_text += chunk
                        now = time.monotonic()
                        if now - last_update >= 0.08:
                            _update_response(response_text)
                            last_update = now
        except ImportError:
            response_text = (
                "The `anthropic` library is not installed.\n\n"
                "Run `pip install anthropic` and set the `ANTHROPIC_API_KEY` "
                "environment variable, then restart the IDE."
            )
        except Exception as exc:
            response_text = f"Error communicating with the API: {exc}"

        ai_state["messages"].append({"role": "assistant", "content": response_text})
        ai_state["generating"] = False
        ai_state["cancel_event"] = None

        try:
            c.set_text("ai.history", _ai_format_history())
            c.fire("ui.setVisible", {"target": "ai.stop", "visible": False})
            c.fire("ui.setVisible", {"target": "ai.send", "visible": True})
        except Exception:
            pass

    _deferred_pool = concurrent.futures.ThreadPoolExecutor(max_workers=8, thread_name_prefix="deferred")

    def _deferred(fn):
        """Submit fn to the worker pool so on_ui_event returns before any RPC calls are made."""
        _deferred_pool.submit(fn)

    def _register_event_responders(client):
        """Register cached C++ event responses. Called at connect and after theme changes."""
        try:
            # Python does not handle hoverChanged events — suppress the notification
            # entirely so no Python threads are created for every mouse movement.
            # The server handles hover highlighting natively in the GTK draw loop.
            client.set_event_notify("hoverChanged", "", False)
        except Exception:
            pass

    def on_ui_event(params):
        client = client_ref.get("client")
        action = params.get("action")
        payload = params.get("payload") or {}
        payload_action = payload.get("action", "") if isinstance(payload, dict) else ""
        item_action = payload_action if isinstance(payload_action, str) else ""
        target = params.get("target")
        _push_event("ui_event", action=action, target=target,
                    payload=_trim_for_log(payload) if isinstance(payload, dict) else payload)
        if (
            action == "valueChanged"
            and isinstance(target, str)
            and target.startswith("nav.debug.kernel.")
            and target.endswith(".debug")
        ):
            _epa_dbg_log(f"[raw-valueChanged] target={target} payload={payload}")

        if action == "valueChanged" and target == "editor.tabs" and client is not None:
            new_index = int(payload.get("value", -1))
            entry = next((t for t in tab_list if t.get("index") == new_index), None)
            if entry:
                tid = entry.get("tab_id", "")
                app_state["active_editor_tab"] = tid
                _persist_editor_session_state()
                c = client
                st = editor_state.get(tid, {})
                _deferred(lambda t=tid, s=st: _focus_editor_widget(c, t, s))
            return {"received": True}

        for tab_id, state in editor_state.items():
            ids = _editor_ids(tab_id)
            if action == "textChanged" and target == ids["source"]:
                app_state["active_editor_tab"] = tab_id
                prev_text = state.get("source_text", "")
                new_text = payload.get("text", "")
                if prev_text != new_text:
                    # Find edit position via suffix match between old and new text.
                    # This correctly places cursor at the edit point for any edit location.
                    new_caret = payload.get("caret", 0)
                    if new_caret > 0:
                        delta = len(new_text) - len(prev_text)
                        undo_caret = max(0, min(new_caret - delta, len(prev_text)))
                    else:
                        j = 0
                        min_len = min(len(prev_text), len(new_text))
                        while j < min_len and prev_text[-(j + 1)] == new_text[-(j + 1)]:
                            j += 1
                        undo_caret = max(0, len(prev_text) - j)
                    state.setdefault("undo_stack", []).append({"text": prev_text, "caret": undo_caret})
                    if len(state["undo_stack"]) > 100:
                        state["undo_stack"] = state["undo_stack"][-100:]
                    state["redo_stack"] = []
                state["source_text"] = new_text
                state["compile_seq"] = int(state.get("compile_seq", 0)) + 1
                if client is not None:
                    c = client
                    current_tab = tab_id
                    seq = state["compile_seq"]
                    _deferred(lambda: _refresh_e_tab(c, current_tab, seq))
                return {"received": True}
        # Track technology checkbox toggles from the wizard.
        if action == "valueChanged" and target in (
            "wizard.tech.epa", "wizard.tech.cpp", "wizard.tech.python"
        ):
            key = "tech_" + target.rsplit(".", 1)[-1]
            wizard_state[key] = payload.get("value", 0) > 0.5

        if action == "action" and target == "app.toolbar" and client is not None:
            if not app_state.get("project_root"):
                return {"received": True}
            btn = payload.get("action", "")
            view_map = {
                "toolbar.files":  "files",
                "toolbar.search": "search",
                "toolbar.repo":   "repo",
                "toolbar.issues": "issues",
                "toolbar.debug":  "debug",
            }
            if btn in view_map:
                c = client
                v = view_map[btn]
                _deferred(lambda: _switch_nav_view(c, v))
                return {"received": True}

        if action == "action" and target == "bottom.toolbar" and client is not None:
            btn = payload.get("action", "")
            c = client
            if btn == "bottom.build":
                _deferred(lambda: _set_bottom_view(c, "build"))
                return {"received": True}
            if btn == "bottom.host_io":
                _deferred(lambda: _set_bottom_view(c, "host_io"))
                return {"received": True}
            if btn == "bottom.console":
                _deferred(lambda: _set_bottom_view(c, "console"))
                return {"received": True}
            if btn == "bottom.terminal":
                _deferred(lambda: _set_bottom_view(c, "terminal"))
                return {"received": True}
            if btn == "bottom.status":
                _deferred(lambda: _set_bottom_view(c, "status"))
                return {"received": True}
            if btn == "bottom.clear":
                def _clear_bottom():
                    try:
                        view = str(app_state.get("bottom_view", "build") or "build")
                        if view == "host_io":
                            app_state["bottom_host_io_output"] = ""
                            c.set_text("bottom.host_io_output", "")
                        elif view == "console":
                            console_state["output"] = ""
                            c.set_text("bottom.console_output", "")
                        elif view == "terminal":
                            pass  # terminal widget manages its own content
                        else:
                            app_state["bottom_build_output"] = ""
                            c.set_text("bottom.build_output", "")
                    except Exception:
                        pass
                _deferred(_clear_bottom)
                return {"received": True}

        if action == "keysTyped" and target == "bottom.console_input":
            console_state["input"] = console_state.get("input", "") + payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "bottom.console_input":
            console_state["input"] = payload.get("text", "")
            return {"received": True}

        if action == "keyDown" and target == "bottom.console_input" and client is not None:
            keyval = payload.get("keyval", 0)
            # Backspace: trim the tracked input manually (keysTyped doesn't fire for backspace)
            if keyval in (0xff08, 65288, 8):
                cur = console_state.get("input", "")
                console_state["input"] = cur[:-1] if cur else ""
                return {"received": True}
            if keyval in (0xff0d, 0x0000ff0d, 65293, 13):
                command = console_state.get("input", "")
                console_state["input"] = ""
                c = client
                def _submit_console():
                    try:
                        c.set_text("bottom.console_input", "")
                    except Exception:
                        pass
                    _run_console_command(c, command)
                _deferred(_submit_console)
                return {"received": True}
            # Up arrow — history navigation
            if keyval in (0xff52, 65362):
                hist = console_state.get("history", [])
                if hist:
                    pos = console_state.get("history_pos", -1)
                    if pos == -1:
                        pos = len(hist) - 1
                    elif pos > 0:
                        pos -= 1
                    console_state["history_pos"] = pos
                    entry = hist[pos]
                    console_state["input"] = entry
                    c = client
                    _deferred(lambda e=entry: c.set_text("bottom.console_input", e))
                return {"received": True}
            # Down arrow — history navigation
            if keyval in (0xff54, 65364):
                hist = console_state.get("history", [])
                pos = console_state.get("history_pos", -1)
                if pos >= 0 and pos < len(hist) - 1:
                    pos += 1
                    console_state["history_pos"] = pos
                    entry = hist[pos]
                    console_state["input"] = entry
                    c = client
                    _deferred(lambda e=entry: c.set_text("bottom.console_input", e))
                else:
                    console_state["history_pos"] = -1
                    console_state["input"] = ""
                    c = client
                    _deferred(lambda: c.set_text("bottom.console_input", ""))
                return {"received": True}

        if action == "textChanged" and target == "nav.search.input" and client is not None:
            query = payload.get("text", "")
            c = client
            _deferred(lambda q=query: _run_search(c, q))
            return {"received": True}

        if action == "action" and payload.get("action") == "app.open_full_terminal":
            _open_full_terminal()
            return {"received": True}

        if action == "action" and payload.get("action") == "repo.refresh" and client is not None:
            c = client
            _deferred(lambda: _refresh_repo_panel(c))
            return {"received": True}

        if action == "action" and payload.get("action") == "repo.stage_all" and client is not None:
            project_root = app_state.get("project_root", "")
            if project_root:
                import subprocess
                c = client
                def _do_stage_all():
                    try:
                        subprocess.run(["git", "-C", project_root, "add", "-A"],
                                       timeout=10, capture_output=True)
                    except Exception:
                        pass
                    _refresh_repo_panel(c)
                _deferred(_do_stage_all)
            return {"received": True}

        if action == "action" and payload.get("action") == "repo.commit" and client is not None:
            project_root = app_state.get("project_root", "")
            if project_root:
                import subprocess
                c = client
                commit_msg = app_state.get("repo_commit_msg", "").strip()
                def _do_commit(msg=commit_msg):
                    if not msg:
                        return
                    try:
                        subprocess.run(
                            ["git", "-C", project_root, "commit", "-m", msg],
                            timeout=15, capture_output=True
                        )
                        app_state["repo_commit_msg"] = ""
                        try:
                            c.set_text("nav.repo.commit_msg", "")
                        except Exception:
                            pass
                    except Exception:
                        pass
                    _refresh_repo_panel(c)
                _deferred(_do_commit)
            return {"received": True}

        if action == "textChanged" and target == "nav.repo.commit_msg":
            app_state["repo_commit_msg"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev.commit_msg":
            ev_state["commit_msg"] = payload.get("text", "")
            return {"received": True}

        if action == "action" and payload.get("action") == "ev.refresh" and client is not None:
            c = client
            _deferred(lambda: _refresh_ev_panel(c))
            return {"received": True}

        if action == "action" and payload.get("action") == "ev.commit" and client is not None:
            msg = ev_state.get("commit_msg", "").strip()
            c = client
            def _do_ev_commit(m=msg):
                repo = _ev_repo()
                if not repo or not m:
                    return
                try:
                    repo.commit(m)
                    ev_state["commit_msg"] = ""
                    try:
                        c.set_text("nav.ev.commit_msg", "")
                    except Exception:
                        pass
                except Exception as exc:
                    print(f"[ev.commit] {exc}", flush=True)
                _refresh_ev_panel(c)
            _deferred(_do_ev_commit)
            return {"received": True}

        if action == "action" and payload.get("action") == "ev.push" and client is not None:
            c = client
            def _do_ev_push():
                repo = _ev_repo()
                if not repo:
                    return
                try:
                    result = repo.push_current_tree()
                    print(json.dumps({"ev.push": result}, indent=2), flush=True)
                except Exception as exc:
                    print(f"[ev.push] {exc}", flush=True)
                _refresh_ev_panel(c)
            _deferred(_do_ev_push)
            return {"received": True}

        if action == "action" and payload.get("action") == "ev.pull" and client is not None:
            c = client
            def _do_ev_pull():
                repo = _ev_repo()
                if not repo:
                    return
                try:
                    result = repo.pull_current_tree()
                    print(json.dumps({"ev.pull": result}, indent=2), flush=True)
                except Exception as exc:
                    print(f"[ev.pull] {exc}", flush=True)
                _refresh_ev_panel(c)
            _deferred(_do_ev_pull)
            return {"received": True}

        if action == "action" and payload.get("action") == "issues.refresh" and client is not None:
            c = client
            _deferred(lambda: _refresh_issues_panel(c))
            return {"received": True}

        if action == "action" and payload.get("action", "").startswith("issues.filter.") and client is not None:
            filt = payload.get("action")[len("issues.filter."):]
            ev_bugs_state["filter"] = filt
            c = client
            _deferred(lambda: c.replace_list_items("nav.issues.list", _issues_list_items()))
            return {"received": True}

        if action == "action" and payload.get("action") == "issues.add" and client is not None:
            title = ev_bugs_state.get("new_title", "").strip()
            if title:
                repo = _ev_repo()
                if repo:
                    try:
                        import getpass
                        author = getpass.getuser()
                    except Exception:
                        author = "user"
                    try:
                        repo.create_bug(title, severity="medium", author=author)
                    except Exception:
                        pass
                ev_bugs_state["new_title"] = ""
                c = client
                def _after_add():
                    try:
                        c.set_text("nav.issues.new_title", "")
                        c.replace_list_items("nav.issues.list", _issues_list_items())
                    except Exception:
                        pass
                _deferred(_after_add)
            return {"received": True}

        if action == "textChanged" and target == "nav.issues.new_title":
            ev_bugs_state["new_title"] = payload.get("text", "")
            return {"received": True}

        if action in ("action", "clicked") and target == "nav.issues.list":
            sel = payload.get("action") or payload.get("id", "")
            if sel.startswith("issue."):
                ev_bugs_state["selected_id"] = sel[len("issue."):]
            return {"received": True}

        if action == "action" and payload.get("action") in ("issues.close", "issues.reopen") and client is not None:
            sel_id = ev_bugs_state.get("selected_id")
            if sel_id and not sel_id.startswith("issues."):
                repo = _ev_repo()
                if repo:
                    new_status = "closed" if payload.get("action") == "issues.close" else "open"
                    try:
                        repo.update_bug(sel_id, status=new_status)
                    except Exception:
                        pass
                c = client
                _deferred(lambda: c.replace_list_items("nav.issues.list", _issues_list_items()))
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_setup.name":
            ev_setup_state["name"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_setup.server":
            ev_setup_state["server"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_setup.remote_root":
            ev_setup_state["remote_root"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_setup.branch":
            ev_setup_state["branch"] = payload.get("text", "") or "main"
            return {"received": True}

        if action in ("action",) and payload.get("action") in ("ev.setup.init", "ev.tab.setup.init") and client is not None:
            from_tab = payload.get("action") == "ev.tab.setup.init"
            error_target = "nav.ev_tab_setup.error" if from_tab else "nav.ev_setup.error"
            if from_tab:
                name        = ev_setup_state.get("tab_name",        ev_setup_state.get("name", "")).strip()
                server      = ev_setup_state.get("tab_server",      ev_setup_state.get("server", "")).strip()
                remote_root = ev_setup_state.get("tab_remote_root", ev_setup_state.get("remote_root", "")).strip()
                branch      = (ev_setup_state.get("tab_branch",     ev_setup_state.get("branch", "main")).strip() or "main")
            else:
                name        = ev_setup_state.get("name", "").strip()
                server      = ev_setup_state.get("server", "").strip()
                remote_root = ev_setup_state.get("remote_root", "").strip()
                branch      = ev_setup_state.get("branch", "main").strip() or "main"
            project_root = app_state.get("project_root", "")
            c = client
            if not project_root:
                return {"received": True}
            if not name:
                _deferred(lambda: c.set_text(error_target, "Project name is required."))
                return {"received": True}
            if not _EV_AVAILABLE or ProjectRepo is None:
                _deferred(lambda: c.set_text(error_target, "EV module not available."))
                return {"received": True}
            err_tgt = error_target
            def _do_init():
                try:
                    ProjectRepo.init_project(
                        Path(project_root),
                        name=name,
                        server=server,
                        remote_root=remote_root or "/",
                        branch=branch,
                    )
                    _refresh_issues_panel(c)
                    _refresh_ev_panel(c)
                except Exception as exc:
                    try:
                        c.set_text(err_tgt, str(exc)[:100])
                    except Exception:
                        pass
            _deferred(_do_init)
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_tab_setup.name":
            ev_setup_state["tab_name"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_tab_setup.server":
            ev_setup_state["tab_server"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_tab_setup.remote_root":
            ev_setup_state["tab_remote_root"] = payload.get("text", "")
            return {"received": True}

        if action == "textChanged" and target == "nav.ev_tab_setup.branch":
            ev_setup_state["tab_branch"] = payload.get("text", "") or "main"
            return {"received": True}

        if action in ("action", "clicked") and target == "nav.search.results" and client is not None:
            result_id = payload.get("action") or payload.get("id", "")
            if result_id.startswith("search.result."):
                loc = result_id[len("search.result."):]
                parts = loc.rsplit(":", 1)
                file_path = parts[0]
                if Path(file_path).is_file():
                    c = client
                    fp = file_path
                    _deferred(lambda: _open_file_tab(c, fp, True))
            return {"received": True}

        if action == "valueChanged" and target == "wizard.python.multi_cpu":
            wizard_state["python_multi_cpu"] = payload.get("value", 0) > 0.5

        if action == "valueChanged" and target == "wizard.cpp.epa_vm_host":
            wizard_state["cpp_epa_vm_host"] = payload.get("value", 0) > 0.5

        if action == "valueChanged" and target == "wizard.cpp.epa_debug_rpc":
            wizard_state["cpp_epa_debug_rpc"] = payload.get("value", 0) > 0.5

        if target == "cpp_tech.host_kind" and action in ("action", "valueChanged", "clicked"):
            selected = payload.get("action") or payload.get("id") or payload.get("text") or ""
            if selected in ("vulkan-surface", "tabbed-control-panel", "rich-editor"):
                cpp_tech_state["ui_template"] = selected

        if action == "valueChanged" and target == "cpp_tech.epa_vm_host":
            cpp_tech_state["cpp_epa_vm_host"] = payload.get("value", 0) > 0.5

        if action == "valueChanged" and target == "cpp_tech.epa_debug_rpc":
            cpp_tech_state["cpp_epa_debug_rpc"] = payload.get("value", 0) > 0.5

        if target in ("wizard.ui_client", "wizard.ui_template") and action in ("action", "valueChanged", "clicked"):
            selected = payload.get("action") or payload.get("id") or payload.get("text") or ""
            if target == "wizard.ui_client" and selected in ("both", "cpp", "python"):
                wizard_state["ui_client"] = selected
                if selected == "cpp":
                    wizard_state["tech_cpp"] = True
                    wizard_state["tech_python"] = False
                elif selected == "python":
                    wizard_state["tech_cpp"] = False
                    wizard_state["tech_python"] = True
                else:
                    wizard_state["tech_cpp"] = True
                    wizard_state["tech_python"] = True
            if target == "wizard.ui_template" and selected in ("tabbed-control-panel", "rich-editor", "vulkan-surface"):
                wizard_state["ui_template"] = selected

        if action == "textChanged" and target in ("wizard.project_name", "wizard.rpc_host", "wizard.rpc_port"):
            key = {
                "wizard.project_name": "project_name",
                "wizard.rpc_host": "rpc_host",
                "wizard.rpc_port": "rpc_port",
            }[target]
            wizard_state[key] = payload.get("text", "")

        # Track project name typed into the wizard text input.
        if action == "keysTyped" and target == "wizard.project_name":
            wizard_state["project_name"] = wizard_state.get("project_name", "") + payload.get("text", "")

        if action == "keyDown" and target == "wizard.project_name":
            if payload.get("keyval", 0) == 0xff08:  # backspace
                name = wizard_state.get("project_name", "")
                if name:
                    wizard_state["project_name"] = name[:-1]

        if action == "keysTyped" and target in ("wizard.rpc_host", "wizard.rpc_port"):
            key = "rpc_host" if target == "wizard.rpc_host" else "rpc_port"
            wizard_state[key] = wizard_state.get(key, "") + payload.get("text", "")

        if action == "keyDown" and target in ("wizard.rpc_host", "wizard.rpc_port"):
            if payload.get("keyval", 0) == 0xff08:
                key = "rpc_host" if target == "wizard.rpc_host" else "rpc_port"
                value = wizard_state.get(key, "")
                if value:
                    wizard_state[key] = value[:-1]

        # Track filename typed into the new-file dialog.
        if action == "keysTyped" and target == "new_file.filename":
            new_file_state["filename"] = new_file_state.get("filename", "") + payload.get("text", "")

        if action == "keyDown" and target == "new_file.filename":
            if payload.get("keyval", 0) == 0xff08:  # backspace
                name = new_file_state.get("filename", "")
                if name:
                    new_file_state["filename"] = name[:-1]

        if action == "keysTyped" and target == "new_file.new_folder_name":
            new_file_state["new_folder_name"] = new_file_state.get("new_folder_name", "") + payload.get("text", "")

        if action == "keyDown" and target == "new_file.new_folder_name":
            if payload.get("keyval", 0) == 0xff08:  # backspace
                name = new_file_state.get("new_folder_name", "")
                if name:
                    new_file_state["new_folder_name"] = name[:-1]

        # Open-file dialog: double-click a directory to navigate into it.
        if target == "dialog.files" and action == "action":
            entry_path = payload.get("action", "")
            if entry_path and Path(entry_path).is_dir() and client is not None:
                c = client
                p = entry_path
                _deferred(lambda: _open_file_navigate(c, p))
            return {"received": True}

        # Open-file dialog: track typed location bar changes.
        if action == "textChanged" and target == "dialog.location":
            typed = payload.get("text", "")
            if typed and Path(typed).is_dir():
                open_file_nav_state["path"] = typed

        # New-file dialog: double-click a folder to navigate into it.
        if target == "new_file.folder_list" and action in ("clicked", "action"):
            row_height = 23
            if action == "clicked":
                y = payload.get("y", -1.0)
                row_index = int(y / row_height) if y >= 0 else -1
                items = _folder_items(new_file_nav_state.get("path", str(Path.home())))
                row_value = items[row_index]["id"] if 0 <= row_index < len(items) else ""
                print(f"[folder_list] click row={row_index} value={row_value!r}", flush=True)
            elif action == "action":
                folder_path = payload.get("action", "")
                is_dir = Path(folder_path).is_dir() if folder_path else False
                print(f"[folder_list] double-click value={folder_path!r} is_dir={is_dir}", flush=True)
                if folder_path and is_dir and client is not None:
                    c = client
                    p = folder_path
                    print(f"[folder_list] navigating to {p!r}", flush=True)
                    _deferred(lambda: _new_file_navigate(c, p))
            return {"received": True}

        if target == "new_file.template_list" and action in ("clicked", "action"):
            template_items = _e_template_items()
            template_id = ""
            if action == "clicked":
                row_height = 23
                y = payload.get("y", -1.0)
                row_index = int(y / row_height) if y >= 0 else -1
                if 0 <= row_index < len(template_items):
                    template_id = template_items[row_index]["id"]
            else:
                template_id = payload.get("action", "")
            if template_id in E_FILE_TEMPLATES:
                new_file_state["template"] = template_id
                if client is not None:
                    c = client
                    summary = _e_template_summary(template_id)
                    _deferred(lambda: c.set_text("new_file.template_summary", summary))
            return {"received": True}

        # Tree view file click — single click = preview tab, double click = permanent tab.
        if action == "action" and target in ("nav.tree.epa", "nav.tree.cpp", "nav.tree.python", _NAV_3D_TREE_ID) and client is not None:
            node_path = payload.get("action", "")
            if node_path and Path(node_path).is_file():
                if target == _NAV_3D_TREE_ID and node_path.endswith(".e3d.json"):
                    c = client
                    np = node_path
                    _deferred(lambda: _open_artifact3d_viewer(c, np))
                if target == _NAV_3D_TREE_ID and node_path.endswith(".elp.json"):
                    c = client
                    np = node_path
                    _deferred(lambda: _open_level_path_viewer(c, np))
                now = time.monotonic()
                last = tab_click_state.get("path")
                last_t = tab_click_state.get("time", 0.0)
                is_double = (node_path == last and now - last_t < 0.5)
                tab_click_state["path"] = node_path
                tab_click_state["time"] = now
                c = client
                np = node_path
                perm = is_double
                _deferred(lambda: _open_file_tab(c, np, perm))
                return {"received": True}

        if target == "artifact.viewer.surface" and action in ("mouseDown", "mouseMove", "mouseUp") and client is not None:
            viewer_state = app_state.get("artifact3d_viewer", {})
            if not isinstance(viewer_state, dict):
                viewer_state = {}
                app_state["artifact3d_viewer"] = viewer_state
            camera = viewer_state.get("camera")
            if not isinstance(camera, dict):
                camera = {"yaw_deg": 18.0, "pitch_deg": 8.0, "distance": 4.8}
                viewer_state["camera"] = camera
            x = float(payload.get("x", -1.0) or -1.0)
            y = float(payload.get("y", -1.0) or -1.0)
            if action == "mouseDown":
                viewer_state["dragging"] = True
                viewer_state["last_xy"] = [x, y]
                return {"received": True}
            if action == "mouseUp":
                viewer_state["dragging"] = False
                viewer_state["last_xy"] = None
                return {"received": True}
            if action == "mouseMove" and viewer_state.get("dragging") and isinstance(viewer_state.get("last_xy"), list):
                last = viewer_state.get("last_xy")
                if x >= 0.0 and y >= 0.0 and isinstance(last, list) and len(last) >= 2:
                    dx = x - float(last[0])
                    dy = y - float(last[1])
                    if abs(dx) > 0.01 or abs(dy) > 0.01:
                        camera["yaw_deg"] = float(camera.get("yaw_deg", 18.0)) + dx * 0.35
                        camera["pitch_deg"] = max(-80.0, min(80.0, float(camera.get("pitch_deg", 8.0)) + dy * 0.25))
                        viewer_state["last_xy"] = [x, y]
                        c = client
                        _deferred(lambda: _refresh_artifact3d_viewer(c))
                return {"received": True}

        if target == "level_path.viewer.surface" and action in ("mouseDown", "mouseMove", "mouseUp", "mouseWheel") and client is not None:
            viewer_state = app_state.get("level_path_viewer", {})
            if not isinstance(viewer_state, dict):
                return {"received": True}
            doc = viewer_state.get("doc")
            if not isinstance(doc, dict):
                return {"received": True}
            raw_x = float(payload.get("x", -1.0) or -1.0)
            raw_y = float(payload.get("y", -1.0) or -1.0)
            x, y = _level_path_surface_event_to_virtual(client, raw_x, raw_y)
            editor_view_state = {
                "tool": str(viewer_state.get("tool", "select")),
                "pending_line": viewer_state.get("pending_line"),
                "view": str(viewer_state.get("view", "top")),
                "zoom": float(viewer_state.get("zoom", 52.0) or 52.0),
                "center": list(viewer_state.get("center", [0.0, 0.0])),
                "active_item": (viewer_state.get("drag") or {}).get("item"),
            }
            if action == "mouseWheel":
                delta = payload.get("delta_y")
                if delta is None:
                    delta = payload.get("dy")
                if delta is None:
                    delta = payload.get("delta")
                try:
                    delta_value = float(delta or 0.0)
                except Exception:
                    delta_value = 0.0
                if abs(delta_value) > 0.001:
                    zoom = float(viewer_state.get("zoom", 52.0) or 52.0)
                    scale = 0.92 if delta_value > 0.0 else 1.08
                    viewer_state["zoom"] = max(12.0, min(220.0, zoom * scale))
                    c = client
                    _deferred(lambda: _refresh_level_path_viewer(c))
                return {"received": True}
            if x < 0.0 or y < 0.0:
                return {"received": True}
            if action == "mouseDown":
                face = level_path_view_cube_hit_test(x, y)
                if face:
                    viewer_state["view"] = face
                    viewer_state["drag"] = {"kind": "view_cube", "start_xy": [x, y], "face_down": face}
                    c = client
                    _deferred(lambda: _refresh_level_path_viewer(c))
                    return {"received": True}
                if level_path_view_cube_contains(x, y):
                    viewer_state["drag"] = {"kind": "view_cube", "start_xy": [x, y], "face_down": None}
                    return {"received": True}
                hit_item = level_path_hit_test_item(doc, editor_view_state, x, y)
                if hit_item:
                    anchor_world = level_path_screen_to_world(doc, x, y, editor_view_state, anchor=hit_item.get("world"))
                    viewer_state["drag"] = {
                        "kind": "move_item",
                        "item": hit_item,
                        "anchor_world": list(anchor_world),
                        "initial_world": list(hit_item.get("world", [0.0, 0.0, 0.0])),
                    }
                    c = client
                    _deferred(lambda: _refresh_level_path_viewer(c))
                    return {"received": True}
                tool = str(viewer_state.get("tool", "select"))
                if tool in ("square", "cube", "line"):
                    world_point = level_path_screen_to_world(doc, x, y, editor_view_state)
                    updated_doc, updated_editor_state = level_path_place_primitive(doc, tool, world_point, editor_view_state)
                    viewer_state["doc"] = updated_doc
                    viewer_state["pending_line"] = updated_editor_state.get("pending_line")
                    if tool != "line" or not viewer_state.get("pending_line"):
                        viewer_state["tool"] = "select"
                    c = client
                    _deferred(lambda: _refresh_level_path_viewer(c))
                    return {"received": True}
                c = client
                _deferred(lambda: _refresh_level_path_viewer(c))
                return {"received": True}
            if action == "mouseMove":
                drag = viewer_state.get("drag")
                if not isinstance(drag, dict):
                    return {"received": True}
                if drag.get("kind") == "view_cube":
                    start_xy = drag.get("start_xy")
                    if isinstance(start_xy, list) and len(start_xy) >= 2:
                        dx = x - float(start_xy[0])
                        dy = y - float(start_xy[1])
                        if abs(dx) > 12.0 or abs(dy) > 12.0:
                            drag["face_down"] = None
                            next_view = level_path_rotate_view(str(viewer_state.get("view", "top")), dx, dy)
                            if next_view != str(viewer_state.get("view", "top")):
                                viewer_state["view"] = next_view
                                drag["start_xy"] = [x, y]
                                c = client
                                _deferred(lambda: _refresh_level_path_viewer(c))
                    return {"received": True}
                if drag.get("kind") == "move_item":
                    anchor = drag.get("anchor_world")
                    initial_world = drag.get("initial_world")
                    if isinstance(anchor, list) and len(anchor) >= 3 and isinstance(initial_world, list) and len(initial_world) >= 3:
                        current_world = level_path_screen_to_world(doc, x, y, editor_view_state, anchor=initial_world)
                        delta_world = (
                            float(current_world[0]) - float(anchor[0]),
                            float(current_world[1]) - float(anchor[1]),
                            float(current_world[2]) - float(anchor[2]),
                        )
                        viewer_state["doc"] = level_path_move_item(doc, drag.get("item", {}), delta_world)
                        drag["anchor_world"] = list(current_world)
                        c = client
                        _deferred(lambda: _refresh_level_path_viewer(c))
                    return {"received": True}
                return {"received": True}
            if action == "mouseUp":
                drag = viewer_state.get("drag")
                if isinstance(drag, dict) and drag.get("kind") == "view_cube":
                    face = level_path_view_cube_hit_test(x, y)
                    face_down = drag.get("face_down")
                    if face:
                        viewer_state["view"] = face
                    elif isinstance(face_down, str) and face_down:
                        viewer_state["view"] = face_down
                viewer_state["drag"] = None
                c = client
                _deferred(lambda: _refresh_level_path_viewer(c))
            return {"received": True}

        # Double-click on a folder item navigates into it.
        if action == "action" and target == "wizard.folder_list" and client is not None:
            folder_path = payload.get("action", "")
            if folder_path:
                c = client
                p = folder_path
                _deferred(lambda: _wizard_navigate(c, p))
            return {"received": True}

        # Open-project dialog: double-click to navigate or open as project.
        if action == "action" and target == "open_project.folder_list" and client is not None:
            folder_path = payload.get("action", "")
            if folder_path and Path(folder_path).is_dir():
                c = client
                fp = folder_path
                def _handle_folder_dclick():
                    if (Path(fp) / ".elaraproject").is_dir():
                        _open_project(c, fp, restore_session=True)
                    else:
                        _open_project_navigate(c, fp)
                _deferred(_handle_folder_dclick)
            return {"received": True}

        if action in ("action", "clicked") and target == "nav.debug.python_threads" and client is not None:
            selected_id = payload.get("action") or payload.get("id", "")
            if selected_id.startswith("python.thread."):
                tid_str = selected_id[len("python.thread."):]
                c = client
                _deferred(lambda t=tid_str: _python_dbg_show_thread(c, t))
            return {"received": True}

        if action in ("action", "clicked") and target == "nav.debug.cpp_threads" and client is not None:
            selected_id = payload.get("action") or payload.get("id", "")
            if selected_id.startswith("cpp.thread."):
                tid_str = selected_id[len("cpp.thread."):]
                c = client
                _deferred(lambda t=tid_str: _cpp_gdb_show_thread(c, t))
            return {"received": True}

        if action == "action" and client is not None:
            item_action = payload.get("action")
            c = client

            if isinstance(item_action, str) and item_action.startswith("breakpoint.toggle."):
                parts = item_action.split(".")
                if len(parts) >= 4:
                    tab_id, kind = _editor_widget_tab_and_kind(target)
                    if tab_id and kind:
                        try:
                            line0 = int(parts[2])
                            enabled = bool(int(parts[3]))
                            tab_entry = next((t for t in tab_list if t.get("tab_id") == tab_id), None)
                            path = str(tab_entry.get("path", "") or "") if tab_entry else ""
                            if kind == "source" and _editor_language_for_path(path) == "cpp":
                                _cpp_gdb_set_breakpoint(c, path, line0, enabled)
                            else:
                                _set_editor_breakpoint_line(tab_id, kind, line0, enabled)
                        except Exception:
                            pass
                return {"received": True}

            for tab_id, state in editor_state.items():
                ids = _editor_ids(tab_id)
                if item_action == ids["button_e"]:
                    app_state["active_editor_tab"] = tab_id
                    state["view"] = "e"
                    current_tab = tab_id
                    _deferred(lambda: (_apply_editor_view(c, current_tab, set_focus=True), _refresh_debug_sidebars(c, current_tab)))
                    return {"received": True}
                if item_action == ids["button_epa"]:
                    app_state["active_editor_tab"] = tab_id
                    state["view"] = "epa"
                    current_tab = tab_id
                    _deferred(lambda: _refresh_e_tab(c, current_tab, focus=True))
                    return {"received": True}
            if item_action in (
                "debug.cpp.reset",
                "debug.cpp.stop",
                "debug.cpp.continue",
                "debug.cpp.step_over",
                "debug.cpp.step_into",
                "debug.cpp.step_out",
                "debug.cpp.pause",
            ):
                _deferred(lambda action_id=item_action: _cpp_gdb_handle_action(c, action_id))
                return {"received": True}
            if item_action in (
                "debug.python.reset",
                "debug.python.stop",
                "debug.python.continue",
                "debug.python.step_over",
                "debug.python.step_into",
                "debug.python.step_out",
                "debug.python.pause",
            ):
                _deferred(lambda action_id=item_action: _python_dbg_handle_action(c, action_id))
                return {"received": True}
            if item_action == "bottom.status.epa.kill":
                def _kill_epa():
                    _epa_dbg_stop()
                    _refresh_status_panel(c)
                _deferred(_kill_epa)
                return {"received": True}
            if item_action == "bottom.status.cpp.kill":
                def _kill_cpp():
                    _cpp_gdb_stop_session()
                    _refresh_status_panel(c)
                _deferred(_kill_cpp)
                return {"received": True}
            if item_action == "bottom.status.python.kill":
                _deferred(lambda: _python_dbg_stop(c))
                return {"received": True}
            if item_action and item_action.startswith("tab.close."):
                close_tab_id = item_action[len("tab.close."):]
                entry = next((t for t in tab_list if t["tab_id"] == close_tab_id), None)
                if entry:
                    close_index = entry["index"]
                    tab_list.remove(entry)
                    if close_tab_id in editor_state:
                        _save_history(close_tab_id)
                        del editor_state[close_tab_id]
                    for t in tab_list:
                        if t["index"] > close_index:
                            t["index"] -= 1
                    if tab_list:
                        next_entry = min(tab_list, key=lambda item: abs(int(item.get("index", 0) or 0) - close_index))
                        app_state["active_editor_tab"] = next_entry.get("tab_id", "")
                    else:
                        app_state["active_editor_tab"] = ""
                    _persist_editor_session_state(allow_empty_tabs=True)
                    ci = close_index
                    def _do_close_tab():
                        try:
                            c.fire("ui.removeTab", {"target": "editor.tabs", "index": ci})
                            if not tab_list:
                                c.fire("ui.setVisible", {"target": "editor.tabs", "visible": False})
                                c.fire("ui.setVisible", {"target": "editor.welcome", "visible": True})
                        except Exception:
                            pass
                    _deferred(_do_close_tab)
                return {"received": True}

            if item_action in ("edit.undo", "edit.redo"):
                _undo_action = item_action
                def _do_undo_redo(ua=_undo_action):
                    tab_id = app_state.get("active_editor_tab", "")
                    state = editor_state.get(tab_id) if tab_id else None
                    if not state:
                        return
                    ids = _editor_ids(tab_id)
                    undo_stack = state.setdefault("undo_stack", [])
                    redo_stack = state.setdefault("redo_stack", [])
                    current_text = state.get("source_text", "")
                    if ua == "edit.undo":
                        if not undo_stack:
                            return
                        entry = undo_stack.pop()
                        # For redo, compute caret in current_text using same suffix heuristic
                        j = 0
                        min_len = min(len(current_text), len(entry["text"]))
                        while j < min_len and current_text[-(j + 1)] == entry["text"][-(j + 1)]:
                            j += 1
                        redo_caret = max(0, len(current_text) - j)
                        redo_stack.append({"text": current_text, "caret": redo_caret})
                        restored = entry["text"]
                        restored_caret = entry.get("caret", 0)
                    else:
                        if not redo_stack:
                            return
                        entry = redo_stack.pop()
                        j = 0
                        min_len = min(len(current_text), len(entry["text"]))
                        while j < min_len and current_text[-(j + 1)] == entry["text"][-(j + 1)]:
                            j += 1
                        undo_caret = max(0, len(current_text) - j)
                        undo_stack.append({"text": current_text, "caret": undo_caret})
                        restored = entry["text"]
                        restored_caret = entry.get("caret", 0)
                    state["source_text"] = restored
                    state["caret_index"] = restored_caret
                    try:
                        c.set_text(ids["source"], restored)
                        c.set_caret_index(ids["source"], restored_caret)
                    except Exception:
                        pass
                _deferred(_do_undo_redo)
                return {"received": True}

            if target == "app.menu" and item_action in (
                "edit.cut", "edit.copy", "edit.paste", "edit.select_all"
            ):
                _action = item_action
                def _do_edit_action(action=_action):
                    # Try the currently focused widget first so output panels,
                    # debug views, etc. get copy/paste without focus being stolen.
                    try:
                        result = c.perform_focused_action(action)
                        if isinstance(result, dict) and result.get("dispatched"):
                            return
                    except Exception:
                        pass
                    # Fall back to the active editor when nothing else handled it.
                    tab_id = app_state.get("active_editor_tab", "")
                    state = editor_state.get(tab_id) if tab_id else None
                    if state:
                        ids = _editor_ids(tab_id)
                        view = state.get("view", "e")
                        target_widget = ids["source"]
                        if view == "epa":
                            target_widget = ids["epa"]
                        try:
                            c.set_focus(target_widget)
                            c.perform_action(target_widget, action)
                        except Exception:
                            pass
                _deferred(_do_edit_action)

            if item_action == "app.toggle_theme":
                current_theme = app_state.get("theme", "dark")
                next_theme = "light" if current_theme == "dark" else "dark"
                app_state["theme"] = next_theme
                _deferred(lambda t=next_theme: (c.set_theme_mode(t), _register_event_responders(c)))
                return {"received": True}

            if item_action == "app.toggle_right_panel":
                _deferred(lambda: _toggle_right_panel(c))
                return {"received": True}

            if item_action == "app.toggle_bottom_panel":
                _deferred(lambda: _toggle_bottom_panel(c))
                return {"received": True}

            if target == "app.menu" and item_action == "view.appearance.toggle_window_header":
                next_use_system = not _use_system_window_header()
                _save_ide_state({
                    "ui": {
                        "use_system_window_header": next_use_system,
                    }
                })
                current_title = app_state.get("project_name")
                if current_title:
                    current_title = f"EPA-IDE : {current_title}"
                else:
                    current_title = "EPA-IDE"
                _deferred(lambda: (
                    c.set_window_decorated(next_use_system),
                    c.configure_menu_bar_chrome(
                        "app.menu",
                        custom_chrome=not next_use_system,
                        window_title=current_title,
                    ),
                ))
                return {"received": True}

            if target == "app.menu" and item_action == "file.open":
                saved = _load_ide_state().get("last_open_dir", "")
                initial = saved if saved and Path(saved).is_dir() else str(Path.home())
                open_file_nav_state["path"] = initial
                _deferred(lambda: c.open_window("open-file", "Open File", 920, 640, build_open_file_dialog(initial)))

            elif item_action in ("file.new_project", "no_project.new_project"):
                initial = str(Path.home())
                wizard_state.clear()
                wizard_state.update({
                    "tech_epa": True,
                    "tech_cpp": True,
                    "tech_python": True,
                    "project_name": "",
                    "ui_client": "both",
                    "ui_template": "tabbed-control-panel",
                    "python_multi_cpu": False,
                    "cpp_epa_vm_host": False,
                    "cpp_epa_debug_rpc": True,
                    "rpc_host": "127.0.0.1",
                    "rpc_port": "18777",
                })
                nav_state["path"] = initial
                _deferred(lambda: c.open_window(
                    "new-project", "New Project", 540, 760,
                    build_new_project_wizard(initial)
                ))

            elif item_action in ("file.open_project", "no_project.open_project"):
                last = _load_ide_state().get("last_project", "")
                initial = str(Path(last).parent) if last and Path(last).parent.is_dir() else str(Path.home())
                open_project_nav_state["path"] = initial
                _deferred(lambda: c.open_window(
                    "open-project", "Open Project", 500, 500,
                    build_open_project_dialog(initial)
                ))

            elif item_action == "nav.refresh":
                project_root = app_state.get("project_root", "")
                if project_root and client is not None:
                    _deferred(lambda: _open_project(c, project_root))

            elif item_action == "nav.filter_toggle":
                project_root = app_state.get("project_root", "")
                if project_root:
                    app_state["nav_filter_source_only"] = not app_state.get("nav_filter_source_only", False)
                    project_path_ft = Path(project_root)
                    technologies_ft = _project_technologies(project_path_ft)
                    _deferred(lambda pp=project_path_ft, tt=technologies_ft: _populate_nav_trees(c, pp, tt))

            elif item_action == "nav.add":
                items = _project_add_menu_items()
                _deferred(lambda: c.open_window(
                    "project-add-menu",
                    "Project Actions",
                    320,
                    max(120, 50 + len(items) * 38),
                    build_project_add_menu_dialog(items),
                ))

            elif item_action and item_action.startswith("project_add.new_file."):
                tech = item_action[len("project_add.new_file."):]
                project_root = app_state.get("project_root", "")
                tech_sub = {"E": "epa", "Cpp": "cpp", "Python": "python", "Artifact3D": "3d_artifacts"}.get(tech, "")
                initial = project_root or str(Path.home())
                if project_root and tech_sub:
                    candidate = str(Path(project_root) / tech_sub)
                    if Path(candidate).is_dir():
                        initial = candidate
                new_file_state.clear()
                new_file_state.update({
                    "filename": "",
                    "new_folder_name": "",
                    "tech": tech,
                    "dir": initial,
                    "template": "root_node" if tech == "E" else "",
                })
                new_file_nav_state["path"] = initial
                _deferred(lambda: c.close_window("project-add-menu"))
                _deferred(lambda: c.open_window(
                    "new-file",
                    f"New {tech} File",
                    460,
                    650 if tech == "E" else 520,
                    build_new_file_dialog(tech, initial, new_file_state.get("template")),
                ))

            elif item_action and item_action.startswith("project_add.add_tech."):
                tech = item_action[len("project_add.add_tech."):]
                if tech == "cpp":
                    _deferred(lambda: _open_cpp_technology_wizard(c))
                    return {"received": True}
                def _do_add_tech():
                    try:
                        _project_add_technology(c, tech)
                        c.close_window("project-add-menu")
                    except subprocess.CalledProcessError as exc:
                        message = (exc.stderr or exc.stdout or str(exc)).strip()
                        _append_build_output(c, f"[project-add-error] {message}\n")
                    except Exception as exc:
                        _append_build_output(c, f"[project-add-error] {exc}\n")
                _deferred(_do_add_tech)

            elif item_action and item_action.startswith("nav.add_tech."):
                tech = item_action[len("nav.add_tech."):]
                if tech == "cpp":
                    _deferred(lambda: _open_cpp_technology_wizard(c))
                    return {"received": True}
                def _do_nav_add_tech(t=tech):
                    try:
                        _project_add_technology(c, t)
                    except subprocess.CalledProcessError as exc:
                        message = (exc.stderr or exc.stdout or str(exc)).strip()
                        _append_build_output(c, f"[project-add-error] {message}\n")
                    except Exception as exc:
                        _append_build_output(c, f"[project-add-error] {exc}\n")
                _deferred(_do_nav_add_tech)

            elif item_action == "build.build_project":
                _deferred(lambda: _build_project(c, rebuild=False))

            elif item_action == "build.compile_epa":
                _deferred(lambda: _compile_epa_project(c))

            elif item_action == "build.compile":
                _deferred(lambda: _compile_current_file(c))

            elif item_action == "cpp_tech.cancel":
                _deferred(lambda: c.close_window("add-cpp-technology"))

            elif item_action == "cpp_tech.create":
                def _do_add_cpp_from_wizard():
                    try:
                        if bool(cpp_tech_state.get("cpp_epa_debug_rpc", False)) and not bool(cpp_tech_state.get("cpp_epa_vm_host", False)):
                            c.set_text("cpp_tech.error", "EPA debug JSON-RPC requires the EPA VM Host adapter.")
                            return
                        _project_add_technology(c, "cpp", cpp_tech_state)
                        c.close_window("add-cpp-technology")
                    except subprocess.CalledProcessError as exc:
                        message = (exc.stderr or exc.stdout or str(exc)).strip()
                        c.set_text("cpp_tech.error", message or "Could not add C++ technology.")
                        _append_build_output(c, f"[project-add-error] {message}\n")
                    except Exception as exc:
                        c.set_text("cpp_tech.error", str(exc))
                        _append_build_output(c, f"[project-add-error] {exc}\n")
                _deferred(_do_add_cpp_from_wizard)

            elif item_action == "build.rebuild_project":
                _deferred(lambda: _build_project(c, rebuild=True))

            elif item_action == "build.clean":
                _deferred(_clean_project)

            elif item_action == "open_project.cancel":
                _deferred(lambda: _close_open_project_window(c))

            elif item_action == "open_project.nav.up":
                current = open_project_nav_state.get("path", str(Path.home()))
                parent = str(Path(current).parent)
                if parent != current:
                    p = parent
                    _deferred(lambda: _open_project_navigate(c, p))

            elif item_action == "open_project.nav.home":
                _deferred(lambda: _open_project_navigate(c, str(Path.home())))

            elif item_action == "open_project.open":
                current = open_project_nav_state.get("path", str(Path.home()))
                cur = current
                def _do_open():
                    if (Path(cur) / ".elaraproject").is_dir():
                        _open_project(c, cur, restore_session=True)
                    else:
                        try:
                            c.set_text("open_project.hint", "No .elaraproject found — navigate into a project folder")
                        except Exception:
                            pass
                _deferred(_do_open)

            elif item_action and item_action.startswith("new_file.") and item_action not in (
                "new_file.cancel", "new_file.create",
                "new_file.nav.up", "new_file.nav.home",
                "new_file.make_folder",
            ):
                tech = item_action[len("new_file."):]
                project_root = app_state.get("project_root", "")
                tech_dir_map = {"E": "epa", "Cpp": "cpp", "Python": "python", "Artifact3D": "3d_artifacts"}
                tech_sub = tech_dir_map.get(tech, "")
                if project_root and tech_sub:
                    initial = str(Path(project_root) / tech_sub)
                    if not Path(initial).is_dir():
                        initial = project_root
                else:
                    initial = project_root or str(Path.home())
                new_file_state.clear()
                new_file_state.update({
                    "filename": "",
                    "new_folder_name": "",
                    "tech": tech,
                    "dir": initial,
                    "template": "root_node" if tech == "E" else "",
                })
                new_file_nav_state["path"] = initial
                dialog_height = 650 if tech == "E" else 520
                _deferred(lambda: c.open_window(
                    "new-file", "New File", 460, dialog_height,
                    build_new_file_dialog(tech, initial, new_file_state.get("template"))
                ))

            elif item_action == "new_file.cancel":
                _deferred(lambda: c.close_window("new-file"))

            elif item_action == "new_file.nav.up":
                current = new_file_nav_state.get("path", str(Path.home()))
                parent = str(Path(current).parent)
                if parent != current:
                    p = parent
                    _deferred(lambda: _new_file_navigate(c, p))

            elif item_action == "new_file.nav.home":
                _deferred(lambda: _new_file_navigate(c, str(Path.home())))

            elif item_action == "new_file.make_folder":
                folder_name = new_file_state.get("new_folder_name", "").strip()
                cur_dir     = new_file_nav_state.get("path", str(Path.home()))

                def _do_make_folder():
                    if not folder_name:
                        try:
                            c.set_text("new_file.error", "Folder name cannot be empty.")
                        except Exception:
                            pass
                        return
                    new_dir = Path(cur_dir) / folder_name
                    try:
                        new_dir.mkdir(parents=True, exist_ok=True)
                    except OSError as exc:
                        try:
                            c.set_text("new_file.error", f"Could not create folder: {exc}")
                        except Exception:
                            pass
                        return
                    new_file_state["new_folder_name"] = ""
                    try:
                        c.set_text("new_file.new_folder_name", "")
                    except Exception:
                        pass
                    _new_file_navigate(c, cur_dir)

                _deferred(_do_make_folder)

            elif item_action == "new_file.create":
                filename    = new_file_state.get("filename", "").strip()
                save_dir    = new_file_nav_state.get("path", new_file_state.get("dir", str(Path.home())))
                tech        = new_file_state.get("tech", "")
                template_id = new_file_state.get("template", "root_node")
                ext_map     = {"E": ".e", "Cpp": ".cpp", "Python": ".py", "Artifact3D": ".e3d.json"}
                default_ext = ext_map.get(tech, "")

                def _do_create_file():
                    if not filename:
                        try:
                            c.set_text("new_file.error", "File name cannot be empty.")
                        except Exception:
                            pass
                        return
                    name = filename if "." in filename else filename + default_ext
                    dest = Path(save_dir) / name
                    try:
                        dest.parent.mkdir(parents=True, exist_ok=True)
                        if tech == "Cpp":
                            if dest.suffix.lower() != ".cpp":
                                dest = dest.with_suffix(".cpp")
                            header = dest.with_suffix(".h")
                            dest.write_text(_cpp_source_content(dest.name), encoding="utf-8")
                            header.write_text(_cpp_header_content(header.name), encoding="utf-8")
                        else:
                            dest.write_text(_file_content(tech, name, template_id), encoding="utf-8")
                    except OSError as exc:
                        try:
                            c.set_text("new_file.error", f"Could not create file: {exc}")
                        except Exception:
                            pass
                        return
                    try:
                        c.close_window("new-file")
                    except Exception:
                        pass
                    project_root = app_state.get("project_root", "")
                    if project_root:
                        _open_project(c, project_root)
                    try:
                        _open_file_tab(c, str(dest), make_permanent=True)
                    except Exception:
                        pass

                _deferred(_do_create_file)

            elif item_action == "dialog.file.confirm":
                current_dir = open_file_nav_state.get("path", "")
                if current_dir:
                    _save_ide_state({"last_open_dir": current_dir})
                _deferred(lambda: c.close_window("open-file"))

            elif item_action == "dialog.file.cancel":
                _deferred(lambda: c.close_window("open-file"))

            elif item_action == "dialog.nav.up":
                current = open_file_nav_state.get("path", str(Path.home()))
                parent = str(Path(current).parent)
                if parent != current:
                    p = parent
                    _deferred(lambda: _open_file_navigate(c, p))

            elif item_action == "dialog.nav.back":
                pass  # history not yet tracked

            elif item_action == "dialog.nav.forward":
                pass  # history not yet tracked

            elif item_action == "dialog.folder.refresh":
                current = open_file_nav_state.get("path", str(Path.home()))
                _deferred(lambda: _open_file_navigate(c, current))

            elif item_action == "wizard.cancel":
                _deferred(lambda: c.close_window("new-project"))

            elif item_action == "wizard.nav.up":
                current = nav_state.get("path", str(Path.home()))
                parent = str(Path(current).parent)
                if parent != current:
                    p = parent
                    _deferred(lambda: _wizard_navigate(c, p))

            elif item_action == "wizard.nav.home":
                _deferred(lambda: _wizard_navigate(c, str(Path.home())))

            elif item_action == "wizard.create":
                import datetime
                tech_epa    = wizard_state.get("tech_epa",    True)
                tech_cpp    = wizard_state.get("tech_cpp",    True)
                tech_python = wizard_state.get("tech_python", True)
                project_name = wizard_state.get("project_name", "").strip()
                ui_template = wizard_state.get("ui_template", "tabbed-control-panel")
                rpc_host = wizard_state.get("rpc_host", "127.0.0.1").strip() or "127.0.0.1"
                rpc_port_text = wizard_state.get("rpc_port", "18777").strip() or "18777"
                python_multi_cpu = bool(wizard_state.get("python_multi_cpu", False))
                cpp_epa_vm_host = bool(wizard_state.get("cpp_epa_vm_host", False))
                cpp_epa_debug_rpc = bool(wizard_state.get("cpp_epa_debug_rpc", True))
                save_dir    = nav_state.get("path", str(Path.home()))

                def _do_create():
                    if not project_name:
                        try:
                            c.set_text("wizard.error", "Project name cannot be empty.")
                        except Exception:
                            pass
                        return

                    if not (tech_epa or tech_cpp or tech_python):
                        try:
                            c.set_text("wizard.error", "Select at least one technology.")
                        except Exception:
                            pass
                        return

                    if cpp_epa_debug_rpc and not cpp_epa_vm_host:
                        try:
                            c.set_text("wizard.error", "EPA debug JSON-RPC requires the EPA VM Host adapter.")
                        except Exception:
                            pass
                        return

                    try:
                        rpc_port = int(rpc_port_text)
                    except ValueError:
                        try:
                            c.set_text("wizard.error", "UI RPC port must be an integer.")
                        except Exception:
                            pass
                        return

                    if rpc_port <= 0 or rpc_port > 65535:
                        try:
                            c.set_text("wizard.error", "UI RPC port must be between 1 and 65535.")
                        except Exception:
                            pass
                        return

                    project_root = Path(save_dir) / project_name
                    try:
                        (project_root / ".elaraproject").mkdir(parents=True, exist_ok=True)
                        if tech_epa:
                            (project_root / "epa").mkdir(exist_ok=True)
                        (project_root / "build").mkdir(exist_ok=True)
                        if tech_epa:
                            (project_root / "build" / "epa").mkdir(exist_ok=True)
                        techs = [t for t, v in [("epa", tech_epa), ("cpp", tech_cpp), ("python", tech_python)] if v]
                        (project_root / ".elaraproject" / "project.json").write_text(
                            json.dumps({
                                "name": project_name,
                                "technologies": techs,
                                "ui_template": ui_template,
                                "rpc_host": rpc_host,
                                "rpc_port": rpc_port,
                                "python_multi_cpu": python_multi_cpu,
                                "cpp_epa_vm_host": cpp_epa_vm_host,
                                "cpp_epa_debug_rpc": cpp_epa_debug_rpc,
                                "created": datetime.datetime.utcnow().isoformat() + "Z",
                            }, indent=2),
                            encoding="utf-8",
                        )
                        (project_root / ".elaraproject" / "bookmarks.json").write_text("[]", encoding="utf-8")
                        (project_root / ".elaraproject" / "breakpoints.json").write_text("[]", encoding="utf-8")

                        if tech_cpp:
                            builder = _ensure_project_builder()
                            cpp_root = project_root / "cpp"
                            env = os.environ.copy()
                            env["LC_ALL"] = "C"
                            subprocess.run(
                                [
                                    str(builder),
                                    "--non-interactive",
                                    "--app-kind", "ui",
                                    "--ui-client-language", "cpp",
                                    "--ui-template", ui_template,
                                    "--epa-vm-host", "yes" if cpp_epa_vm_host else "no",
                                    "--epa-debug-rpc", "yes" if (tech_epa and cpp_epa_vm_host and cpp_epa_debug_rpc) else "no",
                                    "--address", rpc_host,
                                    "--port", str(rpc_port),
                                    "--name", project_name,
                                    "--output", str(cpp_root),
                                ],
                                check=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                text=True,
                                env=env,
                            )

                        if tech_python:
                            builder = _ensure_project_builder()
                            python_root = project_root / "python"
                            env = os.environ.copy()
                            env["LC_ALL"] = "C"
                            subprocess.run(
                                [
                                    str(builder),
                                    "--non-interactive",
                                    "--app-kind", "ui",
                                    "--ui-client-language", "python",
                                    "--ui-template", ui_template,
                                    "--multi-cpu-python", "yes" if python_multi_cpu else "no",
                                    "--address", rpc_host,
                                    "--port", str(rpc_port),
                                    "--name", project_name,
                                    "--output", str(python_root),
                                ],
                                check=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                text=True,
                                env=env,
                            )
                    except OSError as exc:
                        try:
                            c.set_text("wizard.error", f"Could not create project: {exc}")
                        except Exception:
                            pass
                        return
                    except subprocess.CalledProcessError as exc:
                        message = (exc.stderr or exc.stdout or str(exc)).strip()
                        try:
                            c.set_text("wizard.error", f"Could not generate project scaffold: {message}")
                        except Exception:
                            pass
                        return

                    _open_project(c, project_root)
                    try:
                        c.close_window("new-project")
                    except Exception:
                        pass

                _deferred(_do_create)

            elif item_action and item_action.startswith("tab.close."):
                tab_widget_id = item_action[len("tab.close."):]
                print(json.dumps({"tab_closed": tab_widget_id}, indent=2), flush=True)

            elif item_action and (
                item_action.startswith("debug.kernel.all_workers.")
                or item_action.startswith("debug.kernel.run.")
                or item_action.startswith("debug.kernel.step.")
            ):
                is_bulk_debug = item_action.startswith("debug.kernel.all_workers.")
                is_run = item_action.startswith("debug.kernel.run.")
                if is_bulk_debug:
                    kernel_id_str = item_action[len("debug.kernel.all_workers."):]
                elif is_run:
                    kernel_id_str = item_action[len("debug.kernel.run."):]
                else:
                    kernel_id_str = item_action[len("debug.kernel.step."):]
                project_root_str = app_state.get("project_root", "")
                if project_root_str:
                    rel = kernel_id_str.replace(".", "/") + ".e"
                    file_path = str(Path(project_root_str) / "epa" / rel)
                    tab_id = _tab_id_for_path(file_path)
                    source_tab_id = tab_id
                    def _debug_kernel_action(fp=file_path, stid=source_tab_id):
                        _open_file_tab(c, fp, make_permanent=True)
                        st = editor_state.get(stid)
                        if st:
                            _refresh_e_tab(c, stid)
                            st = editor_state.get(stid)
                        if st and not st.get("debug", False):
                            st["debug"] = True
                            _apply_editor_view(c, stid)
                            _refresh_debug_sidebars(c, stid)
                        bundle_path = _bundle_path_for_kernel_id(kernel_id_str)
                        worker_wid = _selected_worker_wid_for_kernel(kernel_id_str)
                        if worker_wid is None:
                            worker_wid = 1

                        kid = 0
                        do_run = is_run
                        ui_client = c
                        tab_id_for_ind = kernel_id_str
                        bp = bundle_path
                        r = do_run
                        k = kid
                        uc = ui_client
                        ktid = tab_id_for_ind
                        wid = worker_wid
                        dbg_c = _epa_dbg_client()
                        if not _epa_dbg_running() or not dbg_c:
                            if not _start_debug_vm(uc, force_restart=False):
                                return
                            dbg_c = _epa_dbg_client()
                        app_state["debug_active_kernel"] = ktid
                        already_loaded = app_state.get("debug_kernel_loaded") == bp
                        if not already_loaded or not dbg_c:
                            prev_ktid = _kernel_tab_id_from_bundle(app_state.get("debug_kernel_loaded", ""))
                            if prev_ktid and prev_ktid != ktid and uc:
                                _clear_kernel_queue_state(uc, prev_ktid)
                                _set_kernel_run_indicator(uc, prev_ktid, _IND_OFF)
                            result = _epa_dbg_load_bundle(bp, kernel_id=k)
                            if not result.get("ok"):
                                _epa_dbg_log(f"[error] load_bundle failed: {result.get('error', 'unknown')}")
                                _set_kernel_load_indicator(uc, ktid, _IND_RED)
                                return
                            app_state["debug_kernel_loaded"] = bp
                            _set_kernel_load_indicator(uc, ktid, _IND_GREEN)
                            dbg_c = _epa_dbg_client()
                        if not dbg_c:
                            return
                        _sync_cached_worker_debug_states(dbg_c)
                        if is_bulk_debug:
                            current_snapshot = app_state.get("debug_kernel_snapshot_state", {}).get(ktid) or {}
                            enabled = not _kernel_all_workers_debug_enabled(ktid, current_snapshot)
                            worker_wids = _kernel_worker_wids(ktid)
                            try:
                                latest_snap = {}
                                for target_wid in worker_wids:
                                    resp = dbg_c.set_worker_debug(target_wid, enabled, path_id=ktid)
                                    if isinstance(resp, dict):
                                        latest_snap = resp.get("snapshot", latest_snap) or latest_snap
                                    app_state.setdefault("debug_worker_debug_state", {})[
                                    _worker_debug_cache_key(ktid, target_wid)
                                    ] = enabled
                                if latest_snap and uc:
                                    _schedule_all_kernel_debug_state_refresh(uc, dbg_c, ktid, latest_snap)
                                elif uc:
                                    _schedule_all_kernel_debug_state_refresh(uc, dbg_c, ktid, dbg_c.snapshot(k, path_id=ktid))
                            except Exception as exc:
                                _epa_dbg_log(f"[error] bulk_set_worker_debug: {exc}")
                                _push_exception(exc, "bulk_set_worker_debug")
                            return
                        if r:
                            _sync_editor_breakpoints_for_run(dbg_c, k, ktid)
                        # Mark selected worker as healthy and not blocked while the RPC is in flight.
                        if uc:
                            _set_kernel_load_indicator(uc, ktid, _IND_GREEN)
                            _set_kernel_run_indicator(uc, ktid, _IND_GREEN)
                        try:
                            if r:
                                resp = dbg_c.run_to_wait(wid=wid, path_id=ktid)
                                snap = resp.get("snapshot", {}) if isinstance(resp, dict) else {}
                                step_status = str(resp.get("stop_reason", "")) if isinstance(resp, dict) else ""
                            else:
                                snap, step_status = _dbg_step_for_active_view(dbg_c, k, ktid, wid)
                            if uc:
                                _schedule_all_kernel_debug_state_refresh(uc, dbg_c, ktid, snap if snap else None)
                            _drain_epa_dbg_events(uc, dbg_c, k, ktid)
                            if snap and not r:
                                _epa_dbg_log(_format_step_eip_log(ktid, snap))
                                if step_status not in ("boundary", ""):
                                    _epa_dbg_log(f"[step-status] {ktid} wid={wid} {step_status}")
                        except Exception as exc:
                            should_retry_load = (
                                "kernel not created" in str(exc).lower()
                                and bp
                                and _retry_after_forced_bundle_load(dbg_c, bp, k, ktid)
                            )
                            if should_retry_load:
                                try:
                                    dbg_c = _epa_dbg_client()
                                    _sync_cached_worker_debug_states(dbg_c)
                                    if r:
                                        _sync_editor_breakpoints_for_run(dbg_c, k, ktid)
                                        resp = dbg_c.run_to_wait(wid=wid, path_id=ktid)
                                        snap = resp.get("snapshot", {}) if isinstance(resp, dict) else {}
                                        step_status = str(resp.get("stop_reason", "")) if isinstance(resp, dict) else ""
                                    else:
                                        snap, step_status = _dbg_step_for_active_view(dbg_c, k, ktid, wid)
                                    if uc:
                                        _schedule_all_kernel_debug_state_refresh(uc, dbg_c, ktid, snap if snap else None)
                                    _drain_epa_dbg_events(uc, dbg_c, k, ktid)
                                    if snap and not r:
                                        _epa_dbg_log(_format_step_eip_log(ktid, snap))
                                        if step_status not in ("boundary", ""):
                                            _epa_dbg_log(f"[step-status] {ktid} wid={wid} {step_status}")
                                    return
                                except Exception as retry_exc:
                                    exc = retry_exc
                            is_step_complete = (
                                not r
                                and getattr(exc, "code", "") == "step_failed"
                                and "step complete returning to host" in str(exc)
                            )
                            if is_step_complete:
                                try:
                                    snap = dbg_c.snapshot(k)
                                except Exception:
                                    snap = {}
                                if uc:
                                    _schedule_all_kernel_debug_state_refresh(uc, dbg_c, ktid, snap if snap else None)
                                _drain_epa_dbg_events(uc, dbg_c, k, ktid)
                                if snap:
                                    _epa_dbg_log(_format_step_eip_log(ktid, snap))
                                return
                            if uc:
                                _set_kernel_load_indicator(uc, ktid, _IND_RED)
                                _set_kernel_run_indicator(uc, ktid, _IND_OFF)
                            _epa_dbg_log(f"[error] dbg_run_or_step: {exc}")
                            _push_exception(exc, "dbg_run_or_step")
                    _deferred(_debug_kernel_action)
                return {"received": True}

            elif item_action == "ai.send":
                if not ai_state.get("generating") and ai_state.get("input_text", "").strip():
                    _deferred(_ai_send)
                return {"received": True}

            elif item_action == "ai.stop":
                ev = ai_state.get("cancel_event")
                if ev:
                    ev.set()
                proc = ai_state.get("process")
                if proc is not None:
                    try:
                        proc.terminate()
                    except Exception:
                        pass
                return {"received": True}

            elif item_action == "ai.new_chat":
                ai_state["messages"].clear()
                ai_state["input_text"] = ""
                def _do_clear_chat():
                    try:
                        c.set_text("ai.history", _ai_format_history())
                        c.set_text("ai.input", "")
                    except Exception:
                        pass
                _deferred(_do_clear_chat)
                return {"received": True}

            elif item_action in ("level_path.tool.square", "level_path.tool.cube", "level_path.tool.line"):
                viewer_state = app_state.get("level_path_viewer", {})
                if isinstance(viewer_state, dict):
                    viewer_state["tool"] = item_action.rsplit(".", 1)[-1]
                    viewer_state["pending_line"] = None
                    viewer_state["drag"] = None
                    c = client
                    _deferred(lambda: _refresh_level_path_viewer(c))
                return {"received": True}

        # AI context checkbox toggles
        if action == "valueChanged" and target in (
            "ai.ctx.file", "ai.ctx.project", "ai.ctx.selection"
        ):
            key = "ctx_" + target.rsplit(".", 1)[-1]
            ai_state[key] = payload.get("value", 0) > 0.5
            return {"received": True}

        # Ingress designer — kernel selection
        if target == "nav.debug.ingress_kernel" and action in ("action", "valueChanged", "clicked") and client is not None:
            kernel_id = payload.get("action") or ""
            app_state["debug_ingress_kernel"] = kernel_id
            app_state["debug_ingress_worker"] = ""
            c = client
            def _on_kernel_selected(kid=kernel_id):
                _refresh_ingress_worker_combo(c, kid)
                items = _ingress_types_from_project(kid, "")
                app_state["debug_ingress_types_cache"] = items
                _apply_ingress_types_combo(c, items)
            _deferred(_on_kernel_selected)
            return {"received": True}

        # Ingress designer — worker selection
        if target == "nav.debug.ingress_worker" and action in ("action", "valueChanged", "clicked") and client is not None:
            worker_name = payload.get("action") or ""
            app_state["debug_ingress_worker"] = worker_name
            kernel_id = app_state.get("debug_ingress_kernel", "")
            c = client
            def _on_worker_selected(kid=kernel_id, wname=worker_name):
                items = _ingress_types_from_project(kid, wname)
                app_state["debug_ingress_types_cache"] = items
                _apply_ingress_types_combo(c, items)
            _deferred(_on_worker_selected)
            return {"received": True}

        # Debug VM global controls
        if action == "action" and (
            target in ("nav.debug.vm_reset", "debug.vm.reset")
            or payload_action == "debug.vm.reset"
        ):
            c = client
            def _do_vm_reset():
                app_state.pop("debug_kernel_loaded", None)
                app_state.pop("debug_active_kernel", None)
                app_state["debug_vm_started"] = False
                if not _start_debug_vm(c, force_restart=True):
                    _epa_dbg_set_vm_status("error", "start failed")
            _deferred(_do_vm_reset)
            return {"received": True}

        if action == "action" and (
            target in ("nav.debug.vm_stop", "debug.vm.stop")
            or payload_action == "debug.vm.stop"
        ):
            c = client
            def _do_vm_stop():
                app_state.pop("debug_kernel_loaded", None)
                app_state.pop("debug_active_kernel", None)
                app_state["debug_vm_started"] = False
                _reset_all_kernel_debug_state(c)
                _epa_dbg_stop()
            _deferred(_do_vm_stop)
            return {"received": True}

        # Help window — open from Help menu
        if item_action in ("help.docs", "help.about", "help.samples"):
            c = client
            _deferred(lambda: c.open_window("help", "EPA-IDE Help", 960, 680, build_help_window()))
            return {"received": True}

        # Help window — nav tree selection loads a document
        if target == "help.nav_tree" and action == "action" and client is not None:
            node_id = payload.get("action") or ""
            if node_id:
                content = _help_load_doc(node_id)
                c = client
                def _update_help_content(text=content):
                    try:
                        c.set_text("help.content", text)
                    except Exception:
                        pass
                _deferred(_update_help_content)
            return {"received": True}

        # Help window — close button
        if item_action == "help.close":
            c = client
            _deferred(lambda: c.close_window("help") if c else None)
            return {"received": True}

        # Error dialog close button
        if item_action == "error_dialog.close":
            c = client
            _deferred(lambda: c.close_window("epavm-error") if c else None)
            return {"received": True}

        if item_action == "worker_fault.close":
            c = client
            _deferred(lambda: c.close_window("worker-fault") if c else None)
            return {"received": True}

        # Kernel step worker combo — track selection per kernel
        if action in ("action", "valueChanged") and target and target.endswith(".worker"):
            prefix = "nav.debug.kernel."
            suffix = ".worker"
            if target.startswith(prefix) and target.endswith(suffix):
                kernel_id_str = target[len(prefix):-len(suffix)]
                snapshot = app_state.get("debug_kernel_snapshot_state", {}).get(kernel_id_str)
                worker_name, worker_wid = _kernel_worker_combo_selected(kernel_id_str, payload, snapshot)
                if not worker_name:
                    return {"received": True}
                app_state[f"debug_kernel_worker_{kernel_id_str}"] = worker_name
                app_state[f"debug_kernel_worker_wid_{kernel_id_str}"] = 0 if worker_wid is None else int(worker_wid)
                _epa_dbg_log(
                    f"[debug-worker-select] kernel={kernel_id_str} worker={worker_name} "
                    f"wid={0 if worker_wid is None else int(worker_wid)} payload={payload}"
                )
                if client:
                    _refresh_kernel_worker_combo(client, kernel_id_str, snapshot)
                    _apply_cached_kernel_queue_state(client, kernel_id_str)
                    _refresh_selected_worker_debug_from_backend(client, kernel_id_str, snapshot)
                    if snapshot:
                        _update_kernel_indicator_from_snapshot(client, kernel_id_str, snapshot)
                        _refresh_runtime_debug_sidebars(client, kernel_id_str, snapshot)
                        # Show fault popup when a faulted worker is selected.
                        selected = _selected_worker_from_snapshot(kernel_id_str, snapshot)
                        if selected and selected.get("faulted"):
                            fault_msg = str(selected.get("fault_message") or "Hard fault")
                            try:
                                client.open_window(
                                    "worker-fault",
                                    f"Worker Fault — {worker_name}",
                                    540, 200,
                                    build_worker_fault_dialog(worker_name, fault_msg),
                                )
                            except Exception:
                                pass
                    _jump_to_selected_worker_eip(client, kernel_id_str, snapshot)
                return {"received": True}

        # Kernel worker debug checkbox — enabled means line/boundary stepping;
        # disabled means run the selected wid until it blocks again.
        if action in ("valueChanged", "action", "clicked") and target and target.endswith(".debug"):
            prefix = "nav.debug.kernel."
            suffix = ".debug"
            if target.startswith(prefix) and target.endswith(suffix):
                kernel_id_str = target[len(prefix):-len(suffix)]
                wid = _selected_worker_wid_for_kernel(kernel_id_str)
                if wid is None:
                    wid = 0
                if "checked" in payload:
                    enabled = bool(payload.get("checked"))
                elif "value" in payload:
                    enabled = float(payload.get("value", 0) or 0) > 0.5
                else:
                    return {"received": True}
                _push_event(
                    "debug_toggle_branch",
                    kernel=kernel_id_str,
                    wid=wid,
                    action=action,
                    payload=_trim_for_log(payload) if isinstance(payload, dict) else payload,
                    enabled=enabled,
                )
                app_state.setdefault("debug_worker_debug_state", {})[
                    _worker_debug_cache_key(kernel_id_str, wid)
                ] = enabled
                current_worker_name = str(app_state.get(f"debug_kernel_worker_{kernel_id_str}", "") or "")
                _epa_dbg_log(
                    f"[debug-toggle-event] kernel={kernel_id_str} worker={current_worker_name or '<unset>'} "
                    f"wid={wid} action={action} payload={payload} resolved={enabled}"
                )
                dbg_c = _epa_dbg_client()
                if dbg_c and _epa_dbg_running():
                    try:
                        _epa_dbg_log(
                            f"[debug-state-write] kernel={kernel_id_str} worker={current_worker_name or '<unset>'} "
                            f"wid={wid} set={enabled}"
                        )
                        resp = dbg_c.set_worker_debug(wid, enabled, path_id=kernel_id_str)
                        _epa_dbg_log(
                            f"[debug-state-write-result] kernel={kernel_id_str} wid={wid} "
                            f"set={enabled} resp={resp}"
                        )
                        snap = resp.get("snapshot", {}) if isinstance(resp, dict) else {}
                        if snap and client:
                            _apply_kernel_debug_snapshot(client, kernel_id_str, snap, refresh_sidebars=False)
                        if client:
                            _refresh_selected_worker_debug_from_backend(client, kernel_id_str, snap if snap else None)
                    except Exception as exc:
                        _epa_dbg_log(f"[debug-state-error] {kernel_id_str} wid={wid}: {exc}")
                else:
                    if client:
                        _set_kernel_worker_debug_checkbox_local(client, kernel_id_str)
                return {"received": True}

        # Ingress designer — profile selected in list
        if target == "nav.debug.ingress_profiles":
            if action == "valueChanged":
                # List view reports selection as a float index — look up in cached items
                idx = int(payload.get("value", 0))
                items = app_state.get("debug_ingress_profiles_cache", [])
                if 0 <= idx < len(items):
                    app_state["debug_ingress_selected_profile"] = items[idx]["id"]
            elif action in ("action", "clicked"):
                profile_id = payload.get("action") or payload.get("id") or ""
                if profile_id:
                    app_state["debug_ingress_selected_profile"] = profile_id
            return {"received": True}

        # Ingress designer — type selection → refresh profiles list
        if target == "nav.debug.ingress_type" and action in ("action", "valueChanged", "clicked") and client is not None:
            type_name = payload.get("action") or ""
            app_state["debug_ingress_type"] = type_name
            app_state["debug_ingress_selected_profile"] = ""
            c = client
            _deferred(lambda tn=type_name: _refresh_ingress_profiles_list(c, tn))
            return {"received": True}

        # Ingress designer — add profile button
        if target == "nav.debug.ingress_add_btn" and action in ("action", "clicked") and client is not None:
            type_name = app_state.get("debug_ingress_type", "")
            if not type_name:
                cached = app_state.get("debug_ingress_types_cache") or []
                type_name = cached[0]["id"] if cached else ""
            c = client
            if type_name:
                _deferred(lambda tn=type_name: _open_ingress_profile_editor(c, tn))
            return {"received": True}

        # Queue selected ingress packet into epavm
        if target == "nav.debug.ingress_queue_btn" and action in ("action", "clicked"):
            selected_profile = app_state.get("debug_ingress_selected_profile", "")
            type_name = app_state.get("debug_ingress_type", "")
            if not type_name:
                cached = app_state.get("debug_ingress_types_cache") or []
                type_name = cached[0]["id"] if cached else ""
            kernel_id = app_state.get("debug_ingress_kernel", "")
            sel_worker = app_state.get("debug_ingress_worker", "")
            c = client
            def _do_queue(tn=type_name, pn=selected_profile, kid=kernel_id, sw=sel_worker):
                if not tn or not pn or not kid:
                    return
                profiles_dir = _profiles_dir(tn)
                if not profiles_dir:
                    return
                profile_path = profiles_dir / f"{pn}.json"
                if not profile_path.is_file():
                    return
                try:
                    profile_data = json.loads(profile_path.read_text(encoding="utf-8"))
                except Exception:
                    return
                field_values = profile_data.get("fields", profile_data)
                import struct as _struct
                type_defs = _parse_type_defs()
                field_defs = type_defs.get(tn, []) or []
                byte_specs = profile_data.get("serialized_byte_fields", {}) or {}
                computed_values = {}
                field_bytes = bytearray()

                for field_def in field_defs:
                    field_type = str(field_def.get("type", "") or "")
                    field_name = str(field_def.get("name", "") or "")
                    if not field_name:
                        continue
                    if field_type == "byte[]":
                        raw_bytes, computed = _encode_profile_byte_field(field_name, byte_specs.get(field_name))
                        computed_values.update(computed or {})
                        field_bytes.extend(raw_bytes)
                        continue
                    value = computed_values.get(field_name, field_values.get(field_name, 0))
                    try:
                        int_value = int(value, 0) if isinstance(value, str) else int(value)
                    except Exception:
                        int_value = 0
                    field_bytes.extend(_struct.pack("<I", int_value & 0xFFFFFFFF))

                if not field_defs:
                    for v in field_values.values():
                        try:
                            int_value = int(v, 0) if isinstance(v, str) else int(v)
                        except Exception:
                            int_value = 0
                        field_bytes.extend(_struct.pack("<I", int_value & 0xFFFFFFFF))

                hex_bytes = bytes(field_bytes).hex()
                dbg_c = _epa_dbg_client()
                if not _epa_dbg_running() or not dbg_c:
                    if not _start_debug_vm(c, force_restart=False):
                        return
                    dbg_c = _epa_dbg_client()
                if not dbg_c:
                    return
                bundle_path = _bundle_path_for_kernel_id(kid)
                if not bundle_path or not Path(bundle_path).is_file():
                    return
                if app_state.get("debug_kernel_loaded") != bundle_path:
                    previously_loaded = _kernel_tab_id_from_bundle(app_state.get("debug_kernel_loaded", ""))
                    if previously_loaded and previously_loaded != kid and c:
                        _clear_kernel_queue_state(c, previously_loaded)
                        _set_kernel_run_indicator(c, previously_loaded, _IND_OFF)
                    result = _epa_dbg_load_bundle(bundle_path, kernel_id=0)
                    if not result.get("ok"):
                        err_msg = result.get("error", "failed to load debug bundle")
                        _epa_dbg_log(f"[error] load_bundle failed: {err_msg}")
                        _set_kernel_load_indicator(c, kid, _IND_RED)
                        raise RuntimeError(err_msg)
                    app_state["debug_kernel_loaded"] = bundle_path
                    _set_kernel_load_indicator(c, kid, _IND_GREEN)
                app_state["debug_active_kernel"] = kid
                # Resolve wid from selected worker name (wid=0 is coordinator, workers start at 1)
                workers = app_state.get(f"debug_kernel_workers_{kid}", [])
                wid = 1  # default to first worker
                if sw:
                    for idx, w in enumerate(workers):
                        if w.get("name") == sw:
                            wid = idx + 1
                            break
                try:
                    dbg_c.ingress_push_hex(hex_bytes, wid=wid, path_id=kid)
                    if c:
                        worker_label = sw or f"wid={wid}"
                        field_summary = ", ".join(
                            f"{name}={value}"
                            for name, value in field_values.items()
                        )
                        _append_host_io_output(
                            c,
                            f"[ingress] {kid} {worker_label} {tn}"
                            + (f" {pn}" if pn else "")
                            + (f" {field_summary}" if field_summary else "")
                            + "\n",
                        )
                    snap = dbg_c.snapshot(0, path_id=kid)
                    if c and kid:
                        _update_queue_counters(c, snap, kid)
                        _update_kernel_indicator_from_snapshot(c, kid, snap)
                except Exception as exc:
                    _epa_dbg_log(f"[error] queue_ingress: {exc}")
                    _push_exception(exc, "queue_ingress")
            _deferred(_do_queue)
            return {"received": True}

        # Profile editor — field selected in tree
        if target == "ipe.tree" and action == "action" and client is not None:
            node_id = payload.get("action") or ""
            if node_id.startswith("ipe.field."):
                field_name = node_id[len("ipe.field."):]
                ingress_editor_state["selected_field"] = field_name
                cur_val = ingress_editor_state.get("field_values", {}).get(field_name, "0")
                ingress_editor_state["field_input_text"] = cur_val
                c = client
                def _update_form(fname=field_name, fval=cur_val):
                    try:
                        c.set_text("ipe.field_label", fname)
                        c.set_text("ipe.field_input", fval)
                    except Exception:
                        pass
                _deferred(_update_form)
            return {"received": True}

        # Profile editor — field input events (keysTyped fires; textChanged is fallback)
        if target == "ipe.field_input":
            field_name = ingress_editor_state.get("selected_field", "")
            if action == "keysTyped":
                cur = ingress_editor_state.get("field_input_text", "")
                new_text = cur + payload.get("text", "")
                ingress_editor_state["field_input_text"] = new_text
                if field_name:
                    ingress_editor_state.setdefault("field_values", {})[field_name] = new_text
            elif action == "keyDown" and payload.get("key") == "Backspace":
                cur = ingress_editor_state.get("field_input_text", "")
                new_text = cur[:-1] if cur else ""
                ingress_editor_state["field_input_text"] = new_text
                if field_name:
                    ingress_editor_state.setdefault("field_values", {})[field_name] = new_text
            elif action == "textChanged":
                new_text = payload.get("text", "")
                ingress_editor_state["field_input_text"] = new_text
                if field_name:
                    ingress_editor_state.setdefault("field_values", {})[field_name] = new_text
            return {"received": True}

        # Profile editor — name input events (text read via snapshot_widget at save time)
        if target == "ipe.name_input":
            return {"received": True}

        # Profile editor — save
        if target == "ipe.save" and action in ("action", "clicked") and client is not None:
            type_name = ingress_editor_state.get("type_name", "")
            c = client
            def _do_save(tn=type_name):
                # Read widget text directly — text input events don't fire from secondary windows
                try:
                    name_snap = c.snapshot_widget("ipe.name_input")
                    pn = ((name_snap or {}).get("state") or {}).get("text", "").strip()
                except Exception:
                    pn = ""
                # Flush current field_input value into field_values for whichever field is selected
                sel_field = ingress_editor_state.get("selected_field", "")
                if sel_field:
                    try:
                        fi_snap = c.snapshot_widget("ipe.field_input")
                        fi_val = ((fi_snap or {}).get("state") or {}).get("text", "")
                        ingress_editor_state.setdefault("field_values", {})[sel_field] = fi_val
                    except Exception:
                        pass
                fv = dict(ingress_editor_state.get("field_values", {}))
                _epa_dbg_log(f"[ipe.save] type={tn!r} name={pn!r} fields={list(fv.keys())}")
                if not tn:
                    _epa_dbg_log("[ipe.save] no type_name — aborting")
                    return
                if not pn:
                    _epa_dbg_log("[ipe.save] empty profile name — showing error")
                    try:
                        c.set_text("ipe.name_label", "Profile name (required):")
                        c.fire("ui.setForegroundColor", {"target": "ipe.name_label", "color": "#cc3333"})
                    except Exception as exc:
                        _epa_dbg_log(f"[ipe.save] label update failed: {exc}")
                    return
                d = _profiles_dir(tn)
                if d is None:
                    _epa_dbg_log("[ipe.save] no project open — cannot save profile")
                    try:
                        c.set_text("ipe.name_label", "Open a project first:")
                        c.fire("ui.setForegroundColor", {"target": "ipe.name_label", "color": "#cc3333"})
                    except Exception:
                        pass
                    return
                _save_ingress_profile(tn, pn, fv)
                _epa_dbg_log(f"[ipe.save] saved profile {pn!r} for type {tn!r}")
                try:
                    c.close_window("ingress-profile-editor")
                except Exception:
                    pass
                _refresh_ingress_profiles_list(c, tn)
            _deferred(_do_save)
            return {"received": True}

        # Profile editor — cancel
        if target == "ipe.cancel" and action in ("action", "clicked") and client is not None:
            c = client
            _deferred(lambda: c.close_window("ingress-profile-editor"))
            return {"received": True}

        # AI model selection (combo box fires action event with item id)
        if target == "ai.model" and action in ("action", "valueChanged", "clicked"):
            model_id = payload.get("action") or payload.get("id") or payload.get("text")
            if model_id and any(m["id"] == model_id for m in AI_MODELS):
                ai_state["model"] = model_id
                _persist_ai_config()
            return {"received": True}

        if action == "textChanged" and target == "ai.codex_cli":
            ai_state["codex_cli"] = payload.get("text", "") or "codex"
            _persist_ai_config()
            return {"received": True}

        if action == "textChanged" and target == "ai.codex_args":
            ai_state["codex_args"] = payload.get("text", "")
            _persist_ai_config()
            return {"received": True}

        # Track AI input text
        if action == "textChanged" and target == "ai.input":
            ai_state["input_text"] = payload.get("text", "")
            return {"received": True}

        # Ctrl+Enter in ai.input sends the message
        if action == "keyDown" and target == "ai.input":
            keyval = payload.get("keyval", 0)
            modifiers = payload.get("modifiers", 0)
            if keyval in (0xff0d, 0x0000ff0d) and (modifiers & 4):  # Ctrl+Enter
                if not ai_state.get("generating") and ai_state.get("input_text", "").strip():
                    _deferred(_ai_send)
                return {"received": True}

        if action not in ("mouseMove", "mouseDown", "mouseUp"):
            print(json.dumps({"ui.event": params}, indent=2), flush=True)
        return {"received": True}

    _init_editor_state()
    artifact_root = Path(__file__).resolve().parent / "artifacts"
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(build_document().to_json(indent=2), encoding="utf-8")
    worker = None
    _first_connect = True
    try:
      while True:  # reconnect loop — continues only when _ui_server manages the process
        try:
          # Rebuild the document each time so reconnects use current saved layout state.
          initial_ide_state = _current_layout_state()
          builder = build_document()
          _codec = "json" if args.json_rpc else "brpc"
          with ElaraUiRpcClient(args.host, args.port, codec=_codec) as client:
            client_ref["client"] = client
            _ensure_status_ping_loop()
            client.set_find_widget_artifact_root(str(artifact_root))
            if args.event_log:
                client.set_event_log(args.event_log)
            # Open per-session event log file.
            _artifacts_dir = Path(__file__).resolve().parent / "artifacts"
            _artifacts_dir.mkdir(exist_ok=True)
            _session_log_path = _artifacts_dir / f"event-{time.strftime('%Y%m%d-%H%M%S')}.jsonl"
            try:
                _event_log_fh[0] = open(_session_log_path, "a", encoding="utf-8")
            except Exception:
                pass
            _push_event("session_start", host=args.host, port=args.port,
                        log_file=str(_session_log_path))

            # Wrap client.call so every outgoing UI RPC call is logged.
            # This is the single most useful trace for reproducing UI crashes.
            _orig_client_call = client.call
            def _logged_client_call(method, params=None, timeout=5.0):
                _push_event("ui_rpc_out", method=method,
                            params=_trim_for_log(params) if isinstance(params, dict) else params)
                try:
                    return _orig_client_call(method, params, timeout)
                except Exception as _rpc_exc:
                    _push_event("ui_rpc_out_error", method=method, error=str(_rpc_exc))
                    raise
            client.call = _logged_client_call

            # Inject remaining functions into ctx for use by extracted subsystem modules
            ctx["_push_event"]                    = _push_event
            ctx["_push_exception"]                = _push_exception
            ctx["_deferred"]                      = _deferred
            ctx["_refresh_status_panel"]          = _refresh_status_panel
            ctx["_append_build_output"]           = _append_build_output
            ctx["_append_host_io_output"]         = _append_host_io_output
            ctx["_open_file_tab"]                 = _open_file_tab
            ctx["_tab_id_for_path"]               = _tab_id_for_path
            ctx["_persist_editor_session_state"]  = _persist_editor_session_state
            ctx["_write_debug_session_descriptor"] = _write_debug_session_descriptor
            ctx["_project_cpp_binary"]            = _project_cpp_binary
            ctx["_project_cpp_gdb_args"]          = _project_cpp_gdb_args
            ctx["_project_cpp_root"]              = _project_cpp_root
            ctx["_project_python_root"]           = _project_python_root
            ctx["_allocate_epa_dbg_port"]         = _allocate_epa_dbg_port

            client.add_handler("ui.event", on_ui_event)
            # Apply window state first — before the document is loaded (which shows the window).
            _saved_win = (initial_ide_state.get("window", {}) if isinstance(initial_ide_state, dict) else {})
            if bool(_saved_win.get("maximized", False)):
                try:
                    client.set_window_maximized(True)
                except Exception:
                    pass
            load_result = client.load_document(builder)
            print(json.dumps(load_result, indent=2))
            _restore_editor_session_state(client)
            try:
                client.set_visible_batch([
                    ("editor.tabs", bool(tab_list)),
                    ("editor.welcome", not bool(tab_list)),
                ])
            except Exception:
                pass
            if not app_state.get("project_root"):
                try:
                    client.fire("ui.setVisible", {"target": "app.toolbar", "visible": True})
                except Exception:
                    pass
                try:
                    _set_project_toolbar_enabled(client, False)
                except Exception:
                    pass
                for panel in _NAV_PANELS.values():
                    try:
                        client.fire("ui.setVisible", {"target": panel, "visible": False})
                    except Exception:
                        pass
                try:
                    client.fire("ui.setVisible", {"target": "nav.panel", "visible": True})
                    client.fire("ui.setVisible", {"target": "nav.file_tabs", "visible": False})
                    client.fire("ui.setVisible", {"target": "nav.no_project", "visible": True})
                except Exception:
                    pass
            else:
                try:
                    _set_project_toolbar_enabled(client, True)
                except Exception:
                    pass
                _switch_nav_view(client, app_state.get("nav_view", "files"))
            for tab_id in editor_state:
                _refresh_e_tab(client, tab_id)
            if app_state.get("active_editor_tab"):
                _refresh_debug_sidebars(client, app_state["active_editor_tab"])
            _epa_dbg_set_vm_button(_epa_dbg_running())
            try:
                _apply_ai_config_ui(client)
                client.set_text("ai.history", _ai_format_history())
            except Exception:
                pass
            snapshot_sections = builder.snapshot_client_sections() if hasattr(builder, "snapshot_client_sections") else {}
            dumper = UiSnapshotDumper(client, client_sections=snapshot_sections)
            if args.snapshot:
                snapshot = dumper.snapshot()
                print(json.dumps(snapshot, indent=2, ensure_ascii=False))
            if args.dump_snapshot:
                path = dumper.dump(args.snapshot_out)
                print(json.dumps({"snapshot_written": str(path)}, indent=2), flush=True)
            if not args.no_events:
                for action in ("clicked", "keysTyped", "textChanged", "valueChanged", "keyDown", "keyUp", "action"):
                    client.enable_event(action)
                _register_event_responders(client)

            # Wire AI RPC callbacks now that all closures exist and client is live.
            # Only define and start once; across reconnects the callbacks stay valid
            # because they all reference client_ref (the shared dict), not client directly.
            if args.ai_rpc_port and _first_connect:
                def _ai_rpc_compile_tab(tab_id: str) -> dict:
                    state = editor_state.get(tab_id)
                    if not state:
                        return {"tab_id": tab_id, "asm": "", "error": "no_such_tab", "compiled_at": 0.0}
                    source_text = state.get("source_text", "")
                    tab_entry = next((t for t in tab_list if t.get("tab_id") == tab_id), None)
                    source_dir = Path(tab_entry["path"]).parent if tab_entry and tab_entry.get("path") else None
                    try:
                        result = _compile_e_source(source_text, source_dir)
                    except Exception as exc:
                        return {"tab_id": tab_id, "asm": "", "error": str(exc), "compiled_at": 0.0}
                    ts = time.time()
                    state["epa_text"] = result["epa_text"]
                    state["epa_block_map"] = result.get("epa_block_map", {})
                    state["epa_map_path"] = _persist_debug_map(tab_id, result.get("epa_map_text", "")) if result.get("ok") else ""
                    state["compile_error"] = result.get("message", "")
                    state["epa_compiled_at"] = ts
                    state["epa_error"] = result.get("message") if not result["ok"] else None
                    c = client_ref.get("client")
                    if c:
                        try:
                            ids = _editor_ids(tab_id)
                            epa_out = result["epa_text"] if result["ok"] else ""
                            c.set_text(ids["epa"], epa_out)
                            c.set_code_editor_diagnostics(ids["source"], result.get("diagnostics", []))
                        except Exception:
                            pass
                    return {
                        "tab_id": tab_id,
                        "asm": result["epa_text"],
                        "error": result.get("message") if not result["ok"] else None,
                        "compiled_at": ts,
                    }

                def _ai_rpc_open_file(path: str):
                    c = client_ref.get("client")
                    if not c:
                        return None
                    # Check if already open
                    existing = next((t for t in tab_list if t.get("path") == path), None)
                    if existing:
                        return existing["tab_id"]
                    _open_file_tab(c, path, True)
                    found = next((t for t in tab_list if t.get("path") == path), None)
                    return found["tab_id"] if found else None

                def _ai_rpc_switch_view(tab_id: str, view: str):
                    state = editor_state.get(tab_id)
                    if state:
                        state["view"] = view
                    c = client_ref.get("client")
                    if c:
                        _deferred(lambda: _apply_editor_view(c, tab_id, set_focus=False))

                def _ai_rpc_set_editor_content(tab_id: str, content: str):
                    state = editor_state.get(tab_id)
                    if state:
                        state["source_text"] = content
                    c = client_ref.get("client")
                    if c:
                        try:
                            ids = _editor_ids(tab_id)
                            c.set_text(ids["source"], content)
                        except Exception:
                            pass

                def _ai_rpc_set_active_tab(tab_id: str):
                    t = next((x for x in tab_list if x.get("tab_id") == tab_id), None)
                    if not t:
                        return
                    c = client_ref.get("client")
                    if c:
                        try:
                            c.fire("ui.setActiveTab", {"target": "editor.tabs", "index": t["index"]})
                        except Exception:
                            pass
                    app_state["active_editor_tab"] = tab_id

                def _ai_rpc_close_tab(tab_id: str):
                    t = next((x for x in tab_list if x.get("tab_id") == tab_id), None)
                    if not t:
                        return
                    c = client_ref.get("client")
                    if c:
                        try:
                            c.fire("ui.removeTab", {"target": "editor.tabs", "index": t["index"]})
                        except Exception:
                            pass
                    tab_list[:] = [x for x in tab_list if x.get("tab_id") != tab_id]
                    editor_state.pop(tab_id, None)
                    _persist_editor_session_state(allow_empty_tabs=True)

                def _ai_rpc_get_nav_tree() -> list:
                    return app_state.get("nav_tree_nodes", [])

                def _ai_rpc_set_node_expanded(node_id: str, expanded: bool) -> bool:
                    per_tech = app_state.get("nav_tree_nodes_per_tech", {})
                    if not per_tech:
                        return False

                    def _toggle(node_list):
                        for node in node_list:
                            if node.get("id") == node_id:
                                node["expanded"] = expanded
                                return True
                            if _toggle(node.get("children", [])):
                                return True
                        return False

                    _tech_tree_ids = {
                        "epa": "nav.tree.epa",
                        "cpp": "nav.tree.cpp",
                        "python": "nav.tree.python",
                        _NAV_ARTIFACTS_TECH: _NAV_3D_TREE_ID,
                        _NAV_LEVEL_PATHS_TECH: _NAV_3D_TREE_ID,
                    }
                    for tech, tech_nodes in per_tech.items():
                        if _toggle(tech_nodes):
                            c = client_ref.get("client")
                            if c:
                                try:
                                    tree_id = _tech_tree_ids[tech]
                                    document = json.dumps({"nodes": tech_nodes}, separators=(",", ":"))
                                    c.call("ui.replaceChildren", {"target": tree_id, "document": document})
                                except Exception:
                                    pass
                            return True
                    return False

                def _ai_rpc_tree_open_file(path: str) -> str:
                    c = client_ref.get("client")
                    if not c:
                        return ""
                    _open_file_tab(c, path, True)
                    found = next((t for t in tab_list if t.get("path") == path), None)
                    return found["tab_id"] if found else ""

                def _ai_rpc_editor_replace_range(
                    tab_id: str,
                    from_line: int, from_col: int,
                    to_line: int, to_col: int,
                    replacement: str,
                ) -> dict:
                    state = editor_state.get(tab_id)
                    if not state:
                        raise KeyError(f"no_such_tab: {tab_id}")
                    text = state.get("source_text", "")
                    lines = text.split("\n")

                    def _to_offset(line_1, col_0):
                        line_1 = max(1, min(line_1, len(lines)))
                        col_0 = max(0, min(col_0, len(lines[line_1 - 1])))
                        return sum(len(lines[i]) + 1 for i in range(line_1 - 1)) + col_0

                    from_off = _to_offset(from_line, from_col)
                    to_off = _to_offset(to_line, to_col)
                    if from_off > to_off:
                        from_off, to_off = to_off, from_off
                    new_text = text[:from_off] + replacement + text[to_off:]
                    state["source_text"] = new_text
                    c = client_ref.get("client")
                    if c:
                        try:
                            ids = _editor_ids(tab_id)
                            c.set_text(ids["source"], new_text)
                        except Exception:
                            pass
                    new_lines = new_text.split("\n")
                    ins_chars = len(replacement)
                    ins_end_off = from_off + ins_chars
                    ins_end_line = new_text[:ins_end_off].count("\n") + 1
                    ins_end_col = ins_end_off - new_text[:ins_end_off].rfind("\n") - 1
                    return {
                        "tab_id": tab_id,
                        "lines_total": len(new_lines),
                        "chars_total": len(new_text),
                        "cursor_line": ins_end_line,
                        "cursor_col": ins_end_col,
                    }

                def _ai_rpc_ui_call(method: str, params: dict):
                    c = client_ref.get("client")
                    if not c:
                        raise RuntimeError("ui_unavailable: UI client not connected")
                    return c.call(f"ui.{method}", params)

                def _ai_rpc_open_project(path: str) -> dict:
                    c = client_ref.get("client")
                    if not c:
                        raise RuntimeError("ui_unavailable: UI client not connected")
                    _open_project(c, path, restore_session=True)
                    return {
                        "project_root": app_state.get("project_root", ""),
                        "project_name": app_state.get("project_name", ""),
                    }

                def _ai_rpc_get_exceptions(limit: int = 50) -> list:
                    with _exception_log_lock:
                        entries = list(_exception_log)
                    return entries[-limit:] if limit > 0 else entries

                def _ai_rpc_clear_exceptions() -> dict:
                    with _exception_log_lock:
                        count = len(_exception_log)
                        _exception_log.clear()
                    return {"cleared": count}

                def _ai_rpc_get_ui_status() -> dict:
                    c = client_ref.get("client")
                    proc = _ui_server.get("proc")
                    pid = proc.pid if proc else None
                    alive = proc is not None and proc.poll() is None
                    recent_output = list(_ui_server.get("output_lines", []))[-50:]
                    return {
                        "connected": bool(c and c._running),
                        "managed_process": proc is not None,
                        "process_alive": alive,
                        "process_pid": pid,
                        "server_cmd": _ui_server.get("cmd", []),
                        "recent_output": recent_output,
                    }

                def _ai_rpc_get_status_indicators() -> dict:
                    epa_proc = _epa_dbg.get("proc")
                    epa_client = _epa_dbg.get("client")
                    epa_port = _epa_dbg.get("port")
                    if not epa_proc or epa_proc.poll() is not None:
                        epa_primary = {"state": _IND_RED, "label": "Offline"}
                        epa_secondary = {"state": _IND_OFF, "label": "—"}
                        epa_port_text = "Port: —"
                    elif not epa_client or not epa_client.connected:
                        epa_primary = {"state": _IND_ORANGE, "label": "Listening"}
                        epa_secondary = {"state": _IND_RED, "label": "IDE not connected"}
                        epa_port_text = f"Port: {epa_port}"
                    else:
                        epa_primary = {"state": _IND_GREEN, "label": "Running"}
                        epa_secondary = {"state": _IND_GREEN, "label": "IDE connected"}
                        epa_port_text = f"Port: {epa_port}"
                    ide_connected = bool(epa_client and epa_client.connected)

                    bridge_server = host_debug_bridge.get("server")
                    bridge_port = host_debug_bridge.get("port")
                    bridge_connected = host_debug_bridge.get("client_connected", False)
                    ext_logic_connected = host_debug_bridge.get("external_logic_connected", False)
                    ext_logic_proxied = host_debug_bridge.get("ext_logic_proxied", False)
                    now_ts = time.time()
                    host_ping_fresh = (now_ts - float(host_debug_bridge.get("last_host_pong", 0.0) or 0.0)) < 3.5
                    ext_ping_fresh = (now_ts - float(host_debug_bridge.get("last_ext_pong", 0.0) or 0.0)) < 3.5
                    host_ingress_count = int(app_state.get("host_interconnect_ingress_count", 0) or 0)
                    host_egress_count = int(app_state.get("host_interconnect_egress_count", 0) or 0)
                    if not bridge_server:
                        host_primary = {"state": _IND_RED, "label": "Offline"}
                        host_secondary = {"state": _IND_OFF, "label": "—"}
                        host_port_text = "Port: —"
                    else:
                        if ide_connected and bridge_connected:
                            host_primary = {"state": _IND_GREEN, "label": f"EPA host  in={host_ingress_count}"}
                        elif ide_connected or bridge_connected:
                            host_primary = {"state": _IND_ORANGE, "label": f"EPA host  in={host_ingress_count}"}
                        else:
                            host_primary = {"state": _IND_OFF, "label": f"EPA host  in={host_ingress_count}"}
                        if ext_logic_proxied:
                            host_secondary = {"state": _IND_GREEN, "label": f"External logic  out={host_egress_count}"}
                        elif ext_logic_connected:
                            host_secondary = {"state": _IND_ORANGE, "label": f"External logic  out={host_egress_count}"}
                        else:
                            host_secondary = {"state": _IND_OFF, "label": f"External logic  out={host_egress_count}"}
                        host_port_text = f"Port: {bridge_port}" if bridge_port else "Port: —"
                    host_ping = {
                        "host_bridge": _IND_GREEN if host_ping_fresh and bridge_connected else (_IND_RED if bridge_connected else _IND_OFF),
                        "external_logic": _IND_GREEN if (ext_logic_connected and ext_ping_fresh) else (_IND_RED if ext_logic_connected else _IND_OFF),
                    }

                    gdb_proc = cpp_gdb_state.get("proc")
                    gdb_status = str(cpp_gdb_state.get("status", "") or "")
                    if not gdb_proc:
                        cpp_primary = {"state": _IND_RED, "label": "GDB"}
                        cpp_secondary = {"state": _IND_OFF, "label": "Target"}
                        cpp_port_text = "PID: —"
                    elif gdb_proc.poll() is not None:
                        cpp_primary = {"state": _IND_RED, "label": "GDB"}
                        cpp_secondary = {"state": _IND_RED, "label": "Target exited"}
                        cpp_port_text = f"PID: {gdb_proc.pid}"
                    elif "waiting for gdb prompt" in gdb_status.lower() or "launch" in gdb_status.lower():
                        cpp_primary = {"state": _IND_ORANGE, "label": "GDB"}
                        cpp_secondary = {"state": _IND_ORANGE, "label": "Target"}
                        cpp_port_text = f"PID: {gdb_proc.pid}"
                    else:
                        cpp_primary = {"state": _IND_GREEN, "label": "GDB"}
                        cpp_secondary = {"state": _IND_GREEN, "label": "Target running" if cpp_gdb_state.get("running") else "Target stopped"}
                        cpp_port_text = f"PID: {gdb_proc.pid}"

                    py_started = python_dbg_state.get("started", False)
                    py_port = args.ai_rpc_port if hasattr(args, "ai_rpc_port") and args.ai_rpc_port else None
                    if not py_started:
                        py_primary = {"state": _IND_RED, "label": "Not started"}
                        py_secondary = {"state": _IND_OFF, "label": "—"}
                    else:
                        py_primary = {"state": _IND_GREEN, "label": "Started"}
                        py_secondary = {"state": _IND_GREEN, "label": "Streaming active"}
                    py_port_text = f"Port: {py_port}" if py_port else "Port: —"

                    return {
                        "epa": {
                            "primary": epa_primary,
                            "secondary": epa_secondary,
                            "port_text": epa_port_text,
                            "process_alive": bool(epa_proc and epa_proc.poll() is None),
                            "connected": bool(epa_client and epa_client.connected),
                        },
                        "host": {
                            "primary": host_primary,
                            "secondary": host_secondary,
                            "port_text": host_port_text,
                            "ping": host_ping,
                            "bridge_connected": bool(bridge_connected),
                            "external_logic_connected": bool(ext_logic_connected),
                            "external_logic_proxied": bool(ext_logic_proxied),
                            "ingress_count": host_ingress_count,
                            "egress_count": host_egress_count,
                        },
                        "cpp": {
                            "primary": cpp_primary,
                            "secondary": cpp_secondary,
                            "port_text": cpp_port_text,
                            "running": bool(cpp_gdb_state.get("running")),
                            "status": gdb_status,
                        },
                        "python": {
                            "primary": py_primary,
                            "secondary": py_secondary,
                            "port_text": py_port_text,
                            "started": bool(py_started),
                            "status": str(python_dbg_state.get("status", "") or ""),
                        },
                    }

                _ai_rpc_control_seq = [0]

                def _ai_rpc_runtime_control(target: str, action: str) -> dict:
                    c = client_ref.get("client")
                    if c is None:
                        raise RuntimeError("ui_unavailable: UI client is not connected")
                    target_key = str(target or "").strip().lower()
                    action_key = str(action or "").strip().lower()
                    if not target_key:
                        raise RuntimeError("missing_param: target")
                    if not action_key:
                        raise RuntimeError("missing_param: action")

                    target_aliases = {
                        "host": "cpp",
                        "cpp": "cpp",
                        "c++": "cpp",
                        "python": "python",
                        "ext_logic": "python",
                        "external_logic": "python",
                        "epa": "epa_vm",
                        "epa_vm": "epa_vm",
                        "vm": "epa_vm",
                    }
                    normalized_target = target_aliases.get(target_key, "")
                    if not normalized_target:
                        raise RuntimeError(f"invalid_target: {target}")

                    _ai_rpc_control_seq[0] += 1
                    request_id = f"runtime-{_ai_rpc_control_seq[0]}"
                    _push_event(
                        "ai_runtime_control_queued",
                        request_id=request_id,
                        target=normalized_target,
                        action=action_key,
                    )

                    def _run_control():
                        try:
                            if normalized_target == "cpp":
                                action_map = {
                                    "launch": "debug.cpp.reset",
                                    "reset": "debug.cpp.reset",
                                    "stop": "debug.cpp.stop",
                                    "kill": "debug.cpp.stop",
                                    "continue": "debug.cpp.continue",
                                    "pause": "debug.cpp.pause",
                                    "step_over": "debug.cpp.step_over",
                                    "step_into": "debug.cpp.step_into",
                                    "step_out": "debug.cpp.step_out",
                                }
                                mapped = action_map.get(action_key, "")
                                if not mapped:
                                    raise RuntimeError(f"invalid_action_for_cpp: {action_key}")
                                _cpp_gdb_handle_action(c, mapped)
                            elif normalized_target == "python":
                                action_map = {
                                    "launch": "debug.python.reset",
                                    "reset": "debug.python.reset",
                                    "stop": "debug.python.stop",
                                    "kill": "debug.python.stop",
                                    "continue": "debug.python.continue",
                                    "pause": "debug.python.pause",
                                    "step_over": "debug.python.step_over",
                                    "step_into": "debug.python.step_into",
                                    "step_out": "debug.python.step_out",
                                }
                                mapped = action_map.get(action_key, "")
                                if not mapped:
                                    raise RuntimeError(f"invalid_action_for_python: {action_key}")
                                _python_dbg_handle_action(c, mapped)
                            elif normalized_target == "epa_vm":
                                if action_key == "launch":
                                    if not _start_debug_vm(c, force_restart=False):
                                        raise RuntimeError("epa_vm_start_failed")
                                elif action_key == "reset":
                                    if not _start_debug_vm(c, force_restart=True):
                                        raise RuntimeError("epa_vm_reset_failed")
                                elif action_key in ("stop", "kill"):
                                    _epa_dbg_stop()
                                    _refresh_status_panel(c)
                                else:
                                    raise RuntimeError(f"invalid_action_for_epa_vm: {action_key}")
                            _push_event(
                                "ai_runtime_control_done",
                                request_id=request_id,
                                target=normalized_target,
                                action=action_key,
                            )
                        except Exception as exc:
                            _push_event(
                                "ai_runtime_control_error",
                                request_id=request_id,
                                target=normalized_target,
                                action=action_key,
                                error=str(exc),
                            )

                    _deferred(_run_control)
                    return {
                        "queued": True,
                        "request_id": request_id,
                        "target": normalized_target,
                        "action": action_key,
                    }

                def _ai_rpc_restart_ui(use_gdb: bool = False) -> dict:
                    proc = _ui_server.get("proc")
                    if proc and proc.poll() is None:
                        proc.terminate()
                        try:
                            proc.wait(timeout=4)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.wait()

                    cmd = _resolve_ui_server_cmd()
                    _ui_server["output_lines"] = []
                    if use_gdb:
                        cmd = [
                            "gdb", "--batch",
                            "-ex", "handle SIGPIPE nostop noprint",
                            "-ex", "run",
                            "-ex", "thread apply all bt full",
                            "--args",
                        ] + cmd

                    out_lines = _ui_server["output_lines"]

                    def _reader(stream, tag):
                        for raw in stream:
                            line = raw.decode(errors="replace").rstrip()
                            out_lines.append(f"[{tag}] {line}")
                            if len(out_lines) > 500:
                                del out_lines[:len(out_lines) - 500]

                    new_proc = subprocess.Popen(
                        cmd,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                    )
                    threading.Thread(target=_reader, args=(new_proc.stdout, "stdout"), daemon=True).start()
                    threading.Thread(target=_reader, args=(new_proc.stderr, "stderr"), daemon=True).start()
                    _ui_server["proc"] = new_proc
                    _ui_server["cmd"] = cmd
                    app_state["_ui_reconnect_requested"] = True
                    return {"pid": new_proc.pid, "cmd": cmd, "use_gdb": use_gdb}

                def _ai_rpc_reload_ui_document() -> dict:
                    c = client_ref.get("client")
                    if c is None or not getattr(c, "_running", False):
                        raise RuntimeError("UI client is not connected")
                    builder = build_document()
                    c.load_document(builder)
                    _restore_editor_session_state(c)
                    try:
                        c.set_visible_batch([
                            ("editor.tabs", bool(tab_list)),
                            ("editor.welcome", not bool(tab_list)),
                        ])
                    except Exception:
                        pass
                    if app_state.get("project_root"):
                        _set_project_toolbar_enabled(c, True)
                        _switch_nav_view(c, app_state.get("nav_view", "files"))
                    else:
                        _set_project_toolbar_enabled(c, False)
                    for tab_id in editor_state:
                        _refresh_e_tab(c, tab_id)
                    if app_state.get("active_editor_tab"):
                        _refresh_debug_sidebars(c, app_state["active_editor_tab"])
                    _apply_ai_config_ui(c)
                    _refresh_status_panel(c)
                    return {"loaded": True}

                def _ai_rpc_trigger_action(action_id: str, target: str, payload: dict) -> dict:
                    params = {
                        "action": "action",
                        "target": target or action_id,
                        "payload": dict(payload or {}),
                    }
                    params["payload"].setdefault("action", action_id)
                    return on_ui_event(params)

                def _ai_rpc_ext_call(method: str, params: dict, timeout: float = 10.0):
                    if not method:
                        raise RuntimeError("missing_param: method")
                    port = _external_logic_bridge_ensure()
                    if not port:
                        raise RuntimeError("ext_logic_unavailable: bridge did not start")

                    def _recv_n(sock: socket.socket, n: int) -> bytes:
                        buf = bytearray()
                        while len(buf) < n:
                            chunk = sock.recv(n - len(buf))
                            if not chunk:
                                raise EOFError("connection closed")
                            buf.extend(chunk)
                        return bytes(buf)

                    req_id = str(uuid.uuid4())
                    body = _BRpcCodec.encode({
                        "id": req_id,
                        "method": method,
                        "params": dict(params or {}),
                    })
                    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
                        sock.settimeout(float(timeout))
                        sock.connect(("127.0.0.1", int(port)))
                        sock.sendall(struct.pack(">I", len(body)) + body)
                        header = _recv_n(sock, 4)
                        length = struct.unpack(">I", header)[0]
                        if length > 16 * 1024 * 1024:
                            raise RuntimeError(f"ext_logic_frame_too_large: {length}")
                        resp = _BRpcCodec.decode(_recv_n(sock, length))
                    if str(resp.get("id", "")) != req_id:
                        raise RuntimeError(f"ext_logic_response_id_mismatch: {resp.get('id')} != {req_id}")
                    if not resp.get("ok", False):
                        err = resp.get("error", {})
                        code = err.get("code", "error")
                        message = err.get("message", "")
                        raise RuntimeError(f"{code}: {message}")
                    return resp.get("result")

                def _ai_rpc_get_event_log(limit: int = 100, type_filter: str = "") -> list:
                    with _event_log_lock:
                        entries = list(_event_log)
                    if type_filter:
                        entries = [e for e in entries if e.get("type") == type_filter]
                    if limit and len(entries) > limit:
                        entries = entries[-limit:]
                    return entries

                def _ai_rpc_clear_event_log() -> dict:
                    with _event_log_lock:
                        n = len(_event_log)
                        _event_log.clear()
                    return {"cleared": n}

                def _ai_rpc_list_ingress_types() -> dict:
                    return _parse_type_defs()

                def _ai_rpc_list_ingress_profiles(type_name: str) -> list:
                    items = _profiles_for_type(type_name)
                    d = _profiles_dir(type_name)
                    return [
                        {"name": it["id"], "path": str(d / f"{it['id']}.json") if d else ""}
                        for it in items
                    ]

                def _ai_rpc_get_ingress_profile(type_name: str, profile_name: str) -> dict:
                    d = _profiles_dir(type_name)
                    if not d:
                        raise RuntimeError("no_project: project must be open to read profiles")
                    path = d / f"{profile_name}.json"
                    if not path.is_file():
                        raise RuntimeError(f"io_error: profile not found: {profile_name}")
                    try:
                        data = json.loads(path.read_text(encoding="utf-8"))
                    except Exception as exc:
                        raise RuntimeError(f"io_error: {exc}")
                    return {
                        "type_name": type_name,
                        "profile_name": profile_name,
                        "fields": data.get("fields", {}),
                        "path": str(path),
                    }

                def _ai_rpc_save_ingress_profile(type_name: str, profile_name: str,
                                                  fields: dict) -> dict:
                    d = _profiles_dir(type_name)
                    if not d:
                        raise RuntimeError("no_project: project must be open to save profiles")
                    d.mkdir(parents=True, exist_ok=True)
                    out = {"type": type_name, "name": profile_name, "fields": fields}
                    path = d / f"{profile_name}.json"
                    path.write_text(json.dumps(out, indent=2), encoding="utf-8")
                    # Refresh the UI list if the currently selected type matches
                    ui_c = client_ref.get("client")
                    if ui_c:
                        cur_type = app_state.get("debug_ingress_type", "")
                        if not cur_type or cur_type == type_name:
                            _deferred(lambda tn=type_name: _refresh_ingress_profiles_list(ui_c, tn))
                    return {
                        "type_name": type_name,
                        "profile_name": profile_name,
                        "path": str(path),
                        "fields_written": len(fields),
                    }

                def _ai_rpc_delete_ingress_profile(type_name: str, profile_name: str) -> dict:
                    d = _profiles_dir(type_name)
                    if not d:
                        raise RuntimeError("no_project: project must be open")
                    path = d / f"{profile_name}.json"
                    deleted = path.is_file()
                    if deleted:
                        try:
                            path.unlink()
                        except OSError as exc:
                            raise RuntimeError(f"io_error: {exc}")
                    ui_c = client_ref.get("client")
                    if ui_c and deleted:
                        cur_type = app_state.get("debug_ingress_type", "")
                        if not cur_type or cur_type == type_name:
                            _deferred(lambda tn=type_name: _refresh_ingress_profiles_list(ui_c, tn))
                    return {"deleted": deleted, "path": str(path)}

                ide_bindings._compile_tab = _ai_rpc_compile_tab
                ide_bindings._open_file = _ai_rpc_open_file
                ide_bindings._switch_view = _ai_rpc_switch_view
                ide_bindings._set_editor_content = _ai_rpc_set_editor_content
                ide_bindings._set_active_tab = _ai_rpc_set_active_tab
                ide_bindings._close_tab = _ai_rpc_close_tab
                ide_bindings._get_nav_tree = _ai_rpc_get_nav_tree
                ide_bindings._set_node_expanded = _ai_rpc_set_node_expanded
                ide_bindings._tree_open_file = _ai_rpc_tree_open_file
                ide_bindings._editor_replace_range = _ai_rpc_editor_replace_range
                ide_bindings._ui_call = _ai_rpc_ui_call
                ide_bindings._open_project = _ai_rpc_open_project
                ide_bindings._get_exceptions = _ai_rpc_get_exceptions
                ide_bindings._clear_exceptions = _ai_rpc_clear_exceptions
                ide_bindings._get_ui_status = _ai_rpc_get_ui_status
                ide_bindings._get_status_indicators = _ai_rpc_get_status_indicators
                ide_bindings._restart_ui = _ai_rpc_restart_ui
                ide_bindings._reload_ui_document = _ai_rpc_reload_ui_document
                ide_bindings._trigger_action = _ai_rpc_trigger_action
                ide_bindings._ext_call = _ai_rpc_ext_call
                ide_bindings._runtime_control = _ai_rpc_runtime_control
                ide_bindings._get_event_log = _ai_rpc_get_event_log
                ide_bindings._clear_event_log = _ai_rpc_clear_event_log
                ide_bindings._log_event = _push_event
                ide_bindings._list_ingress_types = _ai_rpc_list_ingress_types
                ide_bindings._list_ingress_profiles = _ai_rpc_list_ingress_profiles
                ide_bindings._get_ingress_profile = _ai_rpc_get_ingress_profile
                ide_bindings._save_ingress_profile = _ai_rpc_save_ingress_profile
                ide_bindings._delete_ingress_profile = _ai_rpc_delete_ingress_profile

                ai_rpc_server = AiRpcServer(port=args.ai_rpc_port, ide=ide_bindings)
                ai_rpc_server.start()
                _ai_rpc_server_ref[0] = ai_rpc_server
                _first_connect = False

            if args.once:
                return
            if not args.no_worker:
                try:
                    worker = start_background_worker()
                    print(json.dumps({"multi_cpu_worker": worker.snapshot()}, indent=2), flush=True)
                except RuntimeError as exc:
                    print(json.dumps({"multi_cpu_worker_disabled": str(exc)}, indent=2), flush=True)
            if args.repl:
                repl = ElaraUiRepl(args.host, args.port, client_sections=snapshot_sections, default_snapshot_path=args.snapshot_out)
                # Re-use the already-connected client so the UI event handler remains wired into this process.
                repl.client = client
                repl.dumper = UiSnapshotDumper(client, client_sections=snapshot_sections)
                repl._running = True
                print("Integrated Elara UI REPL ready. Type 'help' for commands.", flush=True)
                while repl._running:
                    try:
                        line = input("elara-ui> ")
                    except (EOFError, KeyboardInterrupt):
                        print()
                        break
                    repl.execute_line(line)
                return
            print("Connected to Elara UI RPC head. Press Ctrl+C to exit.", flush=True)
            next_layout_persist = 0.0
            while client._running and not app_state.get("_ui_reconnect_requested"):
                now = time.monotonic()
                if now >= next_layout_persist:
                    _persist_editor_session_state()
                    _persist_runtime_layout_state(client)
                    next_layout_persist = now + 1.0
                time.sleep(0.25)

            if not client._running and not app_state.get("_ui_reconnect_requested"):
                _push_event("ui_disconnect", reason="unexpected",
                            note="See recent ui_rpc_out entries to reproduce crash under gdb")
                _push_exception(Exception("UI RPC connection dropped unexpectedly"), "main_loop")
            else:
                _push_event("ui_disconnect", reason="requested")

            if not app_state.get("_ui_reconnect_requested"):
                try:
                    _python_dbg_stop(client)
                except Exception:
                    pass
                try:
                    _cpp_gdb_stop_session()
                except Exception:
                    pass
                try:
                    _host_debug_bridge_stop()
                except Exception:
                    pass
                try:
                    _external_logic_bridge_stop()
                except Exception:
                    pass

        except (OSError, ElaraUiRpcError) as _conn_exc:
            _push_exception(_conn_exc, "ui_connect")
            if _ui_server.get("proc") is None:
                raise SystemExit(str(_conn_exc))

        finally:
            # Close the session log file; the next connect iteration opens a new one.
            fh = _event_log_fh[0]
            if fh is not None:
                try:
                    fh.close()
                except Exception:
                    pass
                _event_log_fh[0] = None

        # --- Decide whether to reconnect ---
        client_ref["client"] = None
        _reconnect = app_state.pop("_ui_reconnect_requested", False)
        if not _reconnect and _ui_server.get("proc") is not None:
            # UI dropped; if we're managing the process, stay alive and wait
            proc = _ui_server["proc"]
            if proc.poll() is None:
                _reconnect = True  # managed proc still running (we asked it to restart)

        if not _reconnect:
            break

        # Wait up to 10 s for the new server to start accepting connections.
        print(json.dumps({"ui_reconnecting": True}), flush=True)
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            try:
                import socket as _sock
                _s = _sock.create_connection((args.host, args.port), timeout=0.5)
                _s.close()
                break
            except OSError:
                time.sleep(0.5)

    except KeyboardInterrupt:
        if worker is not None:
            try:
                worker.stop()
                worker.wait(timeout_ms=2000)
            except Exception:
                pass
        return


    finally:
        try:
            _host_debug_bridge_send_json({
                "kind": "quit",
                "session_id": str(app_state.get("debug_session_id", "") or ""),
            })
        except Exception:
            pass
        try:
            if "client_ref" in locals():
                client = client_ref.get("client")
                if client is not None:
                    _persist_runtime_layout_state(client)
        except Exception:
            pass
        _flush_ide_state_to_disk()
        if worker is not None:
            try:
                worker.stop()
                worker.wait(timeout_ms=2000)
            except Exception:
                pass
        if ai_rpc_server is not None:
            try:
                ai_rpc_server.stop()
            except Exception:
                pass
        try:
            _python_dbg_stop()
        except Exception:
            pass
        try:
            _cpp_gdb_stop_session()
        except Exception:
            pass
        try:
            _host_debug_bridge_stop()
        except Exception:
            pass
        try:
            _external_logic_bridge_stop()
        except Exception:
            pass
        try:
            _epa_dbg_stop()
        except Exception:
            pass


if __name__ == "__main__":
    main()
