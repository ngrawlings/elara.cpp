from __future__ import annotations

import json
import math
from pathlib import Path


WIDTH = 1180.0
HEIGHT = 820.0
VIEW_CUBE_CELL = 34.0
VIEW_CUBE_MARGIN = 18.0
VIEW_CUBE_HIT_PAD = 6.0

_VIEW_LABELS = {
    "top": "Top",
    "bottom": "Bottom",
    "front": "Front",
    "back": "Back",
    "left": "Left",
    "right": "Right",
}

_VIEW_ROTATION = {
    "front": {"left": "left", "right": "right", "up": "top", "down": "bottom"},
    "back": {"left": "right", "right": "left", "up": "top", "down": "bottom"},
    "left": {"left": "back", "right": "front", "up": "top", "down": "bottom"},
    "right": {"left": "front", "right": "back", "up": "top", "down": "bottom"},
    "top": {"left": "left", "right": "right", "up": "back", "down": "front"},
    "bottom": {"left": "left", "right": "right", "up": "front", "down": "back"},
}


def load_level_path_document(path: Path) -> dict:
    doc = json.loads(path.read_text(encoding="utf-8"))
    if isinstance(doc, dict):
        doc["__source_path"] = str(path.resolve())
    return doc


def _color(kind: str) -> tuple[float, float, float]:
    if kind == "square":
        return (0.34, 0.68, 0.40)
    if kind == "cube":
        return (0.42, 0.52, 0.86)
    return (0.88, 0.78, 0.28)


def _tool_label(tool_id: str) -> str:
    return {
        "square": "Open Space",
        "cube": "Closed Space",
        "line": "Connecting Corridor",
        "select": "Select",
    }.get(tool_id, tool_id.replace("_", " ").title())


def _edge(color: tuple[float, float, float]) -> tuple[float, float, float]:
    return (
        max(color[0] * 0.55, 0.08),
        max(color[1] * 0.55, 0.08),
        max(color[2] * 0.55, 0.08),
    )


def _editor_view_state(doc: dict, editor_state: dict | None = None) -> tuple[str, float, list[float]]:
    editor_state = editor_state or {}
    view = str(editor_state.get("view", "top") or "top")
    zoom = float(editor_state.get("zoom", 52.0) or 52.0)
    center = editor_state.get("center") if isinstance(editor_state.get("center"), list) else None
    if not center:
        stored = doc.get("editor_view") if isinstance(doc.get("editor_view"), dict) else {}
        center = stored.get("center") if isinstance(stored.get("center"), list) else [0.0, 0.0]
        zoom = float(editor_state.get("zoom", stored.get("scale", 52.0) or 52.0) or 52.0)
    cx = float(center[0] if len(center) > 0 else 0.0)
    cy = float(center[1] if len(center) > 1 else 0.0)
    return view, zoom, [cx, cy]


def _world_to_plane(view: str, world: list[float] | tuple[float, float, float]) -> tuple[float, float]:
    x = float(world[0] if len(world) > 0 else 0.0)
    y = float(world[1] if len(world) > 1 else 0.0)
    z = float(world[2] if len(world) > 2 else 0.0)
    if view == "front":
        return (x, y)
    if view == "back":
        return (x, y)
    if view == "left":
        return (z, y)
    if view == "right":
        return (z, y)
    if view == "bottom":
        return (x, z)
    return (x, z)


def _plane_to_world(view: str, u: float, v: float, anchor: list[float] | tuple[float, float, float] | None = None) -> tuple[float, float, float]:
    ax = float(anchor[0] if anchor and len(anchor) > 0 else 0.0)
    ay = float(anchor[1] if anchor and len(anchor) > 1 else 0.0)
    az = float(anchor[2] if anchor and len(anchor) > 2 else 0.0)
    if view == "front":
        return (u, v, az)
    if view == "back":
        return (u, v, az)
    if view == "left":
        return (ax, v, u)
    if view == "right":
        return (ax, v, u)
    if view == "bottom":
        return (u, ay, v)
    return (u, ay, v)


