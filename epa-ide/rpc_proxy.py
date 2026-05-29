#!/usr/bin/env python3
"""
RPC message-timing proxy between EPA-IDE and elaraui-server.

Usage:
  1. Start elaraui-server on a different port:
       build/bin/elaraui-server --port 18780 --persistent
  2. Run this proxy (default: listen 18777, forward to 18780):
       python3 rpc_proxy.py [listen_port] [server_port]
  3. Start EPA-IDE normally (connects to 18777 -> proxy -> server)

The proxy decodes every framed JSON message in both directions and writes a
tab-separated log to rpc_proxy.log with:
  - monotonic timestamp (ms from first message)
  - delta from previous message on the same channel (ms)
  - direction  PY->SRV or SRV->PY
  - message kind: REQUEST / RESPONSE / NOTIFY / ?
  - method / id summary
  - payload byte length

Look for:
  - Large deltas on PY->SRV: Python is slow to send (thread contention, GIL)
  - Large deltas on SRV->PY: server is slow to respond (GTK tick, rendering)
  - Regular intervals on either side: fixed-rate polling or timer
"""

import json
import socket
import struct
import sys
import threading
import time
from pathlib import Path

LISTEN_HOST = "127.0.0.1"
LISTEN_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18777
SERVER_HOST  = "127.0.0.1"
SERVER_PORT  = int(sys.argv[2]) if len(sys.argv) > 2 else 18780
LOG_PATH     = Path(__file__).parent / "rpc_proxy.log"

_start_ts    = None
_log_lock    = threading.Lock()
_last_py2srv = None   # monotonic ms of last PY->SRV message
_last_srv2py = None   # monotonic ms of last SRV->PY message
_log_fh      = None


def _ts_ms() -> float:
    global _start_ts
    now = time.monotonic() * 1000.0
    if _start_ts is None:
        _start_ts = now
    return now - _start_ts


def _classify(payload: bytes) -> tuple[str, str]:
    """Return (kind, summary) for a decoded JSON payload."""
    try:
        obj = json.loads(payload)
    except Exception:
        return "?", f"<unparseable {len(payload)}b>"

    has_id     = "id"     in obj
    has_ok     = "ok"     in obj
    has_method = "method" in obj

    if has_ok and has_id:
        ok   = obj["ok"]
        rid  = obj.get("id", "?")
        if ok:
            result = obj.get("result")
            brief = json.dumps(result, separators=(",", ":"))[:60] if result is not None else "null"
            return "RESPONSE", f"id={rid} ok result={brief}"
        else:
            err = obj.get("error") or {}
            return "RESPONSE", f"id={rid} ERR {err.get('code','?')}: {err.get('message','')[:60]}"

    if has_method and has_id:
        return "REQUEST", f"id={obj['id']} {obj['method']} {_params_brief(obj)}"

    if has_method and not has_id:
        return "NOTIFY", f"{obj['method']} {_params_brief(obj)}"

    return "?", str(obj)[:80]


def _params_brief(obj: dict) -> str:
    p = obj.get("params")
    if p is None:
        return ""
    s = json.dumps(p, separators=(",", ":"))
    return s[:80] if len(s) <= 80 else s[:77] + "..."


def _log(direction: str, payload: bytes):
    global _last_py2srv, _last_srv2py
    kind, summary = _classify(payload)
    ts = _ts_ms()

    with _log_lock:
        if direction == "PY->SRV":
            delta = ts - _last_py2srv if _last_py2srv is not None else 0.0
            _last_py2srv = ts
        else:
            delta = ts - _last_srv2py if _last_srv2py is not None else 0.0
            _last_srv2py = ts

        line = f"{ts:12.2f}\t{delta:8.2f}\t{direction}\t{kind:<8}\t{len(payload):6}\t{summary}\n"
        print(line, end="", flush=True)
        if _log_fh:
            _log_fh.write(line)
            _log_fh.flush()


def _recv_exact(sock: socket.socket, n: int) -> bytes | None:
    buf = bytearray()
    while len(buf) < n:
        try:
            chunk = sock.recv(n - len(buf))
        except OSError:
            return None
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


def _send_exact(sock: socket.socket, data: bytes) -> bool:
    try:
        sock.sendall(data)
        return True
    except OSError:
        return False


def _forward(src: socket.socket, dst: socket.socket, direction: str, stop: threading.Event):
    """Read length-prefixed frames from src, log them, forward to dst."""
    while not stop.is_set():
        prefix = _recv_exact(src, 4)
        if prefix is None:
            break
        length = struct.unpack(">I", prefix)[0]
        body = _recv_exact(src, length)
        if body is None:
            break
        _log(direction, body)
        frame = prefix + body
        if not _send_exact(dst, frame):
            break
    stop.set()


def handle_client(client_sock: socket.socket, client_addr):
    print(f"[proxy] client connected from {client_addr}", flush=True)
    try:
        srv_sock = socket.create_connection((SERVER_HOST, SERVER_PORT))
    except OSError as e:
        print(f"[proxy] cannot connect to server {SERVER_HOST}:{SERVER_PORT}: {e}", flush=True)
        client_sock.close()
        return

    print(f"[proxy] connected to server {SERVER_HOST}:{SERVER_PORT}", flush=True)
    stop = threading.Event()

    t1 = threading.Thread(target=_forward, args=(client_sock, srv_sock, "PY->SRV", stop), daemon=True)
    t2 = threading.Thread(target=_forward, args=(srv_sock, client_sock, "SRV->PY", stop), daemon=True)
    t1.start()
    t2.start()
    stop.wait()

    client_sock.close()
    srv_sock.close()
    print(f"[proxy] session ended", flush=True)


def main():
    global _log_fh
    _log_fh = open(LOG_PATH, "w")
    header = f"# ts_ms\tdelta_ms\tdirection\tkind\tbytes\tsummary\n"
    _log_fh.write(header)
    print(header, end="", flush=True)

    listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listen_sock.bind((LISTEN_HOST, LISTEN_PORT))
    listen_sock.listen(1)
    print(f"[proxy] listening on {LISTEN_HOST}:{LISTEN_PORT} -> {SERVER_HOST}:{SERVER_PORT}", flush=True)
    print(f"[proxy] logging to {LOG_PATH}", flush=True)

    while True:
        try:
            client_sock, client_addr = listen_sock.accept()
        except KeyboardInterrupt:
            break
        threading.Thread(target=handle_client, args=(client_sock, client_addr), daemon=True).start()

    listen_sock.close()
    if _log_fh:
        _log_fh.close()


if __name__ == "__main__":
    main()
