import json
import socket
import struct
import threading
import time


class PythonDapClient:
    """DAP (Debug Adapter Protocol) client for communicating with debugpy."""

    def __init__(self, host: str, port: int):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.connect((host, port))
        self._seq = 0
        self._seq_lock = threading.Lock()
        self._send_lock = threading.Lock()
        self._pending: dict[int, threading.Event] = {}
        self._responses: dict[int, dict] = {}
        self._pending_lock = threading.Lock()
        self._event_handler = None
        self._recv_buf = b""
        self._closed = False
        self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_thread.start()

    def set_event_handler(self, handler):
        self._event_handler = handler

    def send(self, command: str, arguments: dict = None, timeout: float = 5.0) -> dict:
        with self._seq_lock:
            self._seq += 1
            seq = self._seq
        msg = {"seq": seq, "type": "request", "command": command}
        if arguments is not None:
            msg["arguments"] = arguments
        body = json.dumps(msg).encode()
        frame = f"Content-Length: {len(body)}\r\n\r\n".encode() + body
        ev = threading.Event()
        with self._pending_lock:
            self._pending[seq] = ev
        try:
            with self._send_lock:
                self._sock.sendall(frame)
        except Exception:
            with self._pending_lock:
                self._pending.pop(seq, None)
            return {}
        ev.wait(timeout)
        with self._pending_lock:
            return self._responses.pop(seq, {})

    def fire(self, command: str, arguments: dict = None):
        """Send a request without waiting for a response (fire-and-forget)."""
        with self._seq_lock:
            self._seq += 1
            seq = self._seq
        msg = {"seq": seq, "type": "request", "command": command}
        if arguments is not None:
            msg["arguments"] = arguments
        body = json.dumps(msg).encode()
        frame = f"Content-Length: {len(body)}\r\n\r\n".encode() + body
        try:
            with self._send_lock:
                self._sock.sendall(frame)
        except Exception:
            pass

    def close(self):
        self._closed = True
        try:
            self._sock.close()
        except Exception:
            pass

    def _recv_loop(self):
        while not self._closed:
            try:
                chunk = self._sock.recv(4096)
                if not chunk:
                    break
                self._recv_buf += chunk
                self._process_buf()
            except Exception:
                break
        # unblock any waiting send() calls
        with self._pending_lock:
            for ev in self._pending.values():
                ev.set()

    def _process_buf(self):
        while True:
            sep = self._recv_buf.find(b"\r\n\r\n")
            if sep == -1:
                break
            header = self._recv_buf[:sep].decode(errors="replace")
            content_length = 0
            for line in header.split("\r\n"):
                if line.lower().startswith("content-length:"):
                    try:
                        content_length = int(line.split(":", 1)[1].strip())
                    except ValueError:
                        pass
            start = sep + 4
            if len(self._recv_buf) < start + content_length:
                break
            raw = self._recv_buf[start: start + content_length]
            self._recv_buf = self._recv_buf[start + content_length:]
            try:
                self._dispatch(json.loads(raw))
            except Exception:
                pass

    def _dispatch(self, msg: dict):
        msg_type = msg.get("type")
        if msg_type == "response":
            seq = msg.get("request_seq")
            with self._pending_lock:
                ev = self._pending.pop(seq, None)
                if ev is not None:
                    self._responses[seq] = msg
                    ev.set()
        elif msg_type == "event":
            handler = self._event_handler
            if handler:
                try:
                    handler(msg.get("event", ""), msg.get("body") or {})
                except Exception:
                    pass