def _projector(doc: dict, editor_state: dict | None = None):
    _, scale, center = _editor_view_state(doc, editor_state)
    cu = center[0]
    cv = center[1]

    def project(u: float, v: float) -> tuple[float, float]:
        return (WIDTH * 0.50 + (u - cu) * scale, HEIGHT * 0.55 - (v - cv) * scale)

    return project


def _unproject(doc: dict, screen_x: float, screen_y: float, editor_state: dict | None = None) -> tuple[float, float]:
    _, scale, center = _editor_view_state(doc, editor_state)
    cu = center[0]
    cv = center[1]
    world_u = ((screen_x - WIDTH * 0.50) / scale) + cu
    world_v = (-(screen_y - HEIGHT * 0.55) / scale) + cv
    return (world_u, world_v)


def _rect_commands(
    x0: float,
    y0: float,
    x1: float,
    y1: float,
    fill: tuple[float, float, float],
    edge: tuple[float, float, float],
    depth: float = 0.3,
) -> list[dict]:
    return [
        {"op": "triangle", "x0": x0, "y0": y0, "x1": x1, "y1": y0, "x2": x1, "y2": y1, "depth": depth, "r": fill[0], "g": fill[1], "b": fill[2]},
        {"op": "triangle", "x0": x0, "y0": y0, "x1": x1, "y1": y1, "x2": x0, "y2": y1, "depth": depth, "r": fill[0], "g": fill[1], "b": fill[2]},
        {"op": "line", "x0": x0, "y0": y0, "x1": x1, "y1": y0, "r": edge[0], "g": edge[1], "b": edge[2]},
        {"op": "line", "x0": x1, "y0": y0, "x1": x1, "y1": y1, "r": edge[0], "g": edge[1], "b": edge[2]},
        {"op": "line", "x0": x1, "y0": y1, "x1": x0, "y1": y1, "r": edge[0], "g": edge[1], "b": edge[2]},
        {"op": "line", "x0": x0, "y0": y1, "x1": x0, "y1": y0, "r": edge[0], "g": edge[1], "b": edge[2]},
    ]


def _screen_rect_from_center(project, u: float, v: float, su: float, sv: float) -> tuple[float, float, float, float]:
    x0, y0 = project(u - su * 0.5, v - sv * 0.5)
    x1, y1 = project(u + su * 0.5, v + sv * 0.5)
    return (min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1))


def _area_plane_dims(area: dict, view: str) -> tuple[float, float]:
    shape = str(area.get("shape", "square"))
    size = area.get("size") if isinstance(area.get("size"), list) else []
    sx = float(size[0] if len(size) > 0 else 4.0)
    sy = float(size[1] if len(size) > 1 else (3.0 if shape == "cube" else 0.18))
    sz = float(size[2] if len(size) > 2 else (size[1] if len(size) > 1 else sx))
    if shape == "square":
        sy = max(0.18, sy if len(size) > 2 else 0.18)
        sz = float(size[1] if len(size) > 1 else sx)
    if view in ("front", "back"):
        return (sx, sy)
    if view in ("left", "right"):
        return (sz, sy)
    return (sx, sz)


def _view_cube_faces() -> dict[str, tuple[float, float, float, float]]:
    width = VIEW_CUBE_CELL * 3.0
    height = VIEW_CUBE_CELL * 4.0
    left = WIDTH - VIEW_CUBE_MARGIN - width
    top = VIEW_CUBE_MARGIN
    return {
        "top": (left + VIEW_CUBE_CELL, top + 0.0, left + 2.0 * VIEW_CUBE_CELL, top + VIEW_CUBE_CELL),
        "left": (left + 0.0, top + VIEW_CUBE_CELL, left + VIEW_CUBE_CELL, top + 2.0 * VIEW_CUBE_CELL),
        "front": (left + VIEW_CUBE_CELL, top + VIEW_CUBE_CELL, left + 2.0 * VIEW_CUBE_CELL, top + 2.0 * VIEW_CUBE_CELL),
        "right": (left + 2.0 * VIEW_CUBE_CELL, top + VIEW_CUBE_CELL, left + 3.0 * VIEW_CUBE_CELL, top + 2.0 * VIEW_CUBE_CELL),
        "bottom": (left + VIEW_CUBE_CELL, top + 2.0 * VIEW_CUBE_CELL, left + 2.0 * VIEW_CUBE_CELL, top + 3.0 * VIEW_CUBE_CELL),
        "back": (left + VIEW_CUBE_CELL, top + 3.0 * VIEW_CUBE_CELL, left + 2.0 * VIEW_CUBE_CELL, top + 4.0 * VIEW_CUBE_CELL),
    }


