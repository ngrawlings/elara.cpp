#!/usr/bin/env python3
"""
Persistent AI RPC client for EPA-IDE.

Keeps one TCP connection open and supports multiple requests over that
connection. Intended for long-lived debugger sessions where reconnecting for
every request is undesirable.

Usage:
  python3 ai_rpc_client.py --port 18792

Interactive forms:
  hello
  ping
  get_project
  open_project {"path":"/abs/project"}
  ui_call {"method":"snapshotWidget","params":{"target":"nav.debug_panel"}}
  {"method":"ping","params":{}}

Commands:
  :quit
  :help
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import threading
import time


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 18792
DEFAULT_KEEPALIVE_SECONDS = 15.0


def _enable_socket_keepalive(sock: socket.socket) -> None:
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    except OSError:
        return
    for opt_name, value in (
        ("TCP_KEEPIDLE", 30),
        ("TCP_KEEPINTVL", 10),
        ("TCP_KEEPCNT", 3),
    ):
        opt = getattr(socket, opt_name, None)
        if opt is None:
            continue
        try:
            sock.setsockopt(socket.IPPROTO_TCP, opt, value)
        except OSError:
            pass


class AiRpcClient:
    def __init__(self, host: str, port: int, keepalive_seconds: float):
        self.host = host
        self.port = port
        self.keepalive_seconds = max(5.0, keepalive_seconds)
        self.sock: socket.socket | None = None
        self.file = None
        self._lock = threading.Lock()
        self._running = False
        self._last_io = time.monotonic()
        self._ping_thread: threading.Thread | None = None

    def connect(self) -> None:
        self.sock = socket.create_connection((self.host, self.port), timeout=5.0)
        _enable_socket_keepalive(self.sock)
        self.sock.settimeout(None)
        self.file = self.sock.makefile("rwb")
        self._running = True
        self._ping_thread = threading.Thread(target=self._keepalive_loop, daemon=True)
        self._ping_thread.start()

    def close(self) -> None:
        self._running = False
        try:
            if self.file is not None:
                self.file.close()
        except Exception:
            pass
        try:
            if self.sock is not None:
                self.sock.close()
        except Exception:
            pass
        self.file = None
        self.sock = None

    def call(self, method: str, params: dict | None = None) -> dict:
        if self.file is None:
            raise RuntimeError("not connected")
        req = {"method": method, "params": params or {}}
        payload = (json.dumps(req, ensure_ascii=False) + "\n").encode("utf-8")
        with self._lock:
            self.file.write(payload)
            self.file.flush()
            line = self.file.readline()
            self._last_io = time.monotonic()
        if not line:
            raise RuntimeError("connection closed by peer")
        return json.loads(line.decode("utf-8"))

    def _keepalive_loop(self) -> None:
        while self._running:
            time.sleep(1.0)
            if time.monotonic() - self._last_io < self.keepalive_seconds:
                continue
            try:
                self.call("ping", {})
            except Exception:
                self._running = False
                return


def _parse_line(text: str) -> tuple[str, dict]:
    text = text.strip()
    if not text:
        raise ValueError("empty input")
    if text.startswith("{"):
        obj = json.loads(text)
        method = obj.get("method")
        if not method:
            raise ValueError("raw JSON request must include 'method'")
        params = obj.get("params") or {}
        if not isinstance(params, dict):
            raise ValueError("'params' must be an object")
        return str(method), params
    parts = text.split(None, 1)
    method = parts[0]
    params = {}
    if len(parts) > 1:
        params = json.loads(parts[1])
        if not isinstance(params, dict):
            raise ValueError("params JSON must be an object")
    return method, params


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--keepalive-seconds", type=float, default=DEFAULT_KEEPALIVE_SECONDS)
    args = ap.parse_args()

    client = AiRpcClient(args.host, args.port, args.keepalive_seconds)
    client.connect()
    hello = client.call("hello", {})
    print(json.dumps(hello, ensure_ascii=False))
    print(":help for commands", flush=True)

    try:
        for raw in sys.stdin:
            line = raw.strip()
            if not line:
                continue
            if line == ":quit":
                break
            if line == ":help":
                print(":quit", flush=True)
                print(":help", flush=True)
                print("method", flush=True)
                print("method {\"param\":1}", flush=True)
                print("{\"method\":\"ping\",\"params\":{}}", flush=True)
                continue
            try:
                method, params = _parse_line(line)
                print(json.dumps(client.call(method, params), ensure_ascii=False), flush=True)
            except Exception as exc:
                print(json.dumps({"ok": False, "error": str(exc)}), flush=True)
    finally:
        client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
