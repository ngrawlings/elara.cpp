#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
EPA_IDE_DIR = REPO_ROOT / "epa-ide"
if str(EPA_IDE_DIR) not in sys.path:
    sys.path.insert(0, str(EPA_IDE_DIR))

from e3d_runtime import WIDTH, HEIGHT, build_runtime_preview, load_e3d_document


class BrpcJsonClient:
    def __init__(self, host: str, port: int, timeout: float = 5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self._next_id = 1

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def _send_frame(self, payload: bytes) -> None:
        header = b"BRPC" + len(payload).to_bytes(4, "little")
        self.sock.sendall(header + payload)

    def _recv_exact(self, size: int) -> bytes:
        chunks: list[bytes] = []
        remaining = size
        while remaining > 0:
            chunk = self.sock.recv(remaining)
            if not chunk:
                raise RuntimeError("socket closed while receiving frame")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def _recv_frame(self) -> bytes:
        header = self._recv_exact(8)
        if header[:4] != b"BRPC":
            raise RuntimeError(f"unexpected frame header: {header[:4]!r}")
        size = int.from_bytes(header[4:8], "little")
        return self._recv_exact(size)

    def call(self, method: str, params: dict) -> dict:
        request = {
            "jsonrpc": "2.0",
            "id": self._next_id,
            "method": method,
            "params": params,
        }
        self._next_id += 1
        self._send_frame(json.dumps(request).encode("utf-8"))
        while True:
            response = json.loads(self._recv_frame().decode("utf-8"))
            if response.get("id") != request["id"]:
                continue
            if "error" in response:
                raise RuntimeError(json.dumps(response["error"]))
            return response["result"]


def smoke_document(title: str) -> str:
    return json.dumps(
        {
            "elara_ui_protocol": 1,
            "window": {
                "title": title,
                "width": int(WIDTH),
                "height": int(HEIGHT),
                "backend_id": "org.elara.ui.e3d-runtime-smoke",
                "use_system_header": False,
            },
            "root": {"content": "app.surface"},
            "widgets": [
                {
                    "id": "app.surface",
                    "type": "elara.widgets.vulkan_surface",
                    "properties": {
                        "virtual_width": int(WIDTH),
                        "virtual_height": int(HEIGHT),
                        "overlay_text": title,
                    },
                }
            ],
        }
    )

def main() -> int:
    parser = argparse.ArgumentParser(description="Runtime smoke test for E3D pre-mesh format")
    parser.add_argument("artifact", help="Path to .e3d.json artifact")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18810)
    parser.add_argument("--widget", default="app.surface")
    parser.add_argument("--dump-only", action="store_true")
    parser.add_argument("--dump-commands", action="store_true")
    args = parser.parse_args()

    path = Path(args.artifact)
    doc = load_e3d_document(path)
    commands, errors = build_runtime_preview(doc)

    if args.dump_commands:
        print(json.dumps({"commands": commands, "errors": errors}, indent=2))
    if args.dump_only:
        print(json.dumps({"artifact": str(path), "errors": errors, "command_count": len(commands)}, indent=2))
        return 1 if errors else 0

    client = BrpcJsonClient(args.host, args.port)
    try:
        title = f"E3D Runtime Smoke: {path.name}"
        print(client.call("loadDocument", {"document": smoke_document(title)}))
        print(client.call("setSectionJson", {"target": args.widget, "section": "commands", "value": commands}))
        time.sleep(0.25)
        print(json.dumps({"artifact": str(path), "errors": errors, "command_count": len(commands)}, indent=2))
        return 1 if errors else 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