def _build_view_cube_commands(commands: list[dict], current_view: str):
    for face, rect in _view_cube_faces().items():
        x0, y0, x1, y1 = rect
        active = face == current_view
        fill = (0.34, 0.48, 0.82) if active else (0.16, 0.18, 0.21)
        edge = (0.78, 0.82, 0.92) if active else (0.42, 0.46, 0.54)
        commands.extend(_rect_commands(x0, y0, x1, y1, fill, edge, depth=0.12))
        commands.append({
            "op": "text",
            "x": x0 + 6,
            "y": y0 + 10,
            "text": _VIEW_LABELS[face],
            "size": 11,
            "r": 0.96,
            "g": 0.97,
            "b": 0.99,
        })


def _point_in_rect(x: float, y: float, rect: tuple[float, float, float, float]) -> bool:
    return rect[0] <= x <= rect[2] and rect[1] <= y <= rect[3]


def _inflate_rect(rect: tuple[float, float, float, float], pad: float) -> tuple[float, float, float, float]:
    return (rect[0] - pad, rect[1] - pad, rect[2] + pad, rect[3] + pad)


def view_cube_hit_test(x: float, y: float) -> str | None:
    # Prefer exact painted face hits first so adjacent inflated regions do not
    # steal clicks from the intended face.
    for face, rect in _view_cube_faces().items():
        if _point_in_rect(x, y, rect):
            return face
    best_face = None
    best_distance = None
    for face, rect in _view_cube_faces().items():
        if _point_in_rect(x, y, _inflate_rect(rect, VIEW_CUBE_HIT_PAD)):
            cx = (rect[0] + rect[2]) * 0.5
            cy = (rect[1] + rect[3]) * 0.5
            distance = math.hypot(x - cx, y - cy)
            if best_distance is None or distance < best_distance:
                best_face = face
                best_distance = distance
    return best_face


def view_cube_contains(x: float, y: float) -> bool:
    rects = list(_view_cube_faces().values())
    if not rects:
        return False
    x0 = min(r[0] for r in rects)
    y0 = min(r[1] for r in rects)
    x1 = max(r[2] for r in rects)
    y1 = max(r[3] for r in rects)
    return _point_in_rect(x, y, _inflate_rect((x0, y0, x1, y1), VIEW_CUBE_HIT_PAD))


def rotate_view(current_view: str, dx: float, dy: float) -> str:
    if abs(dx) < 12.0 and abs(dy) < 12.0:
        return current_view
    direction = "right" if abs(dx) >= abs(dy) and dx > 0.0 else None
    if direction is None and abs(dx) >= abs(dy):
        direction = "left"
    if direction is None:
        direction = "down" if dy > 0.0 else "up"
    return _VIEW_ROTATION.get(current_view, _VIEW_ROTATION["front"]).get(direction, current_view)


