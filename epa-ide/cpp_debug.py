"""cpp_debug — C++ GDB debugger subsystem for epa-ide.

Extracted from app.py via the factory/ctx pattern.  Call setup(ctx) once
inside main() after the shared state dicts have been created.  All functions
are registered into ctx so they can be called from the rest of app.py and
from other subsystem modules.
"""

import os
import re
import select
import subprocess
import time
from pathlib import Path

from editor_tabs import _editor_ids


def setup(ctx: dict) -> None:
    # ── Unpack shared state ──────────────────────────────────────────────────
    cpp_gdb_state  = ctx["cpp_gdb_state"]
    client_ref     = ctx["client_ref"]
    app_state      = ctx["app_state"]

    # ── C++ UI setter functions ──────────────────────────────────────────────

    def _set_cpp_thread_items(client, items: list):
        if not items:
            items = [{"id": "cpp.threads.empty", "label": "No GDB thread data"}]
        targets = ["nav.debug.cpp_threads"]
        seen = set()
        for target in targets:
            if target in seen:
                continue
            seen.add(target)
            try:
                client.set_section_json(target, "items", items)
            except Exception:
                try:
                    client.call("ui.setSectionJson", {"target": target, "section": "items", "value": items})
                except Exception:
                    pass

    def _set_cpp_status_text(client, text: str):
        cpp_gdb_state["status"] = text
        targets = ["nav.debug.cpp_status"]
        seen = set()
        for target in targets:
            if target in seen:
                continue
            seen.add(target)
            try:
                client.set_text(target, text)
            except Exception:
                pass

    def _set_cpp_vm_status(client, state: str, detail: str = ""):
        color = "#777777"
        label = "C++ debugger idle"
        if state == "starting":
            color = "#d29922"
            label = "C++ debugger starting"
        elif state == "running":
            color = "#2da44e"
            label = "C++ debugger attached"
        elif state == "stopped":
            color = "#d29922"
            label = "C++ debugger stopped"
        elif state == "error":
            color = "#d73a49"
            label = "C++ debugger error"
        if detail:
            label = f"{label}: {detail}"
        try:
            client.set_text("nav.debug.cpp_vm_status", f"●  {label}")
            client.fire("ui.setForegroundColor", {"target": "nav.debug.cpp_vm_status", "color": color})
        except Exception:
            pass

    def _set_cpp_vm_buttons(client, started: bool):
        try:
            client.set_text("nav.debug.cpp_reset", "↻  Reset" if started else "▶  Start")
            client.set_enabled("nav.debug.cpp_stop", started)
        except Exception:
            pass

    def _set_cpp_tree_nodes(client, target: str, root_label: str, leaf_labels: list):
        children = [{"id": f"{target}.leaf.{idx}", "label": label} for idx, label in enumerate(leaf_labels or ["No data"])]
        nodes = [{"id": f"{target}.root", "label": root_label, "expanded": True, "children": children}]
        try:
            client.set_section_json(target, "nodes", nodes)
        except Exception:
            try:
                client.call("ui.setSectionJson", {"target": target, "section": "nodes", "value": nodes})
            except Exception:
                pass

    def _set_cpp_registers_text(client, labels: list):
        pass

    def _set_cpp_memory_text(client, labels: list):
        pass

    # ── C++ GDB functions ────────────────────────────────────────────────────

    def _cpp_gdb_stop_session(update_ui: bool = True):
        proc = cpp_gdb_state.get("proc")
        cpp_gdb_state["proc"] = None
        cpp_gdb_state["running"] = False
        cpp_gdb_state["project_root"] = ""
        cpp_gdb_state["binary"] = ""
        cpp_gdb_state["args"] = []
        cpp_gdb_state["read_buffer"] = ""
        if proc is None:
            ui_c = client_ref.get("client")
            if ui_c and update_ui:
                _set_cpp_vm_buttons(ui_c, False)
                _set_cpp_vm_status(ui_c, "stopped")
                ctx["_refresh_status_panel"](ui_c)
            return
        try:
            if proc.stdin:
                proc.stdin.write("-gdb-exit\n")
                proc.stdin.flush()
        except Exception:
            pass
        try:
            proc.terminate()
            proc.wait(timeout=1.0)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass
        ui_c = client_ref.get("client")
        if ui_c and update_ui:
            _set_cpp_vm_buttons(ui_c, False)
            _set_cpp_vm_status(ui_c, "stopped")
            ctx["_refresh_status_panel"](ui_c)

    def _cpp_gdb_read_until_prompt(proc: subprocess.Popen, timeout: float = 5.0) -> list:
        lines = []
        if proc.stdout is None:
            raise RuntimeError("gdb stdout not available")
        deadline = time.time() + timeout
        while time.time() < deadline:
            buffer = str(cpp_gdb_state.get("read_buffer", "") or "")
            if "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                cpp_gdb_state["read_buffer"] = buffer
                text = line.rstrip("\r")
                lines.append(text)
                if text.rstrip() == "(gdb)":
                    return lines
                continue
            if buffer.rstrip() == "(gdb)":
                cpp_gdb_state["read_buffer"] = ""
                lines.append(buffer.rstrip("\r"))
                return lines

            remaining = max(0.0, deadline - time.time())
            ready, _, _ = select.select([proc.stdout], [], [], remaining)
            if not ready:
                continue
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            cpp_gdb_state["read_buffer"] = buffer + chunk.decode("utf-8", errors="replace")
        raise RuntimeError("Timed out waiting for gdb prompt")

    def _cpp_gdb_send(command: str, timeout: float = 5.0) -> list:
        proc = cpp_gdb_state.get("proc")
        if proc is None or proc.stdin is None:
            raise RuntimeError("No GDB session attached")
        cpp_gdb_state["token"] += 1
        token = cpp_gdb_state["token"]
        proc.stdin.write(f"{token}{command}\n")
        proc.stdin.flush()
        lines = _cpp_gdb_read_until_prompt(proc, timeout=timeout)
        result_line = next((line for line in lines if line.startswith(f"{token}^")), "")
        if not result_line:
            raise RuntimeError("\n".join(lines))
        if result_line.startswith(f"{token}^error"):
            match = re.search(r'msg="([^"]*)"', result_line)
            message = match.group(1).replace('\\"', '"') if match else result_line
            raise RuntimeError(message)
        cpp_gdb_state["running"] = any(line.startswith("*running") or "^running" in line for line in lines)
        if any(line.startswith("*stopped") for line in lines):
            cpp_gdb_state["running"] = False
        return lines

    def _cpp_gdb_threads_from_lines(lines: list) -> tuple:
        joined = "\n".join(lines)
        current_match = re.search(r'current-thread-id="([^"]+)"', joined)
        current_id = current_match.group(1) if current_match else ""
        items = []
        for match in re.finditer(r'\{id="([^"]+)".*?target-id="([^"]*)".*?state="([^"]+)"', joined):
            tid, target_id, state = match.groups()
            prefix = "* " if tid == current_id else "  "
            label = f"{prefix}thread {tid}  {state}"
            if target_id:
                label += f"  {target_id}"
            items.append({"id": f"cpp.thread.{tid}", "label": label})
        return items, current_id

    def _cpp_gdb_status_from_lines(lines: list) -> str:
        joined = "\n".join(lines)
        if "*stopped" in joined:
            reason = re.search(r'reason="([^"]+)"', joined)
            func = re.search(r'func="([^"]+)"', joined)
            file_name = re.search(r'file="([^"]+)"', joined)
            line_no = re.search(r'line="([^"]+)"', joined)
            parts = ["stopped"]
            if reason:
                parts.append(reason.group(1))
            if func:
                location = func.group(1)
                if file_name and line_no:
                    location += f" at {file_name.group(1)}:{line_no.group(1)}"
                parts.append(location)
            return " | ".join(parts)
        if cpp_gdb_state.get("running"):
            return "running"
        return cpp_gdb_state.get("status", "No GDB session attached.")

    def _cpp_gdb_refresh_ui(client):
        proc = cpp_gdb_state.get("proc")
        if proc is None or proc.poll() is not None:
            _set_cpp_status_text(client, "No GDB session attached.")
            _set_cpp_vm_buttons(client, False)
            _set_cpp_vm_status(client, "stopped")
            _set_cpp_thread_items(client, [])
            _set_cpp_registers_text(client, [])
            _set_cpp_memory_text(client, [])
            return
        if cpp_gdb_state.get("running"):
            _set_cpp_status_text(client, "running")
            _set_cpp_vm_buttons(client, True)
            _set_cpp_vm_status(client, "running")
            _set_cpp_memory_text(client, ["Process running; pause to inspect memory scopes."])
            return
        try:
            thread_lines = _cpp_gdb_send("-thread-info", timeout=3.0)
            thread_items, current_tid = _cpp_gdb_threads_from_lines(thread_lines)
            _set_cpp_vm_buttons(client, True)
            _set_cpp_thread_items(client, thread_items)
            _set_cpp_registers_text(client, [f"Current thread: {current_tid or '?'}", "Register dump not wired yet"])
            _set_cpp_memory_text(client, ["Scope inspector active", "Raw memory view not wired yet"])
            _set_cpp_vm_status(client, "running", "stopped at breakpoint")
        except Exception as exc:
            _set_cpp_vm_buttons(client, False)
            _set_cpp_vm_status(client, "error", str(exc))
            _set_cpp_status_text(client, f"GDB refresh failed: {exc}")

    def _cpp_gdb_show_thread(client, thread_id_str: str):
        """Switch inspector focus to the given thread (user clicked a thread list item)."""
        if cpp_gdb_state.get("running"):
            return
        try:
            tid = int(thread_id_str)
        except (ValueError, TypeError):
            return
        try:
            _cpp_gdb_send(f"-thread-select {tid}", timeout=3.0)
            frame_lines = _cpp_gdb_send("-stack-list-frames", timeout=3.0)

            # Navigate editor to top frame location if source is available
            joined = "\n".join(frame_lines)
            file_match = re.search(r'level="0"[^,]*,.*?file="([^"]+)".*?line="([^"]+)"', joined)
            if file_match:
                file_path = file_match.group(1)
                line_no_str = file_match.group(2)
                if Path(file_path).is_file():
                    ctx["_open_file_tab"](client, file_path, make_permanent=True)
                    opened_tab_id = ctx["_tab_id_for_path"](file_path)
                    ext = Path(file_path).suffix.lower()
                    eip_target = (
                        _editor_ids(opened_tab_id)["source"]
                        if ext in (".e", ".py")
                        else opened_tab_id + ".container"
                    )
                    try:
                        client.set_eip_line(eip_target, max(0, int(line_no_str) - 1))
                    except Exception:
                        pass
        except Exception as exc:
            ctx["_append_build_output"](client, f"[cpp-dbg] show_thread error: {exc}\n")

    def _ensure_cpp_gdb_session(client):
        project_root_text = app_state.get("project_root", "")
        if not project_root_text:
            raise RuntimeError("No project open.")
        _set_cpp_vm_status(client, "starting")
        project_root = Path(project_root_text)
        cpp_root = ctx["_project_cpp_root"](project_root)
        if not cpp_root.is_dir():
            raise RuntimeError(f"Project has no C++ directory: {cpp_root}")
        binary = ctx["_project_cpp_binary"](project_root)
        if not binary.is_file():
            raise RuntimeError(f"C++ debug binary not found: {binary}\nRun Build Project first.")
        wanted_args = ctx["_project_cpp_gdb_args"](project_root)
        proc = cpp_gdb_state.get("proc")
        if (
            proc is not None and proc.poll() is None
            and cpp_gdb_state.get("project_root") == str(project_root)
            and cpp_gdb_state.get("binary") == str(binary)
        ):
            _set_cpp_vm_buttons(client, True)
            return
        _cpp_gdb_stop_session(update_ui=False)
        session_path = ctx["_write_debug_session_descriptor"]()
        proc = subprocess.Popen(
            ["gdb", "--interpreter=mi2", "--quiet", "--args", str(binary), *wanted_args],
            cwd=str(cpp_root),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env={
                **os.environ,
                "LC_ALL": "C",
                "ELARA_DEBUG_SESSION": str(session_path),
            },
        )
        cpp_gdb_state["proc"] = proc
        cpp_gdb_state["project_root"] = str(project_root)
        cpp_gdb_state["binary"] = str(binary)
        cpp_gdb_state["args"] = wanted_args
        cpp_gdb_state["running"] = False
        cpp_gdb_state["read_buffer"] = ""
        _cpp_gdb_log(client, f"[gdb] debug session {session_path}")
        _set_cpp_vm_buttons(client, True)
        _set_cpp_vm_status(client, "running", "GDB launched")
        _set_cpp_status_text(client, "Waiting for GDB prompt...")
        ctx["_refresh_status_panel"](client)
        _cpp_gdb_read_until_prompt(proc, timeout=5.0)
        _set_cpp_status_text(client, "GDB prompt ready; starting target...")
        _cpp_gdb_send('-gdb-set pagination off')
        _cpp_gdb_send('-gdb-set confirm off')
        start_lines = _cpp_gdb_send("-exec-run --start", timeout=20.0)
        _set_cpp_status_text(client, _cpp_gdb_status_from_lines(start_lines))
        _set_cpp_vm_buttons(client, True)
        _set_cpp_vm_status(client, "running", "stopped at main")
        ctx["_refresh_status_panel"](client)
        _cpp_gdb_refresh_ui(client)

    def _cpp_gdb_log(client, line: str):
        ctx["_append_build_output"](client, line.rstrip("\n") + "\n")

    def _cpp_gdb_execute(client, mi_command: str, label: str, timeout: float = 15.0):
        _ensure_cpp_gdb_session(client)
        lines = _cpp_gdb_send(mi_command, timeout=timeout)
        status = _cpp_gdb_status_from_lines(lines)
        _set_cpp_status_text(client, status)
        _set_cpp_vm_buttons(client, True)
        _set_cpp_vm_status(client, "running", status)
        _cpp_gdb_log(client, f"[gdb] {label}: {status}")
        if cpp_gdb_state.get("running"):
            _set_cpp_memory_text(client, ["Process running; pause to inspect memory scopes."])
        else:
            _cpp_gdb_refresh_ui(client)

    def _cpp_gdb_handle_action(client, action_id: str):
        try:
            if action_id in ("debug.cpp.reset",):
                if cpp_gdb_state.get("proc") is not None:
                    _cpp_gdb_stop_session()
                _ensure_cpp_gdb_session(client)
                ctx["_refresh_status_panel"](client)
                return True
            if action_id in ("debug.cpp.stop",):
                _cpp_gdb_stop_session()
                _set_cpp_status_text(client, "No GDB session attached.")
                _set_cpp_thread_items(client, [])
                _set_cpp_registers_text(client, [])
                _set_cpp_memory_text(client, [])
                ctx["_refresh_status_panel"](client)
                return True
            if action_id in ("debug.cpp.continue",):
                _cpp_gdb_execute(client, "-exec-continue", "continue", timeout=5.0)
                return True
            if action_id in ("debug.cpp.step_over",):
                if cpp_gdb_state.get("running"):
                    raise RuntimeError("GDB is running. Pause before stepping.")
                _cpp_gdb_execute(client, "-exec-next", "step over", timeout=20.0)
                return True
            if action_id in ("debug.cpp.step_into",):
                if cpp_gdb_state.get("running"):
                    raise RuntimeError("GDB is running. Pause before stepping.")
                _cpp_gdb_execute(client, "-exec-step", "step into", timeout=20.0)
                return True
            if action_id in ("debug.cpp.step_out",):
                if cpp_gdb_state.get("running"):
                    raise RuntimeError("GDB is running. Pause before stepping.")
                _cpp_gdb_execute(client, "-exec-finish", "step out", timeout=20.0)
                return True
            if action_id in ("debug.cpp.pause",):
                _ensure_cpp_gdb_session(client)
                if not cpp_gdb_state.get("running"):
                    _set_cpp_status_text(client, "stopped")
                    _cpp_gdb_refresh_ui(client)
                    _cpp_gdb_log(client, "[gdb] pause: already stopped")
                    return True
                _cpp_gdb_execute(client, "-exec-interrupt", "pause", timeout=20.0)
                return True
        except Exception as exc:
            _set_cpp_status_text(client, "error")
            _set_cpp_vm_status(client, "error", str(exc))
            _cpp_gdb_log(client, f"[gdb-error] {exc}")
            proc = cpp_gdb_state.get("proc")
            if proc is None or proc.poll() is not None:
                _set_cpp_vm_buttons(client, False)
            else:
                _cpp_gdb_refresh_ui(client)
            ctx["_refresh_status_panel"](client)
            return True
        return False

    # ── Register all functions into ctx ──────────────────────────────────────
    ctx.update({
        "_set_cpp_thread_items":        _set_cpp_thread_items,
        "_set_cpp_status_text":         _set_cpp_status_text,
        "_set_cpp_vm_status":           _set_cpp_vm_status,
        "_set_cpp_vm_buttons":          _set_cpp_vm_buttons,
        "_set_cpp_tree_nodes":          _set_cpp_tree_nodes,
        "_set_cpp_registers_text":      _set_cpp_registers_text,
        "_set_cpp_memory_text":         _set_cpp_memory_text,
        "_cpp_gdb_stop_session":        _cpp_gdb_stop_session,
        "_cpp_gdb_read_until_prompt":   _cpp_gdb_read_until_prompt,
        "_cpp_gdb_send":                _cpp_gdb_send,
        "_cpp_gdb_threads_from_lines":  _cpp_gdb_threads_from_lines,
        "_cpp_gdb_status_from_lines":   _cpp_gdb_status_from_lines,
        "_cpp_gdb_refresh_ui":          _cpp_gdb_refresh_ui,
        "_cpp_gdb_show_thread":         _cpp_gdb_show_thread,
        "_ensure_cpp_gdb_session":      _ensure_cpp_gdb_session,
        "_cpp_gdb_log":                 _cpp_gdb_log,
        "_cpp_gdb_execute":             _cpp_gdb_execute,
        "_cpp_gdb_handle_action":       _cpp_gdb_handle_action,
    })
