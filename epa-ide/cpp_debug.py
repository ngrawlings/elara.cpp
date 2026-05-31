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
import tempfile
import threading
import time
from pathlib import Path

from editor_tabs import _editor_ids


def setup(ctx: dict) -> None:
    # ── Unpack shared state ──────────────────────────────────────────────────
    cpp_gdb_state  = ctx["cpp_gdb_state"]
    client_ref     = ctx["client_ref"]
    app_state      = ctx["app_state"]
    tab_list       = ctx["tab_list"]

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
        tab_id = cpp_gdb_state.get("inspect_tab_id") or app_state.get("active_editor_tab", "")
        if not tab_id:
            return
        ids = _editor_ids(tab_id)
        items = [{"id": f"cpp.register.{idx}", "label": label} for idx, label in enumerate(labels)]
        if not items:
            items = [{"id": "cpp.registers.empty", "label": "No register data"}]
        try:
            client.call("ui.setSectionJson", {
                "target": ids["debug_dynamic"],
                "section": "items",
                "value": items,
            })
        except Exception:
            pass

    def _set_cpp_memory_text(client, labels: list):
        tab_id = cpp_gdb_state.get("inspect_tab_id") or app_state.get("active_editor_tab", "")
        if not tab_id:
            return
        ids = _editor_ids(tab_id)
        items = [{"id": f"cpp.local.{idx}", "label": label} for idx, label in enumerate(labels)]
        if not items:
            items = [{"id": "cpp.locals.empty", "label": "No local data"}]
        try:
            client.call("ui.setSectionJson", {
                "target": ids["debug_local"],
                "section": "items",
                "value": items,
            })
        except Exception:
            pass

    def _set_cpp_stack_items(client, labels: list):
        tab_id = cpp_gdb_state.get("inspect_tab_id") or app_state.get("active_editor_tab", "")
        if not tab_id:
            return
        ids = _editor_ids(tab_id)
        items = [{"id": f"cpp.stack.{idx}", "label": label} for idx, label in enumerate(labels)]
        if not items:
            items = [{"id": "cpp.stack.empty", "label": "No frame data"}]
        try:
            client.call("ui.setSectionJson", {
                "target": ids["debug_stack"],
                "section": "items",
                "value": items,
            })
        except Exception:
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
        stdout_read_fd = cpp_gdb_state.pop("inferior_stdout_read_fd", -1)
        if stdout_read_fd >= 0:
            try:
                os.close(stdout_read_fd)
            except Exception:
                pass
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

    def _cpp_gdb_read_until_stop(proc: subprocess.Popen, timeout: float = 5.0) -> list:
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
                if text.startswith("*stopped"):
                    return lines
                continue
            remaining = max(0.0, deadline - time.time())
            ready, _, _ = select.select([proc.stdout], [], [], remaining)
            if not ready:
                continue
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            cpp_gdb_state["read_buffer"] = buffer + chunk.decode("utf-8", errors="replace")
        raise RuntimeError("Timed out waiting for gdb stop")

    def _cpp_gdb_send(command: str, timeout: float = 5.0, wait_for_stop: bool = False) -> list:
        proc = cpp_gdb_state.get("proc")
        if proc is None or proc.stdin is None:
            raise RuntimeError("No GDB session attached")
        cpp_gdb_state["token"] += 1
        token = cpp_gdb_state["token"]
        proc.stdin.write(f"{token}{command}\n")
        proc.stdin.flush()
        lines = []
        result_line = ""
        # Async commands can leave a stale "(gdb)" prompt in the stream.  Keep
        # reading through prompt chunks until this command's token result arrives.
        for _ in range(3):
            lines.extend(_cpp_gdb_read_until_prompt(proc, timeout=timeout))
            result_line = next((line for line in lines if line.startswith(f"{token}^")), "")
            if result_line:
                break
            if any(line.rstrip() == "(gdb)" for line in lines):
                continue
            break
        if not result_line:
            raise RuntimeError("\n".join(lines))
        if result_line.startswith(f"{token}^error"):
            match = re.search(r'msg="([^"]*)"', result_line)
            message = match.group(1).replace('\\"', '"') if match else result_line
            raise RuntimeError(message)
        cpp_gdb_state["running"] = any(line.startswith("*running") or "^running" in line for line in lines)
        if any(line.startswith("*stopped") for line in lines):
            cpp_gdb_state["running"] = False
        # In async mode, run/step commands return ^running plus an immediate prompt;
        # the later *stopped notification is not guaranteed to be followed by another
        # prompt.  Wait explicitly for that async record.
        if wait_for_stop and cpp_gdb_state.get("running"):
            extra = _cpp_gdb_read_until_stop(proc, timeout=timeout)
            lines.extend(extra)
            cpp_gdb_state["running"] = not any(line.startswith("*stopped") for line in lines)
        return lines

    def _cpp_gdb_interrupt(timeout: float = 20.0) -> list:
        proc = cpp_gdb_state.get("proc")
        if proc is None or proc.stdin is None:
            raise RuntimeError("No GDB session attached")
        cpp_gdb_state["token"] += 1
        token = cpp_gdb_state["token"]
        proc.stdin.write(f"{token}-exec-interrupt --all\n")
        proc.stdin.flush()
        lines = _cpp_gdb_read_until_stop(proc, timeout=timeout)
        try:
            lines.extend(_cpp_gdb_read_until_prompt(proc, timeout=2.0))
        except Exception:
            pass
        joined = "\n".join(lines)
        if f"{token}^error" in joined:
            match = re.search(rf'{token}\^error,msg="([^"]*)"', joined)
            raise RuntimeError(match.group(1).replace('\\"', '"') if match else joined)
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

    def _cpp_mi_unescape(value: str) -> str:
        return (
            value.replace(r"\\", "\\")
            .replace(r"\"", '"')
            .replace(r"\n", "\n")
            .replace(r"\t", "\t")
        )

    def _cpp_mi_objects(lines: list, prefix: str) -> list[dict]:
        joined = "\n".join(lines)
        start = joined.find(prefix + "=[")
        if start < 0:
            return []
        text = joined[start + len(prefix) + 2:]
        end = text.find("]")
        if end >= 0:
            text = text[:end]
        objects = []
        for body in re.findall(r'\{([^{}]*)\}', text):
            item = {}
            for key, value in re.findall(r'([A-Za-z0-9_-]+)="((?:\\.|[^"])*)"', body):
                item[key] = _cpp_mi_unescape(value)
            if item:
                objects.append(item)
        return objects

    def _cpp_frame_objects(lines: list) -> list[dict]:
        return _cpp_mi_objects(lines, "stack")

    def _cpp_stack_labels(lines: list) -> list[str]:
        labels = []
        selected_level = str(cpp_gdb_state.get("selected_frame_level", "") or "")
        for frame in _cpp_frame_objects(lines):
            level = frame.get("level", "?")
            func = frame.get("func", "??")
            file_name = frame.get("file") or frame.get("fullname") or "?"
            line_no = frame.get("line", "?")
            prefix = "-> " if str(level) == selected_level else "   "
            labels.append(f"{prefix}#{level} {func}  {file_name}:{line_no}")
        return labels

    def _cpp_local_labels(lines: list) -> list[str]:
        labels = []
        for var in _cpp_mi_objects(lines, "variables"):
            name = var.get("name", "?")
            value = var.get("value", "<optimized out>")
            type_name = var.get("type", "")
            suffix = f" : {type_name}" if type_name else ""
            labels.append(f"{name} = {value}{suffix}")
        return labels

    def _cpp_register_labels(name_lines: list, value_lines: list) -> list[str]:
        names_text = "\n".join(name_lines)
        names = [_cpp_mi_unescape(m.group(1)) for m in re.finditer(r'"((?:\\.|[^"])*)"', names_text)]
        labels = []
        for item in _cpp_mi_objects(value_lines, "register-values"):
            try:
                number = int(item.get("number", "-1"))
            except Exception:
                number = -1
            reg_name = names[number] if 0 <= number < len(names) and names[number] else f"r{number}"
            labels.append(f"{reg_name} = {item.get('value', '?')}")
        return labels

    def _cpp_resolve_frame_path(frame: dict) -> Path | None:
        file_path = frame.get("fullname") or frame.get("file") or ""
        if not file_path:
            return None
        path_obj = Path(file_path)
        if not path_obj.is_file():
            project_root = Path(cpp_gdb_state.get("project_root", "") or "")
            candidate = project_root / file_path
            if candidate.is_file():
                path_obj = candidate
            else:
                return None
        return path_obj

    def _cpp_choose_inspection_frame(frames: list[dict]) -> dict:
        if not frames:
            return {}
        project_root = Path(cpp_gdb_state.get("project_root", "") or "")
        try:
            project_root_resolved = project_root.resolve()
        except Exception:
            project_root_resolved = project_root
        for frame in frames:
            path_obj = _cpp_resolve_frame_path(frame)
            if path_obj is None:
                continue
            try:
                path_obj.resolve().relative_to(project_root_resolved)
                return frame
            except Exception:
                continue
        for frame in frames:
            if _cpp_resolve_frame_path(frame) is not None:
                return frame
        return frames[0]

    def _cpp_open_frame(client, frame: dict):
        path_obj = _cpp_resolve_frame_path(frame)
        if path_obj is None:
            return
        try:
            line_no = int(frame.get("line", "0") or 0)
        except Exception:
            line_no = 0
        ctx["_open_file_tab"](client, str(path_obj), make_permanent=True)
        tab_id = ctx["_tab_id_for_path"](str(path_obj))
        cpp_gdb_state["inspect_tab_id"] = tab_id
        try:
            client.set_eip_line(_editor_ids(tab_id)["source"], max(0, int(line_no) - 1))
        except Exception:
            pass

    def _cpp_gdb_refresh_inspector(client):
        frame_lines = _cpp_gdb_send("-stack-list-frames", timeout=3.0)
        frames = _cpp_frame_objects(frame_lines)
        selected_frame = _cpp_choose_inspection_frame(frames)
        selected_level = str(selected_frame.get("level", "0") or "0") if selected_frame else "0"
        cpp_gdb_state["selected_frame_level"] = selected_level
        if selected_frame and selected_level != "0":
            try:
                _cpp_gdb_send(f"-stack-select-frame {int(selected_level)}", timeout=3.0)
            except Exception as exc:
                _cpp_gdb_log(client, f"[gdb-error] frame select failed: {exc}")
        if selected_frame:
            _cpp_open_frame(client, selected_frame)
        _set_cpp_stack_items(client, _cpp_stack_labels(frame_lines))

        local_lines = _cpp_gdb_send("-stack-list-variables --simple-values", timeout=3.0)
        _set_cpp_memory_text(client, _cpp_local_labels(local_lines))

        name_lines = _cpp_gdb_send("-data-list-register-names", timeout=3.0)
        value_lines = _cpp_gdb_send("-data-list-register-values x", timeout=3.0)
        _set_cpp_registers_text(client, _cpp_register_labels(name_lines, value_lines))

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
            _cpp_gdb_refresh_inspector(client)
            _set_cpp_vm_status(client, "stopped", "paused")
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
            _cpp_gdb_refresh_inspector(client)
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
        stdout_read_fd, stdout_write_fd = os.pipe()
        cpp_gdb_state["inferior_stdout_read_fd"] = stdout_read_fd

        def _inferior_stdout_reader(rfd: int):
            try:
                with os.fdopen(rfd, "rb") as f:
                    while True:
                        line = f.readline()
                        if not line:
                            break
                        text = line.decode("utf-8", errors="replace").rstrip("\r\n")
                        if text:
                            ui_c = client_ref.get("client")
                            if ui_c:
                                ctx["_append_host_io_output"](ui_c, f"[C++ Host] {text}\n")
            except Exception:
                pass

        threading.Thread(target=_inferior_stdout_reader, args=(stdout_read_fd,), daemon=True).start()

        proc = subprocess.Popen(
            ["gdb", "--interpreter=mi2", "--quiet", "--args", str(binary), *wanted_args],
            cwd=str(cpp_root),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            pass_fds=(stdout_write_fd,),
            env={
                **os.environ,
                "LC_ALL": "C",
                "ELARA_DEBUG_SESSION": str(session_path),
                "ELARA_STDOUT_FD": str(stdout_write_fd),
            },
        )
        os.close(stdout_write_fd)
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
        _set_cpp_status_text(client, "GDB prompt ready; starting target at main...")
        _cpp_gdb_send('-gdb-set pagination off')
        _cpp_gdb_send('-gdb-set confirm off')
        _cpp_gdb_send('-gdb-set target-async on')
        _cpp_gdb_restore_breakpoints(client)
        start_lines = _cpp_gdb_send("-exec-run --start", timeout=20.0, wait_for_stop=True)
        _set_cpp_status_text(client, _cpp_gdb_status_from_lines(start_lines))
        _set_cpp_vm_buttons(client, True)
        _set_cpp_vm_status(client, "stopped", "stopped at main")
        ctx["_refresh_status_panel"](client)
        _cpp_gdb_refresh_ui(client)

    def _cpp_gdb_log(client, line: str):
        ctx["_append_build_output"](client, line.rstrip("\n") + "\n")

    def _cpp_gdb_breakpoints() -> dict:
        return cpp_gdb_state.setdefault("breakpoints_by_path", {})

    def _cpp_gdb_persist_breakpoints():
        persist = ctx.get("_persist_editor_session_state")
        if callable(persist):
            try:
                persist()
            except Exception:
                pass

    def _cpp_gdb_set_breakpoint(client, path: str, line0: int, enabled: bool):
        if not path:
            return
        path = str(Path(path))
        line0 = int(line0)
        by_path = _cpp_gdb_breakpoints()
        line_map = by_path.setdefault(path, {})
        if not enabled:
            bkpt_no = line_map.pop(line0, None)
            if not line_map:
                by_path.pop(path, None)
            _cpp_gdb_persist_breakpoints()
            if bkpt_no and cpp_gdb_state.get("proc") is not None and not cpp_gdb_state.get("running"):
                try:
                    _cpp_gdb_send(f"-break-delete {bkpt_no}", timeout=3.0)
                    _cpp_gdb_log(client, f"[gdb] breakpoint cleared {path}:{line0 + 1}")
                except Exception as exc:
                    _cpp_gdb_log(client, f"[gdb-error] breakpoint clear failed: {exc}")
            elif bkpt_no and cpp_gdb_state.get("running"):
                _cpp_gdb_log(client, f"[gdb] breakpoint clear queued until next reset {path}:{line0 + 1}")
            return

        line_map.setdefault(line0, "")
        _cpp_gdb_persist_breakpoints()
        if cpp_gdb_state.get("proc") is None or cpp_gdb_state.get("running"):
            _cpp_gdb_log(client, f"[gdb] breakpoint queued {path}:{line0 + 1}")
            return
        try:
            lines = _cpp_gdb_send(f'-break-insert "{path}:{line0 + 1}"', timeout=5.0)
            joined = "\n".join(lines)
            match = re.search(r'number="([^"]+)"', joined)
            line_map[line0] = match.group(1) if match else ""
            _cpp_gdb_persist_breakpoints()
            _cpp_gdb_log(client, f"[gdb] breakpoint set {path}:{line0 + 1}")
        except Exception as exc:
            line_map.pop(line0, None)
            if not line_map:
                by_path.pop(path, None)
            _cpp_gdb_persist_breakpoints()
            _cpp_gdb_log(client, f"[gdb-error] breakpoint set failed: {exc}")

    def _cpp_gdb_restore_breakpoints(client):
        by_path = _cpp_gdb_breakpoints()
        for path, line_map in list(by_path.items()):
            for line0 in list(line_map.keys()):
                try:
                    lines = _cpp_gdb_send(f'-break-insert "{path}:{int(line0) + 1}"', timeout=5.0)
                    match = re.search(r'number="([^"]+)"', "\n".join(lines))
                    line_map[line0] = match.group(1) if match else ""
                except Exception as exc:
                    _cpp_gdb_log(client, f"[gdb-error] breakpoint restore failed {path}:{int(line0) + 1}: {exc}")
        _cpp_gdb_persist_breakpoints()

    def _cpp_gdb_execute(client, mi_command: str, label: str, timeout: float = 15.0, wait_for_stop: bool = False):
        _ensure_cpp_gdb_session(client)
        lines = _cpp_gdb_send(mi_command, timeout=timeout, wait_for_stop=wait_for_stop)
        status = _cpp_gdb_status_from_lines(lines)
        _set_cpp_status_text(client, status)
        _set_cpp_vm_buttons(client, True)
        _set_cpp_vm_status(client, "running" if cpp_gdb_state.get("running") else "stopped", status)
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
                _cpp_gdb_execute(client, "-exec-next", "step over", timeout=20.0, wait_for_stop=True)
                return True
            if action_id in ("debug.cpp.step_into",):
                if cpp_gdb_state.get("running"):
                    raise RuntimeError("GDB is running. Pause before stepping.")
                _cpp_gdb_execute(client, "-exec-step", "step into", timeout=20.0, wait_for_stop=True)
                return True
            if action_id in ("debug.cpp.step_out",):
                if cpp_gdb_state.get("running"):
                    raise RuntimeError("GDB is running. Pause before stepping.")
                try:
                    _cpp_gdb_execute(client, "-exec-finish", "step out", timeout=30.0, wait_for_stop=True)
                except RuntimeError as exc:
                    if str(exc).strip() in ("", "\\"):
                        raise RuntimeError("Cannot step out: current frame has no caller.")
                    raise
                return True
            if action_id in ("debug.cpp.pause",):
                _ensure_cpp_gdb_session(client)
                if not cpp_gdb_state.get("running"):
                    _set_cpp_status_text(client, "stopped")
                    _cpp_gdb_refresh_ui(client)
                    _cpp_gdb_log(client, "[gdb] pause: already stopped")
                    return True
                lines = _cpp_gdb_interrupt(timeout=20.0)
                status = _cpp_gdb_status_from_lines(lines)
                _set_cpp_status_text(client, status)
                _set_cpp_vm_buttons(client, True)
                _set_cpp_vm_status(client, "stopped", status)
                _cpp_gdb_log(client, f"[gdb] pause: {status}")
                _cpp_gdb_refresh_ui(client)
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
        "_set_cpp_stack_items":         _set_cpp_stack_items,
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
        "_cpp_gdb_set_breakpoint":      _cpp_gdb_set_breakpoint,
    })