def _area_screen_entries(doc: dict, editor_state: dict | None = None) -> list[dict]:
    view, _, _ = _editor_view_state(doc, editor_state)
    project = _projector(doc, editor_state)
    entries: list[dict] = []
    areas = doc.get("areas")
    if not isinstance(areas, list):
        return entries
    for idx, area in enumerate(areas):
        if not isinstance(area, dict):
            continue
        transform = area.get("transform") if isinstance(area.get("transform"), dict) else {}
        pos = transform.get("position") if isinstance(transform.get("position"), list) else [0.0, 0.0, 0.0]
        world = [
            float(pos[0] if len(pos) > 0 else 0.0),
            float(pos[1] if len(pos) > 1 else 0.0),
            float(pos[2] if len(pos) > 2 else 0.0),
        ]
        u, v = _world_to_plane(view, world)
        su, sv = _area_plane_dims(area, view)
        rect = _screen_rect_from_center(project, u, v, su, sv)
        entries.append({"index": idx, "area": area, "world": world, "rect": rect})
    return entries


def _distance_point_to_segment(px: float, py: float, x0: float, y0: float, x1: float, y1: float) -> float:
    dx = x1 - x0
    dy = y1 - y0
    if abs(dx) < 1e-6 and abs(dy) < 1e-6:
        return math.hypot(px - x0, py - y0)
    t = ((px - x0) * dx + (py - y0) * dy) / ((dx * dx) + (dy * dy))
    t = max(0.0, min(1.0, t))
    cx = x0 + dx * t
    cy = y0 + dy * t
    return math.hypot(px - cx, py - cy)


def hit_test_item(doc: dict, editor_state: dict | None, x: float, y: float) -> dict | None:
    for entry in reversed(_area_screen_entries(doc, editor_state)):
        if _point_in_rect(x, y, entry["rect"]):
            return {"kind": "area", "index": entry["index"], "world": list(entry["world"])}
    view = _editor_view_state(doc, editor_state)[0]
    project = _projector(doc, editor_state)
    passages = doc.get("passages")
    if isinstance(passages, list):
        for idx in range(len(passages) - 1, -1, -1):
            passage = passages[idx]
            if not isinstance(passage, dict):
                continue
            path = passage.get("path") if isinstance(passage.get("path"), dict) else {}
            start = path.get("start") if isinstance(path.get("start"), list) else None
            end = path.get("end") if isinstance(path.get("end"), list) else None
            if not start or not end or len(start) < 3 or len(end) < 3:
                continue
            x0, y0 = project(*_world_to_plane(view, start))
            x1, y1 = project(*_world_to_plane(view, end))
            if _distance_point_to_segment(x, y, x0, y0, x1, y1) <= 8.0:
                mid = [
                    (float(start[0]) + float(end[0])) * 0.5,
                    (float(start[1]) + float(end[1])) * 0.5,
                    (float(start[2]) + float(end[2])) * 0.5,
                ]
                return {"kind": "passage", "index": idx, "world": mid}
    return None


def move_item(doc: dict, item: dict, delta_world: tuple[float, float, float]) -> dict:
    dx, dy, dz = float(delta_world[0]), float(delta_world[1]), float(delta_world[2])
    kind = str(item.get("kind", ""))
    index = int(item.get("index", -1))
    if kind == "area":
        areas = doc.get("areas")
        if isinstance(areas, list) and 0 <= index < len(areas) and isinstance(areas[index], dict):
            transform = areas[index].setdefault("transform", {})
            pos = transform.get("position") if isinstance(transform.get("position"), list) else [0.0, 0.0, 0.0]
            x = float(pos[0] if len(pos) > 0 else 0.0) + dx
            y = float(pos[1] if len(pos) > 1 else 0.0) + dy
            z = float(pos[2] if len(pos) > 2 else 0.0) + dz
            transform["position"] = [round(x, 3), round(y, 3), round(z, 3)]
    elif kind == "passage":
        passages = doc.get("passages")
        if isinstance(passages, list) and 0 <= index < len(passages) and isinstance(passages[index], dict):
            path = passages[index].setdefault("path", {})
            for key in ("start", "end"):
                pos = path.get(key) if isinstance(path.get(key), list) else [0.0, 0.0, 0.0]
                x = float(pos[0] if len(pos) > 0 else 0.0) + dx
                y = float(pos[1] if len(pos) > 1 else 0.0) + dy
                z = float(pos[2] if len(pos) > 2 else 0.0) + dz
                path[key] = [round(x, 3), round(y, 3), round(z, 3)]
    return doc


