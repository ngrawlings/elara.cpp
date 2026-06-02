#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import socket
import sys
import time


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
        chunks = []
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


def smoke_document() -> str:
    return json.dumps(
        {
            "elara_ui_protocol": 1,
            "window": {
                "title": "Vulkan Smoke",
                "width": 960,
                "height": 640,
                "backend_id": "org.elara.ui.vulkan-smoke",
                "use_system_header": False,
            },
            "root": {"content": "app.surface"},
            "widgets": [
                {
                    "id": "app.surface",
                    "type": "elara.widgets.vulkan_surface",
                    "properties": {
                        "virtual_width": 960,
                        "virtual_height": 640,
                        "overlay_text": "Common runtime smoke",
                    },
                }
            ],
        }
    )


def checker_texture() -> dict:
    width = 8
    height = 8
    rgb = []
    for y in range(height):
        for x in range(width):
            if x == 0 or y == 0 or x == width - 1 or y == height - 1:
                rgb.extend([0.08, 0.08, 0.08])
            elif x < width // 2 and y < height // 2:
                rgb.extend([0.92, 0.28, 0.20])
            elif x >= width // 2 and y < height // 2:
                rgb.extend([0.24, 0.82, 0.34])
            elif x < width // 2 and y >= height // 2:
                rgb.extend([0.22, 0.48, 0.94])
            else:
                rgb.extend([0.96, 0.88, 0.22])
    return {"width": width, "height": height, "rgb": rgb}


def textured_cube_commands() -> list[dict]:
    # Two visible faces from the proven proof path, projected already.
    return [
        {"op": "clear", "r": 0.10, "g": 0.11, "b": 0.14},
        {"op": "textured_triangle", "x0": 571, "y0": 267, "x1": 662, "y1": 253, "x2": 675, "y2": 317, "depth": 0.41, "u0": 0.0, "v0": 1.0, "u1": 1.0, "v1": 1.0, "u2": 1.0, "v2": 0.0},
        {"op": "textured_triangle", "x0": 571, "y0": 267, "x1": 675, "y1": 317, "x2": 584, "y2": 332, "depth": 0.41, "u0": 0.0, "v0": 1.0, "u1": 1.0, "v1": 0.0, "u2": 0.0, "v2": 0.0},
        {"op": "textured_triangle", "x0": 584, "y0": 332, "x1": 675, "y1": 317, "x2": 689, "y2": 395, "depth": 0.52, "u0": 0.0, "v0": 1.0, "u1": 1.0, "v1": 1.0, "u2": 1.0, "v2": 0.0},
        {"op": "textured_triangle", "x0": 584, "y0": 332, "x1": 689, "y1": 395, "x2": 599, "y2": 409, "depth": 0.52, "u0": 0.0, "v0": 1.0, "u1": 1.0, "v1": 0.0, "u2": 0.0, "v2": 0.0},
        {"op": "text", "x": 24, "y": 28, "text": "Vulkan textured cube smoke", "size": 18, "r": 0.95, "g": 0.96, "b": 0.98},
    ]


def find_widget(snapshot: dict, widget_id: str) -> dict | None:
    stack = []
    for key in ("content_tree", "popup_tree"):
        if isinstance(snapshot.get(key), dict):
            stack.append(snapshot[key])
    while stack:
        node = stack.pop()
        if node.get("id") == widget_id:
            return node
        for child in node.get("children", []) or []:
            if isinstance(child, dict):
                stack.append(child)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Live common Vulkan widget smoke test")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18810)
    parser.add_argument("--widget", default="app.surface")
    args = parser.parse_args()

    client = BrpcJsonClient(args.host, args.port)
    try:
        print(client.call("loadDocument", {"document": smoke_document()}))
        print(client.call("setSectionJson", {"target": args.widget, "section": "texture", "value": checker_texture()}))
        print(client.call("setSectionJson", {"target": args.widget, "section": "commands", "value": textured_cube_commands()}))
        time.sleep(0.35)
        snapshot = client.call("snapshot", {})
        widget = find_widget(snapshot, args.widget)
        print(json.dumps({"widget_found": widget is not None, "widget": widget}, indent=2))
        return 0 if widget is not None else 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
