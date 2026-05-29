"""BRPC client for external Python logic connecting to the epa-ide external logic bridge.

Self-contained — no epa-ide path required. Connection info is read from the
ELARA_DEBUG_SESSION JSON file whose path is in the ELARA_DEBUG_SESSION env var.

Wire format: 4-byte big-endian length prefix + BRPC binary body.

Typical usage::

    import ext_logic_client

    client = ext_logic_client.ExtLogicClient.from_env()
    client.connect_retry(timeout=10.0)
    client.register(name="my-python-logic")
    # ... call methods, do work ...
    client.close()
"""

import json
import os
import socket
import struct
import threading
import time
import uuid


# ── BRPC codec (inlined — mirrors elara_ui/rpc.py _BRpcCodec) ────────────────

class _BRpcCodec:
    _NAMED_BYTE   = 10
    _NAMED_STRING = 14
    _ARRAY        = 5

    @staticmethod
    def _ns(name: str, value: str) -> bytes:
        nb = name.encode("utf-8")
        vb = value.encode("utf-8")
        return (
            bytes([_BRpcCodec._NAMED_STRING])
            + struct.pack(">I", len(nb)) + nb
            + struct.pack(">I", len(vb)) + vb
        )

    @staticmethod
    def _nb(name: str, value: int) -> bytes:
        nb = name.encode("utf-8")
        return bytes([_BRpcCodec._NAMED_BYTE]) + struct.pack(">I", len(nb)) + nb + bytes([value & 0xFF])

    @staticmethod
    def _array(fields: bytes, count: int) -> bytes:
        return (
            bytes([_BRpcCodec._ARRAY])
            + struct.pack(">I", len(fields))
            + struct.pack(">I", count)
            + fields
        )

    @classmethod
    def encode(cls, payload: dict) -> bytes:
        has_id = "id" in payload
        has_ok = "ok" in payload
        if has_id and has_ok:
            pid = str(payload["id"])
            if payload["ok"]:
                result_json = json.dumps(payload.get("result"), separators=(",", ":"), ensure_ascii=False)
                fields = cls._ns("id", pid) + cls._nb("ok", 1) + cls._ns("result", result_json)
                return cls._array(fields, 3)
            else:
                err = payload.get("error") or {}
                fields = (cls._ns("id", pid) + cls._nb("ok", 0)
                          + cls._ns("code", err.get("code", ""))
                          + cls._ns("msg",  err.get("message", "")))
                return cls._array(fields, 4)
        elif has_id:
            pid = str(payload["id"])
            method = payload.get("method", "")
            params = payload.get("params")
            pjson = "null" if params is None else json.dumps(params, separators=(",", ":"), ensure_ascii=False)
            fields = cls._ns("id", pid) + cls._ns("method", method) + cls._ns("params", pjson)
            return cls._array(fields, 3)
        else:
            method = payload.get("method", "")
            params = payload.get("params")
            pjson = "null" if params is None else json.dumps(params, separators=(",", ":"), ensure_ascii=False)
            fields = cls._ns("method", method) + cls._ns("params", pjson)
            return cls._array(fields, 2)

    @classmethod
    def decode(cls, data: bytes) -> dict:
        pos = 0

        def u8() -> int:
            nonlocal pos
            v = data[pos]; pos += 1; return v

        def u32() -> int:
            nonlocal pos
            v = struct.unpack_from(">I", data, pos)[0]; pos += 4; return v

        def read_str() -> str:
            nonlocal pos
            length = u32()
            s = data[pos:pos + length].decode("utf-8"); pos += length; return s

        type_byte = u8()
        if type_byte != cls._ARRAY:
            raise ValueError(f"Expected BRPC array, got type {type_byte}")
        u32()  # total byte length (unused)
        count = u32()

        fields: dict = {}
        for _ in range(count):
            ftype = u8()
            if ftype == cls._NAMED_STRING:
                name  = read_str()
                value = read_str()
                fields[name] = value
            elif ftype == cls._NAMED_BYTE:
                name  = read_str()
                value = u8()
                fields[name] = value
            else:
                raise ValueError(f"Unexpected BRPC field type: {ftype}")

        if "id" in fields and "ok" in fields:
            if fields["ok"]:
                rjson = fields.get("result", "null")
                result = None if rjson == "null" else json.loads(rjson)
                return {"id": fields["id"], "ok": True, "result": result}
            else:
                return {"id": fields["id"], "ok": False,
                        "error": {"code": fields.get("code", ""), "message": fields.get("msg", "")}}
        elif "id" in fields:
            pjson = fields.get("params", "null")
            params = None if pjson == "null" else json.loads(pjson)
            return {"id": fields["id"], "method": fields["method"], "params": params}
        elif "method" in fields:
            pjson = fields.get("params", "null")
            params = None if pjson == "null" else json.loads(pjson)
            return {"method": fields["method"], "params": params}
        else:
            raise ValueError("Cannot determine BRPC message type from fields")