def build_level_path_preview(doc: dict, editor_state: dict | None = None) -> tuple[list[dict], list[str]]:
    commands: list[dict] = [{"op": "clear", "r": 0.035, "g": 0.04, "b": 0.05}]
    errors: list[str] = []
    view, zoom, center = _editor_view_state(doc, editor_state)
    project = _projector(doc, editor_state)
    tool = str((editor_state or {}).get("tool", "square"))
    pending_line = (editor_state or {}).get("pending_line")
    active_item = (editor_state or {}).get("active_item")

    for entry in _area_screen_entries(doc, editor_state):
        area = entry["area"]
        shape = str(area.get("shape", "square"))
        fill = _color(shape)
        edge = _edge(fill)
        rect = entry["rect"]
        if active_item and active_item.get("kind") == "area" and int(active_item.get("index", -1)) == entry["index"]:
            fill = tuple(min(1.0, c + 0.14) for c in fill)
        commands.extend(_rect_commands(rect[0], rect[1], rect[2], rect[3], fill, edge))
        cx = (rect[0] + rect[2]) * 0.5
        cy = (rect[1] + rect[3]) * 0.5
        label = f"{area.get('id', shape)}"
        if shape == "cube":
            commands.append({"op": "line", "x0": rect[0] + 8, "y0": rect[3] - 8, "x1": rect[2] - 8, "y1": rect[1] + 8, "r": edge[0], "g": edge[1], "b": edge[2]})
        commands.append({"op": "text", "x": cx + 8, "y": cy - 8, "text": label, "size": 12, "r": 0.94, "g": 0.95, "b": 0.98})

    passages = doc.get("passages")
    if isinstance(passages, list):
        for idx, passage in enumerate(passages):
            if not isinstance(passage, dict):
                continue
            path = passage.get("path") if isinstance(passage.get("path"), dict) else {}
            start = path.get("start") if isinstance(path.get("start"), list) else None
            end = path.get("end") if isinstance(path.get("end"), list) else None
            if not start or not end or len(start) < 3 or len(end) < 3:
                continue
            x0, y0 = project(*_world_to_plane(view, start))
            x1, y1 = project(*_world_to_plane(view, end))
            line_color = (0.99, 0.90, 0.42) if active_item and active_item.get("kind") == "passage" and int(active_item.get("index", -1)) == idx else (0.94, 0.84, 0.30)
            commands.append({"op": "line", "x0": x0, "y0": y0, "x1": x1, "y1": y1, "r": line_color[0], "g": line_color[1], "b": line_color[2]})
            nodes = passage.get("section_nodes")
            if isinstance(nodes, list):
                for node in nodes:
                    if not isinstance(node, dict):
                        continue
                    t = float(node.get("t", 0.0) or 0.0)
                    px = float(start[0]) + (float(end[0]) - float(start[0])) * t
                    py = float(start[1]) + (float(end[1]) - float(start[1])) * t
                    pz = float(start[2]) + (float(end[2]) - float(start[2])) * t
                    sx, sv = 0.45, 0.45
                    size = node.get("size") if isinstance(node.get("size"), list) else []
                    if len(size) >= 2:
                        sx = max(0.22, float(size[0]) * 0.2)
                        sv = max(0.22, float(size[1]) * 0.2)
                    ru, rv = _world_to_plane(view, [px, py, pz])
                    rx0, ry0, rx1, ry1 = _screen_rect_from_center(project, ru, rv, sx, sv)
                    commands.extend(_rect_commands(rx0, ry0, rx1, ry1, (0.88, 0.78, 0.28), (0.52, 0.44, 0.12), depth=0.28))

    if isinstance(pending_line, dict):
        start = pending_line.get("start")
        if isinstance(start, list) and len(start) >= 2:
            sx, sy = project(float(start[0]), float(start[1]))
            commands.extend(_rect_commands(sx - 4, sy - 4, sx + 4, sy + 4, (0.98, 0.58, 0.18), (0.56, 0.30, 0.10), depth=0.11))
            commands.append({"op": "text", "x": 26, "y": 82, "text": "Line tool: click end point", "size": 14, "r": 0.96, "g": 0.80, "b": 0.32})

    _build_view_cube_commands(commands, view)
    commands.append({"op": "text", "x": 22, "y": 26, "text": f"Level Path Editor  tool={_tool_label(tool)}  view={view}  zoom={zoom:.1f}", "size": 18, "r": 0.96, "g": 0.97, "b": 0.99})
    commands.append({"op": "text", "x": 22, "y": 48, "text": f"center=({center[0]:.2f}, {center[1]:.2f})  open space  closed space  connecting corridor", "size": 13, "r": 0.74, "g": 0.79, "b": 0.84})
    return commands, errors


