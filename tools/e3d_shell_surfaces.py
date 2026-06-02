#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def _load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _save(path: Path, doc: dict[str, Any]) -> None:
    path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")


def _resolve_ref(doc: dict[str, Any], ref: str) -> list[list[float]]:
    current: Any = doc
    for part in ref.split("."):
        if not isinstance(current, dict):
            return []
        current = current.get(part)
    if not isinstance(current, list):
        return []
    out: list[list[float]] = []
    for point in current:
        if isinstance(point, list) and len(point) >= 2:
            out.append([float(point[0]), float(point[1])])
    return out


def _set_ref(doc: dict[str, Any], ref: str, value: list[list[float]]) -> None:
    parts = ref.split(".")
    current: Any = doc
    for part in parts[:-1]:
        if not isinstance(current, dict):
            raise ValueError(f"cannot write through non-object at '{part}'")
        current = current.setdefault(part, {})
    if not isinstance(current, dict):
        raise ValueError(f"cannot write ref '{ref}'")
    current[parts[-1]] = value


def _area(points: list[list[float]]) -> float:
    total = 0.0
    for i, point in enumerate(points):
        nxt = points[(i + 1) % len(points)]
        total += point[0] * nxt[1] - nxt[0] * point[1]
    return total * 0.5


def _normalized(points: list[list[float]], want_ccw: bool) -> list[list[float]]:
    if len(points) < 3:
        return points
    is_ccw = _area(points) > 0.0
    if is_ccw == want_ccw:
        return points
    return list(reversed(points))


def _edge_normal(point: list[float], nxt: list[float]) -> list[float]:
    dx = nxt[0] - point[0]
    dy = nxt[1] - point[1]
    length = (dx * dx + dy * dy) ** 0.5
    if length <= 1e-9:
        return [0.0, 0.0, 0.0]
    return [round(dy / length, 6), round(-dx / length, 6), 0.0]


def _center(points: list[list[float]]) -> list[float]:
    if not points:
        return [0.0, 0.0]
    return [
        sum(point[0] for point in points) / float(len(points)),
        sum(point[1] for point in points) / float(len(points)),
    ]


def _normal_from_center_rule(point: list[float], nxt: list[float], center: list[float], toward_center: bool) -> list[float]:
    normal = _edge_normal(point, nxt)
    midpoint = [(point[0] + nxt[0]) * 0.5, (point[1] + nxt[1]) * 0.5]
    away = [midpoint[0] - center[0], midpoint[1] - center[1]]
    dot = normal[0] * away[0] + normal[1] * away[1]
    if toward_center:
        if dot > 0.0:
            normal = [-normal[0], -normal[1], normal[2]]
    elif dot < 0.0:
        normal = [-normal[0], -normal[1], normal[2]]
    return [round(normal[0], 6), round(normal[1], 6), round(normal[2], 6)]


def _axis_name(normal: list[float], inner: bool) -> str:
    x = normal[0]
    y = normal[1]
    if abs(x) >= abs(y):
        if inner:
            return "left_jamb" if x > 0.0 else "right_jamb"
        return "right_outer" if x > 0.0 else "left_outer"
    if inner:
        return "bottom_jamb" if y > 0.0 else "top_jamb"
    return "top_outer" if y > 0.0 else "bottom_outer"


def _surface_record(point: list[float], nxt: list[float], normal: list[float], inner: bool) -> dict[str, Any]:
    return {
        "edge": [point, nxt],
        "normal": normal,
        "normal_rule": "toward_cutout_center" if inner else "away_from_outer_center",
        "tags": ["inner" if inner else "outer", "toward_center" if inner else "away_from_center"],
    }


def _ref_label(ref: str) -> str:
    label = ref.split(".")[-1]
    for suffix in ("_cutout", "_outline", "_outer", "_inner"):
        if label.endswith(suffix):
            label = label[: -len(suffix)]
    return label