# ── helpers ───────────────────────────────────────────────────────────────────

def _recv_exactly(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("connection closed mid-message")
        buf.extend(chunk)
    return bytes(buf)


# ── public API ────────────────────────────────────────────────────────────────

class ExtLogicError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(f"[{code}] {message}")
        self.code = code
        self.message = message


class ExtLogicClient:
    """BRPC client that connects to the IDE's external logic bridge.

    The bridge port is published in the ELARA_DEBUG_SESSION JSON file so any
    Python process launched with that env var can discover it automatically.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 0):
        self._host = host
        self._port = port
        self._sock: socket.socket | None = None
        self._lock = threading.RLock()

    # ── factory methods ───────────────────────────────────────────────────────

    @classmethod
    def from_env(cls, env_var: str = "ELARA_DEBUG_SESSION") -> "ExtLogicClient":
        """Load host/port from the debug session file named by *env_var*."""
        path = os.environ.get(env_var, "")
        if not path:
            raise RuntimeError(f"{env_var} environment variable is not set")
        return cls.from_session_file(path)

    @classmethod
    def from_session_file(cls, path: str) -> "ExtLogicClient":
        """Load host/port from an ELARA_DEBUG_SESSION JSON file."""
        with open(path, "r", encoding="utf-8") as f:
            cfg = json.load(f)
        host = cfg.get("ext_logic_host") or "127.0.0.1"
        port = int(cfg.get("ext_logic_port") or 0)
        if not port:
            raise RuntimeError(f"ext_logic_port not in session file: {path}")
        return cls(host, port)

    # ── connection lifecycle ──────────────────────────────────────────────────

    def connect(self, timeout: float = 5.0) -> None:
        with self._lock:
            if self._sock:
                return
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(timeout)
            s.connect((self._host, self._port))
            s.settimeout(None)
            self._sock = s

    def connect_retry(self, timeout: float = 10.0, interval: float = 0.25) -> None:
        deadline = time.monotonic() + timeout
        last_exc: Exception | None = None
        while time.monotonic() < deadline:
            try:
                self.connect()
                return
            except (ConnectionRefusedError, OSError) as exc:
                last_exc = exc
                time.sleep(interval)
        raise TimeoutError(f"ext-logic bridge not reachable after {timeout}s: {last_exc}")

    def close(self) -> None:
        with self._lock:
            if self._sock:
                try:
                    self._sock.close()
                except Exception:
                    pass
                self._sock = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *_):
        self.close()

    @property
    def connected(self) -> bool:
        return self._sock is not None

    # ── low-level wire I/O ────────────────────────────────────────────────────

    def _send_raw(self, data: bytes) -> None:
        with self._lock:
            if not self._sock:
                raise RuntimeError("not connected")
            self._sock.sendall(struct.pack(">I", len(data)) + data)

    def _recv_raw(self, timeout: float = 30.0) -> bytes:
        with self._lock:
            if not self._sock:
                raise RuntimeError("not connected")
            sock = self._sock
        sock.settimeout(timeout)
        try:
            header = _recv_exactly(sock, 4)
            length = struct.unpack(">I", header)[0]
            if length > 16 * 1024 * 1024:
                raise RuntimeError(f"ext-logic frame too large: {length} bytes")
            return _recv_exactly(sock, length)
        finally:
            sock.settimeout(None)

    def _close_on_error(self) -> None:
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None

    # ── RPC ───────────────────────────────────────────────────────────────────

    def call(self, method: str, params=None, timeout: float = 30.0) -> object:
        req_id = str(uuid.uuid4())
        body: dict = {"id": req_id, "method": method}
        if params is not None:
            body["params"] = params
        with self._lock:
            try:
                self._send_raw(_BRpcCodec.encode(body))
                raw = self._recv_raw(timeout=timeout)
                resp = _BRpcCodec.decode(raw)
                if resp.get("id") != req_id:
                    raise RuntimeError(f"response id mismatch: {resp.get('id')} != {req_id}")
                if not resp.get("ok", False):
                    err = resp.get("error", {})
                    raise ExtLogicError(err.get("code", "unknown"), err.get("message", ""))
                return resp.get("result")
            except ExtLogicError:
                raise
            except Exception:
                self._close_on_error()
                raise

    # ── high-level API ────────────────────────────────────────────────────────

    def ping(self) -> str:
        """Round-trip check. Returns 'pong' on success."""
        return self.call("ext.ping")

    def register(self, name: str = "") -> dict:
        """Announce this logic to the IDE and turn the External Logic indicator orange."""
        return self.call("ext.register", {"name": name})
