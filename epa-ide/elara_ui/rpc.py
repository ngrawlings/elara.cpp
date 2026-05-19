import json
import socket
import struct
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Dict, Optional, Tuple


class ElaraUiRpcError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


class ElaraUiRpcClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 18777):
        self.host = host
        self.port = int(port)
        self._socket: Optional[socket.socket] = None
        self._reader: Optional[threading.Thread] = None
        self._running = False
        self._send_lock = threading.Lock()
        self._pending_lock = threading.Lock()
        self._next_request_id = 1
        self._pending: Dict[str, Tuple[threading.Event, dict]] = {}
        self._handlers: Dict[str, Callable[[dict], object]] = {}
        self._event_log_file = None
        self._event_log_lock = threading.Lock()
        self._event_log_seq = 0
        self._artifact_root: Optional[Path] = None
        self._main_document_json: Optional[str] = None
        self._window_documents: Dict[str, str] = {}

    def set_find_widget_artifact_root(self, path: str):
        artifact_root = Path(path).expanduser().resolve()
        artifact_root.mkdir(parents=True, exist_ok=True)
        self._artifact_root = artifact_root
        return self

    def connect(self):
        if self._socket is not None:
            return self

        self._socket = socket.create_connection((self.host, self.port))
        self._running = True
        self._reader = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader.start()
        return self

    def set_event_log(self, path: str):
        """Open a JSONL event log file. Each received event is appended as one JSON line."""
        with self._event_log_lock:
            if self._event_log_file is not None:
                self._event_log_file.close()
            self._event_log_file = open(path, "a")
            ts_ms = int(time.monotonic() * 1000)
            self._event_log_file.write(
                json.dumps({"type": "session_start", "ts_ms": ts_ms}) + "\n"
            )
            self._event_log_file.flush()

    def _log_event(self, payload: dict):
        with self._event_log_lock:
            if self._event_log_file is None:
                return
            ts_ms = int(time.monotonic() * 1000)
            seq = self._event_log_seq
            self._event_log_seq += 1
            params = payload.get("params") or {}
            entry = {
                "type": "event_in",
                "seq": seq,
                "ts_ms": ts_ms,
                "method": payload.get("method"),
                "target": params.get("target"),
                "action": params.get("action"),
                "payload": params.get("payload"),
            }
            if "_seq" in params:
                entry["server_seq"] = params["_seq"]
            if "_ts_ms" in params:
                entry["server_ts_ms"] = params["_ts_ms"]
            self._event_log_file.write(json.dumps(entry) + "\n")
            self._event_log_file.flush()

    def close(self):
        self._running = False
        with self._event_log_lock:
            if self._event_log_file is not None:
                self._event_log_file.close()
                self._event_log_file = None
        if self._socket is not None:
            try:
                self._socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self._socket.close()
            self._socket = None

        with self._pending_lock:
            for event, state in self._pending.values():
                state["error"] = ("connection_closed", "The RPC peer connection was closed")
                event.set()
            self._pending.clear()

    def __enter__(self):
        return self.connect()

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def add_handler(self, method: str, handler: Callable[[dict], object]):
        self._handlers[method] = handler
        return self

    def call(self, method: str, params: Optional[dict] = None, timeout: float = 5.0):
        self.connect()
        request_id = str(self._next_request_id)
        self._next_request_id += 1
        params_dict = params if isinstance(params, dict) else {}

        request = {
            "id": request_id,
            "method": method,
            "params": params if params is not None else None,
        }

        event = threading.Event()
        state = {}
        with self._pending_lock:
            self._pending[request_id] = (event, state)

        self._send_payload(request)

        if not event.wait(timeout):
            with self._pending_lock:
                self._pending.pop(request_id, None)
            raise ElaraUiRpcError("timeout", "The RPC peer did not respond in time")

        if "error" in state:
            code, message = state["error"]
            self._maybe_capture_find_widget_artifact(method, params_dict, code, message)
            raise ElaraUiRpcError(code, message)

        result = state.get("result")
        self._record_ui_documents(method, params_dict, result)
        return result

    def notify(self, method: str, params: Optional[dict] = None, timeout: float = 5.0):
        return self.call(method, params=params, timeout=timeout)

    def load_document(self, builder_or_json, timeout: float = 5.0):
        if hasattr(builder_or_json, "to_json"):
            document = builder_or_json.to_json(indent=2)
        else:
            document = str(builder_or_json)
        return self.call("ui.loadDocument", {"document": document}, timeout=timeout)

    def open_window(self, window_id: str, title: str, width: int, height: int, builder_or_json, timeout: float = 5.0):
        if hasattr(builder_or_json, "to_json"):
            document = builder_or_json.to_json(indent=2)
        else:
            document = str(builder_or_json)
        return self.call(
            "ui.openWindow",
            {
                "window_id": window_id,
                "title": title,
                "width": int(width),
                "height": int(height),
                "document": document,
            },
            timeout=timeout,
        )

    def close_window(self, window_id: str, timeout: float = 5.0):
        return self.call("ui.closeWindow", {"window_id": window_id}, timeout=timeout)

    def snapshot(self, timeout: float = 5.0):
        return self.call("ui.snapshot", {}, timeout=timeout)

    def snapshot_widget(self, target: str, timeout: float = 5.0):
        return self.call("ui.snapshotWidget", {"target": target}, timeout=timeout)

    def get_grid_layout_state(self, target: str, timeout: float = 5.0):
        return self.call("ui.getGridLayoutState", {"target": target}, timeout=timeout)

    def get_window_state(self, timeout: float = 5.0):
        return self.call("ui.getWindowState", {}, timeout=timeout)

    def set_window_maximized(self, maximized: bool, timeout: float = 5.0):
        return self.call("ui.setWindowMaximized", {"maximized": bool(maximized)}, timeout=timeout)

    def set_window_decorated(self, decorated: bool, timeout: float = 5.0):
        return self.call("ui.setWindowDecorated", {"decorated": bool(decorated)}, timeout=timeout)

    def set_theme_mode(self, mode: str, timeout: float = 5.0):
        return self.call("ui.setThemeMode", {"mode": mode}, timeout=timeout)

    def configure_menu_bar_chrome(
        self,
        target: str,
        custom_chrome: bool,
        window_title: str,
        timeout: float = 5.0,
    ):
        return self.call(
            "ui.configureMenuBarChrome",
            {
                "target": target,
                "custom_chrome": bool(custom_chrome),
                "window_title": window_title,
            },
            timeout=timeout,
        )

    def replace_list_items(self, target: str, items: list, timeout: float = 5.0):
        document = json.dumps({"items": items}, separators=(",", ":"))
        return self.call("ui.replaceChildren", {"target": target, "document": document}, timeout=timeout)

    def set_text(self, target: str, value: str, timeout: float = 5.0):
        return self.call("ui.setText", {"target": target, "value": value}, timeout=timeout)

    def set_visible(self, target: str, visible: bool, timeout: float = 5.0):
        return self.call("ui.setVisible", {"target": target, "visible": bool(visible)}, timeout=timeout)

    def set_enabled(self, target: str, enabled: bool, timeout: float = 5.0):
        return self.call("ui.setEnabled", {"target": target, "enabled": bool(enabled)}, timeout=timeout)

    def set_read_only(self, target: str, read_only: bool, timeout: float = 5.0):
        return self.call("ui.setReadOnly", {"target": target, "read_only": bool(read_only)}, timeout=timeout)

    def set_code_editor_diagnostics(self, target: str, diagnostics: list, timeout: float = 5.0):
        return self.call(
            "ui.setCodeEditorDiagnostics",
            {"target": target, "diagnostics": diagnostics},
            timeout=timeout,
        )

    def set_focus(self, target: str, timeout: float = 5.0):
        return self.call("ui.setFocus", {"target": target}, timeout=timeout)

    def lock_focus(self, target: str, timeout: float = 5.0):
        return self.call("ui.lockFocus", {"target": target}, timeout=timeout)

    def clear_focus_lock(self, timeout: float = 5.0):
        return self.call("ui.clearFocusLock", {}, timeout=timeout)

    def set_window_title(self, title: str, timeout: float = 5.0):
        return self.call("ui.setWindowTitle", {"title": title}, timeout=timeout)

    def enable_event(self, action: str, timeout: float = 5.0):
        return self.call("ui.enableEvent", {"action": action}, timeout=timeout)

    def disable_event(self, action: str, timeout: float = 5.0):
        return self.call("ui.disableEvent", {"action": action}, timeout=timeout)

    def dispatch_mouse_move(self, x: float, y: float, timeout: float = 5.0):
        return self.call("ui.dispatchMouseMove", {"x": x, "y": y}, timeout=timeout)

    def dispatch_mouse_down(self, button: int, x: float, y: float, timeout: float = 5.0):
        return self.call("ui.dispatchMouseDown", {"button": button, "x": x, "y": y}, timeout=timeout)

    def dispatch_mouse_up(self, button: int, x: float, y: float, timeout: float = 5.0):
        return self.call("ui.dispatchMouseUp", {"button": button, "x": x, "y": y}, timeout=timeout)

    def dispatch_mouse_scroll(self, dx: float, dy: float, x: float, y: float, timeout: float = 5.0):
        return self.call("ui.dispatchMouseScroll", {"dx": dx, "dy": dy, "x": x, "y": y}, timeout=timeout)

    def dispatch_key_down(self, keyval: int, timeout: float = 5.0):
        return self.call("ui.dispatchKeyDown", {"keyval": keyval}, timeout=timeout)

    def dispatch_key_up(self, keyval: int, timeout: float = 5.0):
        return self.call("ui.dispatchKeyUp", {"keyval": keyval}, timeout=timeout)

    def perform_action(self, target: str, action: str, timeout: float = 5.0):
        return self.call("ui.performAction", {"target": target, "action": action}, timeout=timeout)

    def perform_focused_action(self, action: str, timeout: float = 5.0):
        return self.call("ui.performFocusedAction", {"action": action}, timeout=timeout)

    def _send_payload(self, payload: dict):
        body = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        frame = struct.pack(">I", len(body)) + body
        with self._send_lock:
            if self._socket is None:
                raise ElaraUiRpcError("not_connected", "The RPC peer is not connected")
            self._socket.sendall(frame)

    def _reader_loop(self):
        try:
            while self._running and self._socket is not None:
                payload = self._recv_payload()
                if payload is None:
                    break
                self._handle_payload(payload)
        finally:
            self.close()

    def _recv_payload(self):
        prefix = self._recv_exact(4)
        if not prefix:
            return None
        length = struct.unpack(">I", prefix)[0]
        body = self._recv_exact(length)
        if body is None:
            return None
        return json.loads(body.decode("utf-8"))

    def _recv_exact(self, size: int):
        if self._socket is None:
            return None
        chunks = bytearray()
        while len(chunks) < size:
            chunk = self._socket.recv(size - len(chunks))
            if not chunk:
                return None
            chunks.extend(chunk)
        return bytes(chunks)

    def _handle_payload(self, payload: dict):
        payload_id = payload.get("id")

        if payload_id is not None and "ok" in payload:
            with self._pending_lock:
                pending = self._pending.pop(str(payload_id), None)
            if pending is None:
                return
            event, state = pending
            if payload.get("ok") is True:
                state["result"] = payload.get("result")
            else:
                error = payload.get("error") or {}
                state["error"] = (error.get("code", "unknown_error"), error.get("message", "Unknown RPC error"))
            event.set()
            return

        method = payload.get("method")
        params = payload.get("params")
        if method is None:
            return

        if self._event_log_file is not None:
            self._log_event(payload)

        handler = self._handlers.get(method)

        def _dispatch():
            ok = True
            result = None
            error = None
            try:
                if handler is not None:
                    result = handler(params)
                else:
                    result = {"received": True}
            except Exception as exc:
                ok = False
                error = {"code": "handler_error", "message": str(exc)}
            if payload_id is not None:
                response = {"id": str(payload_id), "ok": ok}
                if ok:
                    response["result"] = result
                else:
                    response["error"] = error
                self._send_payload(response)

        threading.Thread(target=_dispatch, daemon=True).start()

    def _record_ui_documents(self, method: str, params: dict, result) -> None:
        if not method.startswith("ui."):
            return

        if method == "ui.loadDocument":
            document = params.get("document")
            if isinstance(document, str) and document:
                self._main_document_json = document
            return

        if method == "ui.openWindow":
            window_id = params.get("window_id")
            document = params.get("document")
            if isinstance(window_id, str) and window_id and isinstance(document, str) and document:
                self._window_documents[window_id] = document
            return

        if method == "ui.closeWindow":
            window_id = params.get("window_id")
            if isinstance(window_id, str) and window_id:
                self._window_documents.pop(window_id, None)

    def _maybe_capture_find_widget_artifact(self, method: str, params: dict, code: str, message: str) -> None:
        if self._artifact_root is None:
            return

        if code not in {
            "widget_not_found",
            "ambiguous_widget_target",
            "invalid_target_window",
        }:
            return

        if not method.startswith("ui.") or "target" not in params:
            return

        timestamp = datetime.now(timezone.utc)
        stamp = timestamp.strftime("%Y%m%d-%H%M%S")
        artifact_dir = self._artifact_root / f"{stamp}-find-widget-error"
        suffix = 1
        while artifact_dir.exists():
            suffix += 1
            artifact_dir = self._artifact_root / f"{stamp}-find-widget-error-{suffix}"
        artifact_dir.mkdir(parents=True, exist_ok=True)

        request_payload = {
            "captured_at_utc": timestamp.isoformat(),
            "host": self.host,
            "port": self.port,
            "method": method,
            "params": params,
            "error": {
                "code": code,
                "message": message,
            },
        }
        (artifact_dir / "request.json").write_text(
            json.dumps(request_payload, indent=2, ensure_ascii=False),
            encoding="utf-8",
        )

        if self._main_document_json:
            (artifact_dir / "ui_document_main.json").write_text(
                self._main_document_json,
                encoding="utf-8",
            )

        if self._window_documents:
            windows_dir = artifact_dir / "windows"
            windows_dir.mkdir(parents=True, exist_ok=True)
            for window_id, document in self._window_documents.items():
                safe_name = self._safe_filename(window_id) or "window"
                (windows_dir / f"{safe_name}.json").write_text(document, encoding="utf-8")

        snapshot_payload = None
        try:
            snapshot_payload = self.snapshot(timeout=2.0)
        except Exception as exc:
            snapshot_payload = {
                "snapshot_error": {
                    "type": type(exc).__name__,
                    "message": str(exc),
                }
            }

        if snapshot_payload is not None:
            (artifact_dir / "runtime_snapshot.json").write_text(
                json.dumps(snapshot_payload, indent=2, ensure_ascii=False),
                encoding="utf-8",
            )

        summary_lines = [
            f"captured_at_utc={timestamp.isoformat()}",
            f"host={self.host}",
            f"port={self.port}",
            f"method={method}",
            f"target={params.get('target', '')}",
            f"window_id={params.get('window_id', '')}",
            f"error_code={code}",
            f"error_message={message}",
            f"main_document={'ui_document_main.json' if self._main_document_json else ''}",
            f"window_document_count={len(self._window_documents)}",
            "runtime_snapshot=runtime_snapshot.json",
        ]
        (artifact_dir / "summary.txt").write_text("\n".join(summary_lines) + "\n", encoding="utf-8")

        manifest_lines = [
            "request.json",
            "summary.txt",
        ]
        if self._main_document_json:
            manifest_lines.append("ui_document_main.json")
        if self._window_documents:
            for window_id in self._window_documents:
                safe_name = self._safe_filename(window_id) or "window"
                manifest_lines.append(f"windows/{safe_name}.json")
        manifest_lines.append("runtime_snapshot.json")
        (artifact_dir / "manifest.txt").write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")

        (self._artifact_root / "latest.txt").write_text(str(artifact_dir) + "\n", encoding="utf-8")

    def _safe_filename(self, value: str) -> str:
        return "".join(ch if ch.isalnum() or ch in ("-", "_", ".") else "_" for ch in str(value))
