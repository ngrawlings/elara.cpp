import json
import socket
import struct
import threading
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

    def connect(self):
        if self._socket is not None:
            return self

        self._socket = socket.create_connection((self.host, self.port))
        self._running = True
        self._reader = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader.start()
        return self

    def close(self):
        self._running = False
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
            raise ElaraUiRpcError(code, message)

        return state.get("result")

    def notify(self, method: str, params: Optional[dict] = None, timeout: float = 5.0):
        return self.call(method, params=params, timeout=timeout)

    def load_document(self, builder_or_json, timeout: float = 5.0):
        if hasattr(builder_or_json, "to_json"):
            document = builder_or_json.to_json(indent=2)
        else:
            document = str(builder_or_json)
        return self.call("ui.loadDocument", {"document": document}, timeout=timeout)

    def snapshot(self, timeout: float = 5.0):
        return self.call("ui.snapshot", {}, timeout=timeout)

    def snapshot_widget(self, target: str, timeout: float = 5.0):
        return self.call("ui.snapshotWidget", {"target": target}, timeout=timeout)

    def set_text(self, target: str, value: str, timeout: float = 5.0):
        return self.call("ui.setText", {"target": target, "value": value}, timeout=timeout)

    def set_focus(self, target: str, timeout: float = 5.0):
        return self.call("ui.setFocus", {"target": target}, timeout=timeout)

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

    def dispatch_key_down(self, keyval: int, timeout: float = 5.0):
        return self.call("ui.dispatchKeyDown", {"keyval": keyval}, timeout=timeout)

    def dispatch_key_up(self, keyval: int, timeout: float = 5.0):
        return self.call("ui.dispatchKeyUp", {"keyval": keyval}, timeout=timeout)

    def _send_payload(self, payload: dict):
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
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

        handler = self._handlers.get(method)
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