def load_level_path_preview(path: Path, editor_state: dict | None = None) -> tuple[dict, list[dict], list[str]]:
    doc = load_level_path_document(path)
    commands, errors = build_level_path_preview(doc, editor_state)
    return doc, commands, errors


def place_primitive(doc: dict, tool: str, world_point: tuple[float, float, float], editor_state: dict | None = None) -> tuple[dict, dict]:
    editor_state = dict(editor_state or {})
    areas = doc.setdefault("areas", [])
    passages = doc.setdefault("passages", [])
    wx, wy, wz = float(world_point[0]), float(world_point[1]), float(world_point[2])
    if tool == "square":
        idx = len([a for a in areas if isinstance(a, dict) and a.get("shape") == "square"])
        areas.append({
            "id": f"square_{idx:02d}",
            "kind": "open_sky",
            "shape": "square",
            "size": [4.0, 4.0],
            "transform": {"position": [round(wx, 3), 0.0, round(wz, 3)]},
            "tags": ["placed"]
        })
        return doc, editor_state
    if tool == "cube":
        idx = len([a for a in areas if isinstance(a, dict) and a.get("shape") == "cube"])
        areas.append({
            "id": f"cube_{idx:02d}",
            "kind": "closed_space",
            "shape": "cube",
            "size": [4.0, 3.0, 4.0],
            "transform": {"position": [round(wx, 3), round(wy, 3), round(wz, 3)]},
            "tags": ["placed"]
        })
        return doc, editor_state
    if tool == "line":
        pending = editor_state.get("pending_line")
        view = str(editor_state.get("view", "top") or "top")
        plane = list(_world_to_plane(view, [wx, wy, wz]))
        if not isinstance(pending, dict):
            editor_state["pending_line"] = {"start": [round(plane[0], 3), round(plane[1], 3)], "anchor": [round(wx, 3), round(wy, 3), round(wz, 3)]}
            return doc, editor_state
        start = pending.get("anchor")
        if isinstance(start, list) and len(start) >= 3:
            idx = len(passages)
            passages.append({
                "id": f"line_{idx:02d}",
                "shape": "line",
                "mode": "horizontal",
                "from_area": "",
                "to_area": "",
                "path": {
                    "start": [float(start[0]), float(start[1]), float(start[2])],
                    "end": [round(wx, 3), round(wy, 3), round(wz, 3)]
                },
                "section_nodes": [
                    {"t": 0.0, "shape": "square", "size": [1.4, 1.4]},
                    {"t": 1.0, "shape": "square", "size": [1.4, 1.4]}
                ],
                "tags": ["placed"]
            })
        editor_state["pending_line"] = None
    return doc, editor_state


def screen_to_world(doc: dict, x: float, y: float, editor_state: dict | None = None, anchor: list[float] | tuple[float, float, float] | None = None) -> tuple[float, float, float]:
    view, _, _ = _editor_view_state(doc, editor_state)
    u, v = _unproject(doc, x, y, editor_state)
    return _plane_to_world(view, u, v, anchor)
