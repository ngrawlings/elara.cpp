"""TCP BRPC client for epa-dbg (port 18878).

Wire format: 4-byte big-endian length prefix + BRPC binary body.

Request  (BRPC array of 3 named fields): "id", "method", "params" (JSON string)
Response (BRPC array):  namedString "id", namedByte "ok"=1, namedString "result"
                     or namedString "id", namedByte "ok"=0, namedString "code",
                        namedString "msg"
"""

import select
import socket
import struct
import threading
import time
import uuid

from elara_ui.rpc import _BRpcCodec


class EpaDbgError(RuntimeError):
    def __init__(self, code: str, message: str):
        super().__init__(f"[{code}] {message}")
        self.code = code
        self.message = message


class EpaDbgClient:
    DEFAULT_PORT = 18878

    def __init__(self, host: str = "127.0.0.1", port: int = DEFAULT_PORT):
        self._host = host
        self._port = port
        self._sock: socket.socket | None = None
        # epa-dbg is request/response over one ordered TCP stream. The full
        # transaction must be serialized; otherwise concurrent callers can read
        # each other's length-prefixed response and corrupt the stream.
        self._lock = threading.RLock()

    # ------------------------------------------------------------------
    # Connection lifecycle
    # ------------------------------------------------------------------

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
        raise TimeoutError(f"epa-dbg not reachable after {timeout}s: {last_exc}")

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

    # ------------------------------------------------------------------
    # Low-level wire I/O
    # ------------------------------------------------------------------

    def _send_raw(self, data: bytes) -> None:
        header = struct.pack(">I", len(data))
        with self._lock:
            if not self._sock:
                raise RuntimeError("not connected")
            self._sock.sendall(header + data)

    def _recv_raw(self, timeout: float = 30.0) -> bytes:
        with self._lock:
            if not self._sock:
                raise RuntimeError("not connected")
            sock = self._sock
        sock.settimeout(timeout)
        try:
            header = _recv_exactly(sock, 4)
            length = struct.unpack(">I", header)[0]
            if length > 64 * 1024 * 1024:
                raise RuntimeError(f"epa-dbg frame too large: {length} bytes")
            return _recv_exactly(sock, length)
        finally:
            sock.settimeout(None)

    def _close_unlocked(self) -> None:
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None

    # ------------------------------------------------------------------
    # RPC
    # ------------------------------------------------------------------

    def call(self, method: str, params=None, timeout: float = 30.0) -> object:
        req_id = str(uuid.uuid4())
        body = {"id": req_id, "method": method}
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
                    raise EpaDbgError(err.get("code", "unknown"), err.get("message", ""))
                return resp.get("result")
            except EpaDbgError:
                raise
            except Exception:
                # After a framing/decode/id error the stream position is no
                # longer trustworthy. Force the next call to reconnect instead
                # of compounding one bad read into many spooky follow-on errors.
                self._close_unlocked()
                raise

    # ------------------------------------------------------------------
    # High-level API
    # ------------------------------------------------------------------

    def ping(self) -> str:
        return self.call("epa.ping")

    # Kernel lifecycle

    def create(self, kernel_id: int = 0) -> dict:
        return self.call("epa.debug.create", {"kernel_id": kernel_id})

    def destroy(self, kernel_id: int = 0) -> dict:
        return self.call("epa.debug.destroy", {"kernel_id": kernel_id})

    def reset(self, kernel_id: int = 0) -> dict:
        """Destroy and recreate the kernel — equivalent to a clean reload."""
        try:
            self.destroy(kernel_id)
        except EpaDbgError:
            pass
        return self.create(kernel_id)

    def set_kernel_id(self, kernel_id: int, name: str) -> dict:
        return self.call("epa.debug.setKernelId", {"kernel_id": kernel_id, "name": name})

    # Loading programs

    def load_asm(self, kernel_id: int, asm_text: str) -> dict:
        return self.call("epa.debug.loadAsm", {"kernel_id": kernel_id, "asm_path": asm_text})

    def load_bundle(self, kernel_id: int, bundle_path: str) -> dict:
        return self.call("epa.debug.loadBundle",
                         {"kernel_id": kernel_id, "bundle_path": bundle_path})

    # Ingress

    def ingress_push_hex(self, hex_bytes: str, wid: int = 1, tag: int = 0, path_id: str = "") -> dict:
        params: dict = {"wid": wid, "tag": tag, "payload_hex": hex_bytes}
        if path_id:
            params["path_id"] = path_id
        return self.call("epa.debug.ingressPushHex", params)

    # Execution control

    def step(self, kernel_id: int, ticks: int = 1, path_id: str = "", wid: int | None = None) -> dict:
        params: dict = {"kernel_id": kernel_id, "ticks": ticks}
        if path_id:
            params["path_id"] = path_id
        if wid is not None:
            params["wid"] = int(wid)
        return self.call("epa.debug.step",
                         params,
                         timeout=60.0)

    def run(self, kernel_id: int, max_ticks: int = 0, path_id: str = "") -> dict:
        params: dict = {"kernel_id": kernel_id, "max_ticks": max_ticks}
        if path_id:
            params["path_id"] = path_id
        return self.call("epa.debug.run",
                         params,
                         timeout=120.0)

    def run_to_wait(self, wid: int, max_ticks: int = 500000, path_id: str = "") -> dict:
        """Run until the specified worker reaches waiting_for_data, halted, or faulted."""
        params: dict = {"wid": wid, "max_ticks": max_ticks}
        if path_id:
            params["path_id"] = path_id
        return self.call("epa.debug.runToWait",
                         params,
                         timeout=120.0)

    def step_boundary(self, wid: int, map_path: str, step_mode: str, path_id: str = "", max_ticks: int = 4096) -> dict:
        params: dict = {
            "wid": wid,
            "map_path": map_path,
            "step_mode": step_mode,
            "max_ticks": max_ticks,
        }
        if path_id:
            params["path_id"] = path_id
        return self.call("epa.debug.stepBoundary", params, timeout=120.0)

    def interrupt(self, kernel_id: int) -> dict:
        return self.call("epa.debug.interrupt", {"kernel_id": kernel_id})

    # Inspection

    def snapshot(self, kernel_id: int, path_id: str = "") -> dict:
        params: dict = {"kernel_id": kernel_id}
        if path_id:
            params["path_id"] = path_id
        return self.call("epa.debug.snapshot", params)

    def inspect_worker(
        self,
        wid: int,
        path_id: str = "",
        stack_words: int = 32,
        arena_bytes: int = 128,
        ghs_bytes: int = 128,
    ) -> dict:
        params: dict = {
            "wid": wid,
            "stack_words": stack_words,
            "arena_bytes": arena_bytes,
            "ghs_bytes": ghs_bytes,
        }
        if path_id:
            params["path_id"] = path_id
        return self.call("epa.debug.inspectWorker", params)

    def set_worker_debug(self, wid: int, enabled: bool, path_id: str = "") -> dict:
        params: dict = {"wid": int(wid), "enabled": bool(enabled)}
        if path_id:
            params["path_id"] = path_id
        return self.call("epa.debug.setWorkerDebug", params)

    def get_worker_debug(self, wid: int, path_id: str = "") -> dict:
        params: dict = {"wid": int(wid)}
        if path_id:
            params["path_id"] = path_id
        return self.call("epa.debug.getWorkerDebug", params)

    def events(self, kernel_id: int, clear: bool = False) -> list:
        return self.call("epa.debug.events",
                         {"kernel_id": kernel_id, "clear": clear})

    # Breakpoints

    def breakpoint_add(self, kernel_id: int, addr: int) -> dict:
        return self.call("epa.debug.breakpointAdd",
                         {"kernel_id": kernel_id, "addr": addr})

    def breakpoint_clear(self, kernel_id: int, addr: int | None = None) -> dict:
        params: dict = {"kernel_id": kernel_id}
        if addr is not None:
            params["addr"] = addr
        return self.call("epa.debug.breakpointClear", params)

    def breakpoint_clear_all(self, kernel_id: int = 0) -> dict:
        return self.call("epa.debug.breakpointClear", {"kernel_id": kernel_id})

    def breakpoint_list(self, kernel_id: int) -> list:
        return self.call("epa.debug.breakpointList", {"kernel_id": kernel_id})


# ------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------

def _recv_exactly(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        try:
            chunk = sock.recv(n - len(buf))
        except BlockingIOError:
            select.select([sock], [], [], 0.05)
            continue
        if not chunk:
            raise EOFError("connection closed mid-message")
        buf.extend(chunk)
    return bytes(buf)