def build_surfaces(doc: dict[str, Any], component: dict[str, Any], write_refs: bool) -> dict[str, Any]:
    points_ref = component.get("points_ref")
    if not isinstance(points_ref, str):
        raise ValueError("component has no points_ref")
    outer = _normalized(_resolve_ref(doc, points_ref), want_ccw=True)
    outer_center = _center(outer)
    if write_refs:
        _set_ref(doc, points_ref, outer)

    outer_surfaces: dict[str, Any] = {}
    for i, point in enumerate(outer):
        nxt = outer[(i + 1) % len(outer)]
        normal = _normal_from_center_rule(point, nxt, outer_center, toward_center=False)
        name = _axis_name(normal, inner=False)
        outer_surfaces[name] = _surface_record(point, nxt, normal, inner=False)

    inner_surfaces: dict[str, Any] = {}
    cutout_refs = component.get("cutout_refs") if isinstance(component.get("cutout_refs"), list) else []
    for ref in cutout_refs:
        if not isinstance(ref, str):
            continue
        loop = _normalized(_resolve_ref(doc, ref), want_ccw=False)
        cutout_center = _center(loop)
        if write_refs:
            _set_ref(doc, ref, loop)
        label = _ref_label(ref)
        multi_cutout = len(cutout_refs) > 1
        for i, point in enumerate(loop):
            nxt = loop[(i + 1) % len(loop)]
            normal = _normal_from_center_rule(point, nxt, cutout_center, toward_center=True)
            name = _axis_name(normal, inner=True)
            if multi_cutout:
                name = f"{label}_{name}"
            inner_surfaces[name] = _surface_record(point, nxt, normal, inner=True)

    return {
        "surface_generation": {
            "tool": "tools/e3d_shell_surfaces.py",
            "outer_join_surfaces": "away_from_outer_center",
            "cutout_join_surfaces": "toward_each_cutout_center",
        },
        "outer_surfaces": outer_surfaces,
        "inner_surfaces": inner_surfaces,
    }


def _normalized_json(value: Any) -> Any:
    if isinstance(value, dict):
        return {key: _normalized_json(value[key]) for key in sorted(value.keys())}
    if isinstance(value, list):
        return [_normalized_json(item) for item in value]
    if isinstance(value, float):
        if value == 0.0:
            return 0.0
        return round(value, 6)
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description="Normalize E3D wall-shell loops and generate side-surface normals")
    parser.add_argument("artifact", help="Path to .e3d.json artifact")
    parser.add_argument("--component", required=True, help="Component id to process")
    parser.add_argument("--write", action="store_true", help="Write normalized loops and generated surface declarations")
    parser.add_argument("--check", action="store_true", help="Fail if stored surfaces differ from generated surfaces")
    args = parser.parse_args()

    path = Path(args.artifact)
    doc = _load(path)
    components = doc.get("components")
    if not isinstance(components, dict):
        raise SystemExit("artifact has no components object")
    component = components.get(args.component)
    if not isinstance(component, dict):
        raise SystemExit(f"component '{args.component}' not found")
    if component.get("primitive") != "wall_shell_with_cutouts":
        raise SystemExit(f"component '{args.component}' is not wall_shell_with_cutouts")

    surfaces = build_surfaces(doc, component, write_refs=args.write)
    matches = True
    if args.check:
        expected = _normalized_json(surfaces)
        actual = _normalized_json({
            "surface_generation": component.get("surface_generation") or {},
            "outer_surfaces": component.get("outer_surfaces") or {},
            "inner_surfaces": component.get("inner_surfaces") or {},
        })
        matches = actual == expected
        if not matches:
            print(json.dumps({
                "artifact": str(path),
                "component": args.component,
                "ok": False,
                "message": "stored shell surfaces differ from generated surfaces",
                "expected": expected,
                "actual": actual,
            }, indent=2), file=sys.stderr)
            return 1
    if args.write:
        component["surface_generation"] = surfaces["surface_generation"]
        component["outer_surfaces"] = surfaces["outer_surfaces"]
        component["inner_surfaces"] = surfaces["inner_surfaces"]
        _save(path, doc)

    print(json.dumps({
        "artifact": str(path),
        "component": args.component,
        "surface_generation": surfaces["surface_generation"],
        "outer_surfaces": surfaces["outer_surfaces"],
        "inner_surfaces": surfaces["inner_surfaces"],
        "written": bool(args.write),
        "ok": matches,
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
