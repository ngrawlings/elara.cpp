"""Elara UI snapshot dumping helpers.

This module asks the running Elara UI RPC head for a complete runtime snapshot
and writes it to disk as JSON. The snapshot is intentionally runtime-state based:
bounds, absolute bounds, focus, popup stack, sparse widget state, and recursive
children are preserved exactly as reported by ui.snapshot/ui.snapshotWidget.

List/tree item payloads are handled in two ways:
1. If the UI head includes them in the widget snapshot state, they are preserved.
2. If the Python builder/runtime registered static sections for a widget id, those
   sections are merged under `client_sections` so list items, tree nodes, menu
   items, toolbar items, chart data, etc. are visible even when the C++ sparse
   runtime snapshot only reports itemCount/selectedId.
"""

from __future__ import annotations

import datetime as _dt
import json
from pathlib import Path
from typing import Any, Dict, Iterable, Mapping, Optional, Set

from .rpc import ElaraUiRpcClient


class UiSnapshotDumper:
    def __init__(self, client: ElaraUiRpcClient, client_sections: Optional[Mapping[str, Mapping[str, Any]]] = None):
        self.client = client
        self.client_sections: Dict[str, Mapping[str, Any]] = dict(client_sections or {})

    def snapshot(self, include_client_sections: bool = True, timeout: float = 5.0) -> Dict[str, Any]:
        raw = self.client.snapshot(timeout=timeout)
        if raw is None:
            raw = {}
        if not isinstance(raw, dict):
            raw = {"raw": raw}

        result: Dict[str, Any] = {
            "schema": "elara.ui.snapshot.v1",
            "captured_at_utc": _dt.datetime.now(_dt.timezone.utc).isoformat(),
            "source": "ui.snapshot",
            "snapshot": raw,
        }

        if include_client_sections:
            self._merge_client_sections(result["snapshot"])
            result["client_section_widget_count"] = len(self.client_sections)

        return result

    def snapshot_widget(self, target: str, include_client_sections: bool = True, timeout: float = 5.0) -> Dict[str, Any]:
        raw = self.client.snapshot_widget(target, timeout=timeout)
        if raw is None:
            raw = {}
        if not isinstance(raw, dict):
            raw = {"raw": raw}

        result: Dict[str, Any] = {
            "schema": "elara.ui.widget_snapshot.v1",
            "captured_at_utc": _dt.datetime.now(_dt.timezone.utc).isoformat(),
            "source": "ui.snapshotWidget",
            "target": target,
            "snapshot": raw,
        }

        if include_client_sections:
            self._merge_client_sections(result["snapshot"])
            result["client_section_widget_count"] = len(self.client_sections)

        return result

    def dump(self, output_path: str | Path, include_client_sections: bool = True, indent: int = 2, timeout: float = 5.0) -> Path:
        path = Path(output_path).expanduser().resolve()
        path.parent.mkdir(parents=True, exist_ok=True)
        data = self.snapshot(include_client_sections=include_client_sections, timeout=timeout)
        path.write_text(json.dumps(data, indent=indent, ensure_ascii=False), encoding="utf-8")
        return path

    def dump_widget(self, target: str, output_path: str | Path, include_client_sections: bool = True, indent: int = 2, timeout: float = 5.0) -> Path:
        path = Path(output_path).expanduser().resolve()
        path.parent.mkdir(parents=True, exist_ok=True)
        data = self.snapshot_widget(target, include_client_sections=include_client_sections, timeout=timeout)
        path.write_text(json.dumps(data, indent=indent, ensure_ascii=False), encoding="utf-8")
        return path

    def _merge_client_sections(self, node_or_root: Any) -> None:
        for widget in self._iter_snapshot_widgets(node_or_root):
            wid = widget.get("id")
            if not wid or wid not in self.client_sections:
                continue
            existing = widget.get("client_sections")
            if not isinstance(existing, dict):
                existing = {}
                widget["client_sections"] = existing
            for key, value in self.client_sections[wid].items():
                existing[key] = value

    def _iter_snapshot_widgets(self, node_or_root: Any) -> Iterable[Dict[str, Any]]:
        seen: Set[int] = set()

        def walk(value: Any):
            if isinstance(value, dict):
                marker = id(value)
                if marker in seen:
                    return
                seen.add(marker)

                if "id" in value and ("type" in value or "state" in value or "children" in value):
                    yield value

                for key in ("content", "popup"):
                    child = value.get(key)
                    if child is not None:
                        yield from walk(child)

                for key in ("popups", "children", "tabs", "items", "nodes"):
                    children = value.get(key)
                    if isinstance(children, list):
                        for child in children:
                            yield from walk(child)
                    elif isinstance(children, dict):
                        yield from walk(children)

                # Generic recursive fallback for future snapshot shapes.
                for key, child in value.items():
                    if key in ("content", "popup", "popups", "children", "tabs", "items", "nodes"):
                        continue
                    if isinstance(child, (dict, list)):
                        yield from walk(child)
            elif isinstance(value, list):
                for item in value:
                    yield from walk(item)

        yield from walk(node_or_root)


def dump_ui_snapshot(client: ElaraUiRpcClient, output_path: str | Path, client_sections: Optional[Mapping[str, Mapping[str, Any]]] = None) -> Path:
    return UiSnapshotDumper(client, client_sections=client_sections).dump(output_path)
