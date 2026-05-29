"""python_debug — Python DAP debugger subsystem for epa-ide.

Extracted from app.py via the factory/ctx pattern.  Call setup(ctx) once
inside main() after the shared state dicts have been created.  All functions
are registered into ctx so they can be called from the rest of app.py and
from other subsystem modules.
"""

import os
import subprocess
import sys
import threading
import time
from pathlib import Path

from editor_tabs import _editor_ids
from dap_client import PythonDapClient


def setup(ctx: dict) -> None:
    # ── Unpack shared state ──────────────────────────────────────────────────
    python_dbg_state = ctx["python_dbg_state"]
    client_ref       = ctx["client_ref"]
    app_state        = ctx["app_state"]

    # ── Python UI setter functions ───────────────────────────────────────────

    def _set_python_thread_items(client, items: list):
        if not items:
            items = [{"id": "python.threads.empty", "label": "No Python thread data"}]
        try:
            client.call("ui.setSectionJson", {
                "target": "nav.debug.python_threads",
                "section": "items",
                "value": items,
            })
        except Exception:
            pass

    def _set_python_status_text(client, text: str):
        python_dbg_state["status"] = text
        try:
            client.set_text("nav.debug.python_status", "")
        except Exception:
            pass
        if text:
            ctx["_append_build_output"](client, f"[python] {text}\n")

    def _set_python_vm_status(client, state: str, detail: str = ""):
        color = "#777777"
        label = "Python debugger idle"
        if state == "starting":
            color = "#d29922"
            label = "Python debugger starting"
        elif state == "running":
            color = "#2da44e"
            label = "Python debugger running"
        elif state == "stopped":
            color = "#d29922"
            label = "Python debugger stopped"
        elif state == "error":
            color = "#d73a49"
            label = "Python debugger error"
        if detail:
            label = f"{label}: {detail}"
        try:
            client.set_text("nav.debug.python_vm_status", f"●  {label}")
            client.fire("ui.setForegroundColor", {"target": "nav.debug.python_vm_status", "color": color})
        except Exception:
            pass

    def _set_python_vm_buttons(client, started: bool):
        try:
            client.set_text("nav.debug.python_reset", "↻  Reset" if started else "▶  Start")
            client.set_enabled("nav.debug.python_stop", started)
        except Exception:
            pass

    def _set_python_frame_items(client, items: list, tab_id: str = None):
        tid = tab_id or python_dbg_state.get("inspect_tab_id")
        if not tid:
            return
        python_dbg_state["inspect_tab_id"] = tid
        ids = _editor_ids(tid)
        panel_width = 280 if items else 0
        try:
            client.set_grid_column_exact_size(ids["debug_panel"], 1, panel_width)
        except Exception:
            pass
        display = items if items else [{"id": "python.stack.empty", "label": "No frame data"}]
        try:
            client.call("ui.setSectionJson", {
                "target": ids["debug_stack"],
                "section": "items",
                "value": display,
            })
        except Exception:
            pass

    def _set_python_locals_text(client, labels: list, tab_id: str = None):
        tid = tab_id or python_dbg_state.get("inspect_tab_id")
        if not tid:
            return
        ids = _editor_ids(tid)
        items = [{"id": f"python.local.{i}", "label": l} for i, l in enumerate(labels)]
        display = items if items else [{"id": "python.locals.empty", "label": "No local data"}]
        try:
            client.call("ui.setSectionJson", {
                "target": ids["debug_local"],
                "section": "items",
                "value": display,
            })
        except Exception:
            pass

    def _set_python_registers_text(client, labels: list):
        pass

    def _set_python_memory_text(client, labels: list):
        pass

    # ── Python DAP debugger functions ────────────────────────────────────────

    def _python_dbg_refresh_ui(client):
        started = bool(python_dbg_state.get("started"))
        stopped = bool(python_dbg_state.get("stopped"))
        _set_python_vm_buttons(client, started)
        try:
            client.set_text("nav.debug.python_status", "")
        except Exception:
            pass
        if not started:
            _set_python_vm_status(client, "idle")
            _set_python_thread_items(client, [])
            _set_python_frame_items(client, [])
            _set_python_locals_text(client, [])
            python_dbg_state.pop("inspect_tab_id", None)
        elif stopped:
            _set_python_vm_status(client, "stopped")
        else:
            _set_python_vm_status(client, "running")

    def _python_dbg_refresh_threads(client):
        """Query DAP for live threads and stack frames, update UI."""
        dap = python_dbg_state.get("dap")
        if dap is None:
            ctx["_append_build_output"](client, "[python-dbg] refresh_threads: dap is None\n")
            return

        thread_id = python_dbg_state.get("thread_id")
        stopped = python_dbg_state.get("stopped", False)

        # --- Thread list ---
        try:
            resp = dap.send("threads", {}, timeout=3.0)
            ctx["_append_build_output"](client, f"[python-dbg] threads response: {resp}\n")
            threads = (resp.get("body") or {}).get("threads", [])
            items = [
                {"id": f"python.thread.{t['id']}",
                 "label": f"Thread {t['id']}: {t.get('name', '?')}"}
                for t in threads
            ]
            _set_python_thread_items(client, items)
            # Select the stopped/current thread
            if thread_id and items:
                try:
                    client.set_text("nav.debug.python_threads",
                                    f"python.thread.{thread_id}")
                except Exception:
                    pass
        except Exception as e:
            ctx["_append_build_output"](client, f"[python-dbg] threads exception: {e}\n")

        if not stopped or not thread_id:
            return

        # --- Stack trace and locals for stopped thread ---
        try:
            stack_resp = dap.send("stackTrace",
                                  {"threadId": thread_id, "startFrame": 0, "levels": 30},
                                  timeout=3.0)
            frames = (stack_resp.get("body") or {}).get("stackFrames", [])
            if not frames:
                return

            # Navigate the editor to the top frame's file and line
            top_frame = frames[0]
            top_src_path = (top_frame.get("source") or {}).get("path", "")
            top_line = top_frame.get("line", 0)
            inspect_tab_id = None
            if top_src_path and top_line > 0 and Path(top_src_path).is_file():
                ctx["_open_file_tab"](client, top_src_path, make_permanent=True)
                inspect_tab_id = ctx["_tab_id_for_path"](top_src_path)
                ids = _editor_ids(inspect_tab_id)
                try:
                    client.set_eip_line(ids["source"], top_line - 1)
                except Exception:
                    pass

            reason = python_dbg_state.get("status", "Stopped")
            out_lines = [f"[python] {reason}"]
            out_lines.append("[python] Traceback (most recent call last):")
            frame_items = []
            for f in reversed(frames):
                src_path = (f.get("source") or {}).get("path", "?")
                src_name = Path(src_path).name
                line = f.get("line", 0)
                name = f.get("name", "?")
                out_lines.append(f'[python]   File "{src_name}", line {line}, in {name}')
            for idx, f in enumerate(frames):
                src_path = (f.get("source") or {}).get("path", "?")
                src_name = Path(src_path).name
                line = f.get("line", 0)
                name = f.get("name", "?")
                frame_items.append({
                    "id": f"python.frame.{f.get('id', idx)}",
                    "label": f"{name}  ({src_name}:{line})",
                })
            _set_python_frame_items(client, frame_items, tab_id=inspect_tab_id)

            # Locals for the top frame
            top_frame_id = top_frame.get("id")
            scopes_resp = dap.send("scopes", {"frameId": top_frame_id}, timeout=3.0)
            scopes = (scopes_resp.get("body") or {}).get("scopes", [])
            local_labels = []
            for scope in scopes:
                if scope.get("name", "").lower() != "locals":
                    continue
                vars_resp = dap.send(
                    "variables",
                    {"variablesReference": scope["variablesReference"]},
                    timeout=3.0,
                )
                variables = (vars_resp.get("body") or {}).get("variables", [])[:20]
                if variables:
                    out_lines.append("[python] [locals]")
                    for v in variables:
                        label = f"{v['name']} = {v['value']}"
                        local_labels.append(label)
                        out_lines.append(f"[python]   {label}")
                break
            _set_python_locals_text(client, local_labels, tab_id=inspect_tab_id)

            ctx["_append_build_output"](client, "\n".join(out_lines) + "\n")
        except Exception:
            pass

    def _python_dbg_show_thread(client, thread_id_str: str):
        """Switch inspector focus to the given thread (user clicked a thread list item)."""
        dap = python_dbg_state.get("dap")
        if dap is None or not python_dbg_state.get("stopped"):
            return
        try:
            tid = int(thread_id_str)
        except (ValueError, TypeError):
            return
        try:
            stack_resp = dap.send("stackTrace",
                                  {"threadId": tid, "startFrame": 0, "levels": 30},
                                  timeout=3.0)
            frames = (stack_resp.get("body") or {}).get("stackFrames", [])
            if not frames:
                return

            top_frame = frames[0]
            top_src_path = (top_frame.get("source") or {}).get("path", "")
            top_line = top_frame.get("line", 0)
            inspect_tab_id = None
            if top_src_path and top_line > 0 and Path(top_src_path).is_file():
                ctx["_open_file_tab"](client, top_src_path, make_permanent=True)
                inspect_tab_id = ctx["_tab_id_for_path"](top_src_path)
                ids = _editor_ids(inspect_tab_id)
                try:
                    client.set_eip_line(ids["source"], top_line - 1)
                except Exception:
                    pass

            frame_items = [
                {
                    "id": f"python.frame.{f.get('id', i)}",
                    "label": f"{f.get('name','?')}  ({Path((f.get('source') or {}).get('path','?')).name}:{f.get('line',0)})",
                }
                for i, f in enumerate(frames)
            ]
            _set_python_frame_items(client, frame_items, tab_id=inspect_tab_id)

            top_frame_id = top_frame.get("id")
            scopes_resp = dap.send("scopes", {"frameId": top_frame_id}, timeout=3.0)
            scopes = (scopes_resp.get("body") or {}).get("scopes", [])
            local_labels = []
            for scope in scopes:
                if scope.get("name", "").lower() != "locals":
                    continue
                vars_resp = dap.send(
                    "variables",
                    {"variablesReference": scope["variablesReference"]},
                    timeout=3.0,
                )
                for v in (vars_resp.get("body") or {}).get("variables", [])[:20]:
                    local_labels.append(f"{v['name']} = {v['value']}")
                break
            _set_python_locals_text(client, local_labels, tab_id=inspect_tab_id)
        except Exception as exc:
            ctx["_append_build_output"](client, f"[python-dbg] show_thread error: {exc}\n")

    def _python_dap_on_event(event: str, body: dict):
        """Called from the DAP receive thread.

        Must NOT call dap.send() directly — that blocks waiting for a response
        that can only arrive on this same thread, causing a deadlock.  Any
        follow-up DAP requests must be dispatched onto a new thread.
        """
        c = client_ref.get("client")
        dap = python_dbg_state.get("dap")

        if event == "initialized":
            # configurationDone is sent in the main launch sequence after attach,
            # so there is nothing to do here except log receipt of the event.
            if c:
                ctx["_append_build_output"](c, "[python] debugpy initialized\n")

        elif event == "stopped":
            reason = body.get("reason", "stopped")
            thread_id = body.get("threadId")
            python_dbg_state["stopped"] = True
            python_dbg_state["thread_id"] = thread_id
            python_dbg_state["status"] = f"Stopped: {reason}"
            if c:
                ctx["_append_build_output"](c, f"[python-dbg] stopped event: reason={reason} thread={thread_id}\n")
                _set_python_vm_status(c, "stopped")
                _set_python_status_text(c, python_dbg_state["status"])
                threading.Thread(target=_python_dbg_refresh_threads, args=(c,), daemon=True).start()

        elif event == "continued":
            python_dbg_state["stopped"] = False
            python_dbg_state["status"] = "Running"
            if c:
                _set_python_vm_status(c, "running")
                _set_python_status_text(c, "Running")
                _set_python_thread_items(c, [])
                _set_python_frame_items(c, [])   # hides the panel via inspect_tab_id
                _set_python_locals_text(c, [])
                python_dbg_state.pop("inspect_tab_id", None)

        elif event in ("terminated", "exited"):
            threading.Thread(target=_python_dbg_stop, args=(c,), daemon=True).start()

        elif event == "thread":
            # Refresh the thread list whenever a thread starts or exits.
            if c and python_dbg_state.get("started"):
                threading.Thread(target=_python_dbg_refresh_threads, args=(c,), daemon=True).start()

        elif event == "output":
            output = body.get("output", "")
            category = body.get("category", "console")
            if c and output:
                line = f"[python-{category}] {output}"
                if not line.endswith("\n"):
                    line += "\n"
                ctx["_append_build_output"](c, line)

    def _python_dbg_stop(client=None):
        """Terminate the debugpy process and clean up state."""
        dap = python_dbg_state.get("dap")
        if dap:
            try:
                dap.send("disconnect", {"terminateDebuggee": True}, timeout=2.0)
            except Exception:
                pass
            dap.close()
        python_dbg_state["dap"] = None

        proc = python_dbg_state.get("proc")
        if proc and proc.poll() is None:
            try:
                proc.terminate()
                proc.wait(timeout=2.0)
            except Exception:
                try:
                    proc.kill()
                except Exception:
                    pass
        python_dbg_state["proc"] = None
        python_dbg_state["started"] = False
        python_dbg_state["stopped"] = False
        python_dbg_state["status"] = "No Python debug session attached."
        python_dbg_state["thread_id"] = None
        python_dbg_state["port"] = None

        if client:
            _python_dbg_refresh_ui(client)
            ctx["_refresh_status_panel"](client)
            ctx["_append_build_output"](client, "[python] debug session ended\n")

    def _python_dbg_launch(client):
        """Launch app.py under debugpy and connect the DAP client."""
        project_root_text = app_state.get("project_root", "")
        if not project_root_text:
            raise RuntimeError("No project open.")
        project_root = Path(project_root_text)
        python_root = ctx["_project_python_root"](project_root)
        entry = python_root / "app.py"
        if not entry.is_file():
            raise RuntimeError(f"python/app.py not found in project: {project_root}")

        _python_dbg_stop()

        port = ctx["_allocate_epa_dbg_port"]()
        python_dbg_state["port"] = port

        session_path = ctx["_write_debug_session_descriptor"]()
        epa_ide_dir = str(Path(__file__).parent)
        env = dict(os.environ)
        if session_path:
            env["ELARA_DEBUG_SESSION"] = str(session_path)
        existing_pypath = env.get("PYTHONPATH", "")
        env["PYTHONPATH"] = epa_ide_dir + (":" + existing_pypath if existing_pypath else "")

        proc = subprocess.Popen(
            [sys.executable, "-m", "debugpy",
             "--listen", f"127.0.0.1:{port}",
             "--wait-for-client",
             str(entry)],
            cwd=str(python_root),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )
        python_dbg_state["proc"] = proc

        def _reader(stream, tag):
            for raw in stream:
                line = raw.decode(errors="replace").rstrip()
                ctx["_append_build_output"](client, f"[python-{tag}] {line}\n")
        threading.Thread(target=_reader, args=(proc.stdout, "out"), daemon=True).start()
        threading.Thread(target=_reader, args=(proc.stderr, "err"), daemon=True).start()

        ctx["_append_build_output"](client, f"[python] launching debugpy on port {port}\n")

        # Wait for debugpy to open the socket
        deadline = time.time() + 8.0
        dap = None
        while time.time() < deadline:
            if proc.poll() is not None:
                raise RuntimeError(f"Python process exited (code {proc.returncode}) before accepting connections")
            try:
                dap = PythonDapClient("127.0.0.1", port)
                break
            except ConnectionRefusedError:
                time.sleep(0.2)
        if dap is None:
            raise RuntimeError("Timed out waiting for debugpy to accept connections")

        python_dbg_state["dap"] = dap
        dap.set_event_handler(_python_dap_on_event)

        # DAP handshake (based on wire-level testing against debugpy 1.8.x):
        #  1. initialize  — response arrives immediately
        #  2. attach      — fires the 'initialized' event (arrives ~0.5s later);
        #                   attach response is deferred until after configurationDone
        #  3. setExceptionBreakpoints — response deferred until configurationDone
        #  4. configurationDone — triggers all deferred responses + process/thread events
        dap.send("initialize", {
            "clientID": "epa-ide",
            "adapterID": "python",
            "pathFormat": "path",
            "linesStartAt1": True,
            "columnsStartAt1": True,
            "supportsVariableType": True,
            "supportsRunInTerminalRequest": False,
        })
        dap.fire("attach", {"__restart": False})
        dap.fire("setExceptionBreakpoints", {"filters": ["uncaught"]})
        dap.send("configurationDone", {})

        python_dbg_state["started"] = True
        python_dbg_state["status"] = "Running"
        ctx["_append_build_output"](client, "[python] DAP handshake complete — running\n")
        ctx["_open_file_tab"](client, str(entry), make_permanent=True)

        # Poll threads periodically while the script runs so the list stays
        # populated without requiring a stop event.
        def _thread_poller():
            for delay in (0.5, 2.0, 5.0):
                time.sleep(delay)
                if not python_dbg_state.get("started") or python_dbg_state.get("stopped"):
                    return
                try:
                    resp = dap.send("threads", {}, timeout=3.0)
                    threads = (resp.get("body") or {}).get("threads", [])
                    c2 = client_ref.get("client")
                    if c2 and threads:
                        items = [
                            {"id": f"python.thread.{t['id']}",
                             "label": f"Thread {t['id']}: {t.get('name', '?')}"}
                            for t in threads
                        ]
                        _set_python_thread_items(c2, items)
                except Exception:
                    pass
        threading.Thread(target=_thread_poller, daemon=True).start()

    def _python_dbg_handle_action(client, action_id: str):
        try:
            if action_id == "debug.python.reset":
                _set_python_vm_status(client, "starting")
                _python_dbg_launch(client)
                _python_dbg_refresh_ui(client)
                ctx["_refresh_status_panel"](client)
                return True

            if action_id == "debug.python.stop":
                _python_dbg_stop(client)
                return True

            dap = python_dbg_state.get("dap")
            if not python_dbg_state.get("started") or dap is None:
                raise RuntimeError("Python debugger is not running.")

            thread_id = python_dbg_state.get("thread_id") or 1

            if action_id == "debug.python.continue":
                dap.send("continue", {"threadId": thread_id})
                python_dbg_state["stopped"] = False
                python_dbg_state["status"] = "Running"
                _set_python_vm_status(client, "running")
                _set_python_status_text(client, "Running")
                return True

            if action_id == "debug.python.step_over":
                dap.send("next", {"threadId": thread_id})
                python_dbg_state["status"] = "Stepping…"
                _set_python_status_text(client, "Stepping…")
                return True

            if action_id == "debug.python.step_into":
                dap.send("stepIn", {"threadId": thread_id})
                python_dbg_state["status"] = "Stepping…"
                _set_python_status_text(client, "Stepping…")
                return True

            if action_id == "debug.python.step_out":
                dap.send("stepOut", {"threadId": thread_id})
                python_dbg_state["status"] = "Stepping…"
                _set_python_status_text(client, "Stepping…")
                return True

            if action_id == "debug.python.pause":
                dap.send("pause", {"threadId": thread_id})
                python_dbg_state["status"] = "Pausing…"
                _set_python_status_text(client, "Pausing…")
                return True

        except Exception as exc:
            python_dbg_state["status"] = f"Error: {exc}"
            _set_python_vm_status(client, "error")
            _set_python_status_text(client, python_dbg_state["status"])
            ctx["_append_build_output"](client, f"[python-error] {exc}\n")
            return True
        return False

    # ── Register all functions into ctx ──────────────────────────────────────
    ctx.update({
        "_set_python_thread_items":     _set_python_thread_items,
        "_set_python_status_text":      _set_python_status_text,
        "_set_python_vm_status":        _set_python_vm_status,
        "_set_python_vm_buttons":       _set_python_vm_buttons,
        "_set_python_frame_items":      _set_python_frame_items,
        "_set_python_locals_text":      _set_python_locals_text,
        "_set_python_registers_text":   _set_python_registers_text,
        "_set_python_memory_text":      _set_python_memory_text,
        "_python_dbg_refresh_ui":       _python_dbg_refresh_ui,
        "_python_dbg_refresh_threads":  _python_dbg_refresh_threads,
        "_python_dbg_show_thread":      _python_dbg_show_thread,
        "_python_dap_on_event":         _python_dap_on_event,
        "_python_dbg_stop":             _python_dbg_stop,
        "_python_dbg_launch":           _python_dbg_launch,
        "_python_dbg_handle_action":    _python_dbg_handle_action,
    })
