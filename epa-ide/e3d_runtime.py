from __future__ import annotations

import json
import math
from pathlib import Path


WIDTH = 1180.0
HEIGHT = 820.0


def load_e3d_document(path: Path) -> dict:
    doc = json.loads(path.read_text(encoding="utf-8"))
    if isinstance(doc, dict):
        doc["__source_path"] = str(path.resolve())
    return doc


def _vec3(value, default: tuple[float, float, float] = (0.0, 0.0, 0.0)) -> list[float]:
    if isinstance(value, list):
        out = [float(default[0]), float(default[1]), float(default[2])]
        for i in range(min(3, len(value))):
            out[i] = float(value[i])
        return out
    return [float(default[0]), float(default[1]), float(default[2])]


def _transform_value(transform: dict | None, key: str, default: tuple[float, float, float]) -> list[float]:
    if isinstance(transform, dict):
        return _vec3(transform.get(key), default)
    return [float(default[0]), float(default[1]), float(default[2])]


def _color(doc: dict, material_id: str, fallback: tuple[float, float, float]) -> tuple[float, float, float]:
    material = (doc.get("materials") or {}).get(material_id, {})
    color = material.get("base_color") if isinstance(material, dict) else None
    if isinstance(color, list) and len(color) >= 3:
        try:
            return (float(color[0]), float(color[1]), float(color[2]))
        except Exception:
            pass
    return fallback


def _anchor_obj(anchor_value) -> dict | None:
    if isinstance(anchor_value, dict):
        return anchor_value
    if isinstance(anchor_value, list) and len(anchor_value) >= 3:
        return {"position": [anchor_value[0], anchor_value[1], anchor_value[2]]}
    return None


def _component_map(doc: dict) -> dict:
    value = doc.get("components")
    return value if isinstance(value, dict) else {}


def _source_dir(doc: dict) -> Path | None:
    raw = doc.get("__source_path")
    if isinstance(raw, str) and raw:
        try:
            return Path(raw).resolve().parent
        except Exception:
            return None
    return None


def _import_entries(doc: dict) -> dict[str, dict]:
    imports = doc.get("imports")
    out: dict[str, dict] = {}
    if not isinstance(imports, list):
        return out
    base_dir = _source_dir(doc)
    for item in imports:
        if not isinstance(item, dict):
            continue
        alias = item.get("id")
        rel_path = item.get("path")
        component_id = item.get("component")
        if not isinstance(alias, str) or not alias or not isinstance(rel_path, str) or not rel_path:
            continue
        if base_dir is None:
            continue
        target = (base_dir / rel_path).resolve()
        try:
            imported_doc = load_e3d_document(target)
        except Exception:
            continue
        components = _component_map(imported_doc)
        selected_id = component_id if isinstance(component_id, str) and component_id else next(iter(components.keys()), None)
        component = components.get(selected_id) if selected_id else None
        if isinstance(component, dict):
            out[alias] = {"component": component, "doc": imported_doc, "component_id": selected_id}
    return out


def _resolve_component_ref(doc: dict, component_ref: str) -> tuple[dict | None, dict]:
    components = _component_map(doc)
    component = components.get(component_ref)
    if isinstance(component, dict):
        return component, doc
    entry = _import_entries(doc).get(component_ref)
    if isinstance(entry, dict):
        imported_component = entry.get("component")
        imported_doc = entry.get("doc")
        if isinstance(imported_component, dict) and isinstance(imported_doc, dict):
            return imported_component, imported_doc
    return None, doc


def _instance_list(doc: dict) -> list[dict]:
    value = doc.get("instances")
    return value if isinstance(value, list) else []


def _transform_point(point: list[float], transform: dict | None) -> list[float]:
    tx, ty, tz = _transform_value(transform, "position", (0.0, 0.0, 0.0))
    sx, sy, sz = _transform_value(transform, "scale", (1.0, 1.0, 1.0))
    return [
        float(point[0]) * sx + tx,
        float(point[1]) * sy + ty,
        float(point[2]) * sz + tz,
    ]


def _compose_transform(parent: dict | None, local: dict | None) -> dict:
    parent_pos = _transform_value(parent, "position", (0.0, 0.0, 0.0))
    parent_scale = _transform_value(parent, "scale", (1.0, 1.0, 1.0))
    local_pos = _transform_value(local, "position", (0.0, 0.0, 0.0))
    local_scale = _transform_value(local, "scale", (1.0, 1.0, 1.0))
    return {
        "position": [
            parent_pos[0] + local_pos[0] * parent_scale[0],
            parent_pos[1] + local_pos[1] * parent_scale[1],
            parent_pos[2] + local_pos[2] * parent_scale[2],
        ],
        "scale": [
            parent_scale[0] * local_scale[0],
            parent_scale[1] * local_scale[1],
            parent_scale[2] * local_scale[2],
        ],
        "rotation": _transform_value(parent, "rotation", (0.0, 0.0, 0.0)),
    }


def _resolve_points_ref(doc: dict, ref: str) -> list:
    current = doc
    for part in ref.split("."):
        if not isinstance(current, dict):
            return []
        current = current.get(part)
    return current if isinstance(current, list) else []


def _mirrored_point(point: list[float], axis: str) -> list[float]:
    x, y, z = float(point[0]), float(point[1]), float(point[2] if len(point) > 2 else 0.0)
    if axis == "x":
        x = -x
    elif axis == "y":
        y = -y
    elif axis == "z":
        z = -z
    return [x, y, z]


def _preview_instances(doc: dict) -> list[dict]:
    instances = [item for item in _instance_list(doc) if isinstance(item, dict)]
    out: list[dict] = [json.loads(json.dumps(item)) for item in instances]
    operations = doc.get("operations")
    if not isinstance(operations, list):
        return out
    source_map = {str(item.get("id", "")): item for item in instances if isinstance(item, dict)}
    for op in operations:
        if not isinstance(op, dict) or op.get("op") != "mirror":
            continue
        source_id = str(op.get("source_instance", ""))
        source = source_map.get(source_id)
        if not source:
            continue
        mirrored = json.loads(json.dumps(source))
        mirrored["id"] = str(op.get("name") or f"{source_id}_mirror")
        axis = str(op.get("axis", "x"))
        pos = _transform_value(mirrored.get("transform"), "position", (0.0, 0.0, 0.0))
        if axis == "x":
            pos[0] = -pos[0]
        elif axis == "y":
            pos[1] = -pos[1]
        elif axis == "z":
            pos[2] = -pos[2]
        mirrored["transform"] = {
            "position": pos,
            "rotation": _transform_value(mirrored.get("transform"), "rotation", (0.0, 0.0, 0.0)),
            "scale": _transform_value(mirrored.get("transform"), "scale", (1.0, 1.0, 1.0)),
        }
        mirrored["_mirror_axis"] = axis
        resolved, _ = _resolve_component_ref(doc, str(mirrored.get("component", "")))
        if isinstance(resolved, dict):
            out.append(mirrored)
    return out


def _preview_instance_map(doc: dict) -> dict[str, dict]:
    return {
        str(item.get("id", "")): item
        for item in _preview_instances(doc)
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }


def _resolve_instance_anchor(doc: dict, instance_id: str, anchor_name: str) -> dict:
    instances = _preview_instance_map(doc)
    instance = instances.get(instance_id)
    if not instance:
        raise ValueError(f"missing instance '{instance_id}'")
    component_id = instance.get("component")
    component, component_doc = _resolve_component_ref(doc, str(component_id))
    if not isinstance(component, dict):
        raise ValueError(f"missing component '{component_id}' for instance '{instance_id}'")
    anchors = component.get("anchors")
    if not isinstance(anchors, dict):
        raise ValueError(f"component '{component_id}' has no anchors")
    anchor = _anchor_obj(anchors.get(anchor_name))
    if not anchor:
        raise ValueError(f"missing anchor '{anchor_name}' on component '{component_id}'")
    pos = anchor.get("position")
    if not isinstance(pos, list) or len(pos) < 3:
        raise ValueError(f"anchor '{anchor_name}' on component '{component_id}' has invalid position")
    world = _transform_point(
        [float(pos[0]), float(pos[1]), float(pos[2])],
        _compose_transform(instance.get("transform"), component.get("local_transform")),
    )
    return {
        "instance_id": instance_id,
        "component_id": component_id,
        "anchor_name": anchor_name,
        "world_position": world,
        "connector": anchor.get("connector"),
        "tags": anchor.get("tags") or [],
    }


def _range_text(joint: dict) -> str:
    mode = str(joint.get("mode", "unknown"))
    rotation = joint.get("rotation_limit_deg")
    translation = joint.get("translation_limit")
    bits = [mode]
    if isinstance(rotation, dict):
        for axis in ("pitch", "yaw", "roll"):
            bounds = rotation.get(axis)
            if isinstance(bounds, list) and len(bounds) >= 2:
                bits.append(f"{axis[0]}[{bounds[0]},{bounds[1]}]")
    if isinstance(translation, dict):
        for axis in ("x", "y", "z"):
            bounds = translation.get(axis)
            if isinstance(bounds, list) and len(bounds) >= 2 and (float(bounds[0]) != 0.0 or float(bounds[1]) != 0.0):
                bits.append(f"{axis}[{bounds[0]},{bounds[1]}]")
    return " ".join(bits)


def _joint_color(mode: str) -> tuple[float, float, float]:
    if mode == "rigid":
        return (0.98, 0.58, 0.20)
    if mode == "hinge":
        return (0.28, 0.72, 0.98)
    if mode == "slider":
        return (0.96, 0.82, 0.18)
    if mode == "ball":
        return (0.88, 0.34, 0.82)
    if mode == "planar":
        return (0.30, 0.88, 0.72)
    return (0.86, 0.86, 0.86)


def _projector(doc: dict):
    preview = doc.get("preview_2d") if isinstance(doc.get("preview_2d"), dict) else {}
    scale = float(preview.get("scale", 210.0) or 210.0)
    center = preview.get("center") if isinstance(preview.get("center"), list) else [0.0, 0.0]
    cx = float(center[0] if len(center) > 0 else 0.0)
    cy = float(center[1] if len(center) > 1 else 0.0)
    depth_scale = float(preview.get("depth_scale", 180.0) or 180.0)

    def project(point3: list[float], camera: dict | None = None) -> tuple[float, float]:
        x = float(point3[0] if len(point3) > 0 else 0.0)
        y = float(point3[1] if len(point3) > 1 else 0.0)
        z = float(point3[2] if len(point3) > 2 else 0.0)
        if camera:
            yaw = math.radians(float(camera.get("yaw_deg", 0.0)))
            pitch = math.radians(float(camera.get("pitch_deg", 0.0)))
            dx = x - cx
            dy = y - cy
            dz = z
            cos_y = math.cos(yaw)
            sin_y = math.sin(yaw)
            rx = dx * cos_y + dz * sin_y
            rz = -dx * sin_y + dz * cos_y
            cos_p = math.cos(pitch)
            sin_p = math.sin(pitch)
            ry = dy * cos_p - rz * sin_p
            rz2 = dy * sin_p + rz * cos_p
            camera_distance = float(camera.get("distance", 4.8))
            denom = max(0.4, camera_distance - rz2)
            persp = camera_distance / denom
            sx = WIDTH * 0.48 + rx * scale * persp
            sy = HEIGHT * 0.55 - ry * scale * persp + rz2 * 0.02 * depth_scale
            return (sx, sy)
        return (WIDTH * 0.48 + (x - cx) * scale, HEIGHT * 0.55 - (y - cy) * scale)

    return project


def _camera_space(doc: dict, point3: list[float], camera: dict | None) -> tuple[float, float, float]:
    x = float(point3[0] if len(point3) > 0 else 0.0)
    y = float(point3[1] if len(point3) > 1 else 0.0)
    z = float(point3[2] if len(point3) > 2 else 0.0)
    preview = doc.get("preview_2d") if isinstance(doc.get("preview_2d"), dict) else {}
    center = preview.get("center") if isinstance(preview.get("center"), list) else [0.0, 0.0]
    cx = float(center[0] if len(center) > 0 else 0.0)
    cy = float(center[1] if len(center) > 1 else 0.0)
    if not camera:
        return (x - cx, y - cy, z)
    yaw = math.radians(float(camera.get("yaw_deg", 0.0)))
    pitch = math.radians(float(camera.get("pitch_deg", 0.0)))
    dx = x - cx
    dy = y - cy
    dz = z
    cos_y = math.cos(yaw)
    sin_y = math.sin(yaw)
    rx = dx * cos_y + dz * sin_y
    rz = -dx * sin_y + dz * cos_y
    cos_p = math.cos(pitch)
    sin_p = math.sin(pitch)
    ry = dy * cos_p - rz * sin_p
    rz2 = dy * sin_p + rz * cos_p
    return (rx, ry, rz2)


def _project_points(project, points3: list[list[float]], camera: dict | None = None) -> list[tuple[float, float]]:
    return [project(pt, camera) for pt in points3]


def _darken(color: tuple[float, float, float], factor: float = 0.42) -> tuple[float, float, float]:
    return (
        max(color[0] * factor, 0.06),
        max(color[1] * factor, 0.06),
        max(color[2] * factor, 0.06),
    )


def _polygon_signed_area(points2: list[tuple[float, float]]) -> float:
    area = 0.0
    for i in range(len(points2)):
        x1, y1 = points2[i]
        x2, y2 = points2[(i + 1) % len(points2)]
        area += x1 * y2 - x2 * y1
    return area * 0.5


def _cross2(a: tuple[float, float], b: tuple[float, float], c: tuple[float, float]) -> float:
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def _point_in_triangle(p: tuple[float, float], a: tuple[float, float], b: tuple[float, float], c: tuple[float, float]) -> bool:
    c1 = _cross2(a, b, p)
    c2 = _cross2(b, c, p)
    c3 = _cross2(c, a, p)
    has_neg = c1 < 0.0 or c2 < 0.0 or c3 < 0.0
    has_pos = c1 > 0.0 or c2 > 0.0 or c3 > 0.0
    return not (has_neg and has_pos)


def _triangulate_polygon(points2: list[tuple[float, float]]) -> list[tuple[int, int, int]]:
    if len(points2) < 3:
        return []
    winding_positive = _polygon_signed_area(points2) > 0.0
    indices = list(range(len(points2)))
    triangles: list[tuple[int, int, int]] = []
    guard = 0
    while len(indices) > 3 and guard < len(points2) * len(points2):
        guard += 1
        ear_found = False
        for idx in range(len(indices)):
            i_prev = indices[(idx - 1) % len(indices)]
            i_curr = indices[idx]
            i_next = indices[(idx + 1) % len(indices)]
            a = points2[i_prev]
            b = points2[i_curr]
            c = points2[i_next]
            cross = _cross2(a, b, c)
            if winding_positive:
                if cross <= 1e-6:
                    continue
            else:
                if cross >= -1e-6:
                    continue
            blocked = False
            for other in indices:
                if other in (i_prev, i_curr, i_next):
                    continue
                if _point_in_triangle(points2[other], a, b, c):
                    blocked = True
                    break
            if blocked:
                continue
            triangles.append((i_prev, i_curr, i_next))
            del indices[idx]
            ear_found = True
            break
        if not ear_found:
            break
    if len(indices) == 3:
        triangles.append((indices[0], indices[1], indices[2]))
    return triangles


def _add_polygon(commands: list[dict], points2: list[tuple[float, float]], fill: tuple[float, float, float], edge: tuple[float, float, float], depth: float = 0.24, draw_edges: bool = True):
    if len(points2) < 3:
        return
    triangles = _triangulate_polygon(points2)
    if not triangles:
        triangles = [(0, i, i + 1) for i in range(1, len(points2) - 1)]
    for i0, i1, i2 in triangles:
        x0, y0 = points2[i0]
        x1, y1 = points2[i1]
        x2, y2 = points2[i2]
        commands.append({
            "op": "triangle",
            "x0": x0, "y0": y0,
            "x1": x1, "y1": y1,
            "x2": x2, "y2": y2,
            "depth": depth,
            "r": fill[0], "g": fill[1], "b": fill[2],
        })
    if draw_edges:
        for i in range(len(points2)):
            x1, y1 = points2[i]
            x2, y2 = points2[(i + 1) % len(points2)]
            commands.append({"op": "line", "x0": x1, "y0": y1, "x1": x2, "y1": y2, "r": edge[0], "g": edge[1], "b": edge[2]})


def _depth_for_points(doc: dict, points3: list[list[float]], camera: dict | None, bias: float = 0.0) -> float:
    if not points3:
        return 0.5
    avg_z = sum(_camera_space(doc, pt, camera)[2] for pt in points3) / float(len(points3))
    depth = 0.5 - avg_z * 0.08 + bias
    if depth < 0.02:
        depth = 0.02
    if depth > 0.98:
        depth = 0.98
    return depth


def _add_polygon3d(commands: list[dict], doc: dict, project, points3: list[list[float]], fill: tuple[float, float, float], edge: tuple[float, float, float], camera: dict | None, depth_bias: float = 0.0, draw_edges: bool = True):
    _add_polygon(commands, _project_points(project, points3, camera), fill, edge, depth=_depth_for_points(doc, points3, camera, depth_bias), draw_edges=draw_edges)


def _render_box(commands: list[dict], project, doc: dict, world_transform: dict, size_value, fill, edge, camera: dict | None):
    sx, sy, _ = _vec3(size_value, (1.0, 1.0, 1.0))
    corners = [
        [-sx * 0.5, -sy * 0.5, 0.0],
        [sx * 0.5, -sy * 0.5, 0.0],
        [sx * 0.5, sy * 0.5, 0.0],
        [-sx * 0.5, sy * 0.5, 0.0],
    ]
    points3 = [_transform_point(pt, world_transform) for pt in corners]
    _add_polygon3d(commands, doc, project, points3, fill, edge, camera)


def _render_polygon_fan(commands: list[dict], project, doc: dict, component: dict, world_transform: dict, fill, edge, mirror_axis: str | None, camera: dict | None):
    points_ref = component.get("points_ref")
    if not isinstance(points_ref, str):
        return
    raw = _resolve_points_ref(doc, points_ref)
    points3: list[list[float]] = []
    for pt in raw:
        if isinstance(pt, list) and len(pt) >= 2:
            p = [float(pt[0]), float(pt[1]), 0.0]
            if mirror_axis:
                p = _mirrored_point(p, mirror_axis)
            points3.append(_transform_point(p, world_transform))
    _add_polygon3d(commands, doc, project, points3, fill, edge, camera)


def _render_thin_shell_from_outline(commands: list[dict], project, doc: dict, component: dict, world_transform: dict, fill, edge, mirror_axis: str | None, camera: dict | None):
    points_ref = component.get("points_ref")
    if not isinstance(points_ref, str):
        return
    raw = _resolve_points_ref(doc, points_ref)
    thickness = float(component.get("thickness", 0.001) or 0.001)
    back_fill = _color(doc, str(component.get("back_material") or component.get("material") or "default"), fill)
    side_fill = _color(doc, str(component.get("side_material") or component.get("material") or "default"), _darken(fill, 0.85))
    front3: list[list[float]] = []
    back3: list[list[float]] = []
    half = thickness * 0.5
    for pt in raw:
        if isinstance(pt, list) and len(pt) >= 2:
            p = [float(pt[0]), float(pt[1]), 0.0]
            if mirror_axis:
                p = _mirrored_point(p, mirror_axis)
            front3.append(_transform_point([p[0], p[1], p[2] + half], world_transform))
            back3.append(_transform_point([p[0], p[1], p[2] - half], world_transform))
    if len(front3) < 3:
        return
    _add_polygon3d(commands, doc, project, front3, fill, edge, camera, depth_bias=-0.01)
    _add_polygon3d(commands, doc, project, list(reversed(back3)), back_fill, edge, camera, depth_bias=0.01)
    for i in range(len(front3)):
        j = (i + 1) % len(front3)
        quad = [front3[i], front3[j], back3[j], back3[i]]
        _add_polygon3d(commands, doc, project, quad, side_fill, edge, camera)


def _axis_rects_from_cutouts(outer_pts: list[list[float]], cutout_groups: list[list[list[float]]]) -> list[list[list[float]]]:
    if not outer_pts:
        return []
    ox0 = min(float(p[0]) for p in outer_pts)
    ox1 = max(float(p[0]) for p in outer_pts)
    oy0 = min(float(p[1]) for p in outer_pts)
    oy1 = max(float(p[1]) for p in outer_pts)
    holes: list[tuple[float, float, float, float]] = []
    y_values = {oy0, oy1}
    for group in cutout_groups:
        if not group:
            continue
        hx0 = min(float(p[0]) for p in group)
        hx1 = max(float(p[0]) for p in group)
        hy0 = min(float(p[1]) for p in group)
        hy1 = max(float(p[1]) for p in group)
        holes.append((hx0, hx1, hy0, hy1))
        y_values.add(hy0)
        y_values.add(hy1)
    ys = sorted(y_values)
    rects: list[list[list[float]]] = []
    for i in range(len(ys) - 1):
        y0 = ys[i]
        y1 = ys[i + 1]
        if y1 - y0 <= 1e-6:
            continue
        y_mid = (y0 + y1) * 0.5
        spans = [(ox0, ox1)]
        for hx0, hx1, hy0, hy1 in holes:
            if y_mid <= hy0 + 1e-6 or y_mid >= hy1 - 1e-6:
                continue
            new_spans: list[tuple[float, float]] = []
            for sx0, sx1 in spans:
                if hx1 <= sx0 or hx0 >= sx1:
                    new_spans.append((sx0, sx1))
                    continue
                if hx0 > sx0:
                    new_spans.append((sx0, hx0))
                if hx1 < sx1:
                    new_spans.append((hx1, sx1))
            spans = new_spans
        for sx0, sx1 in spans:
            if sx1 - sx0 <= 1e-6:
                continue
            rects.append([
                [sx0, y0, 0.0],
                [sx1, y0, 0.0],
                [sx1, y1, 0.0],
                [sx0, y1, 0.0],
            ])
    return rects


def _render_wall_shell_with_cutouts(commands: list[dict], project, doc: dict, component: dict, world_transform: dict, fill, edge, mirror_axis: str | None, camera: dict | None):
    points_ref = component.get("points_ref")
    if not isinstance(points_ref, str):
        return
    raw_outer = _resolve_points_ref(doc, points_ref)
    cutout_refs = component.get("cutout_refs") if isinstance(component.get("cutout_refs"), list) else []
    thickness = float(component.get("thickness", 0.14) or 0.14)
    back_fill = _color(doc, str(component.get("back_material") or component.get("material") or "default"), fill)
    side_fill = _color(doc, str(component.get("side_material") or component.get("material") or "default"), _darken(fill, 0.85))
    outer2: list[list[float]] = []
    for pt in raw_outer:
        if isinstance(pt, list) and len(pt) >= 2:
            p = [float(pt[0]), float(pt[1]), 0.0]
            if mirror_axis:
                p = _mirrored_point(p, mirror_axis)
            outer2.append(p)
    cutout_groups: list[list[list[float]]] = []
    for ref in cutout_refs:
        if not isinstance(ref, str):
            continue
        group: list[list[float]] = []
        for pt in _resolve_points_ref(doc, ref):
            if isinstance(pt, list) and len(pt) >= 2:
                p = [float(pt[0]), float(pt[1]), 0.0]
                if mirror_axis:
                    p = _mirrored_point(p, mirror_axis)
                group.append(p)
        if group:
            cutout_groups.append(group)
    face_rects = _axis_rects_from_cutouts(outer2, cutout_groups)
    half = thickness * 0.5
    for rect in face_rects:
        front = [_transform_point([p[0], p[1], p[2] + half], world_transform) for p in rect]
        back = [_transform_point([p[0], p[1], p[2] - half], world_transform) for p in rect]
        _add_polygon3d(commands, doc, project, front, fill, edge, camera, depth_bias=-0.01, draw_edges=False)
        _add_polygon3d(commands, doc, project, list(reversed(back)), back_fill, edge, camera, depth_bias=0.01, draw_edges=False)
    outer_front = [_transform_point([p[0], p[1], p[2] + half], world_transform) for p in outer2]
    outer_back = [_transform_point([p[0], p[1], p[2] - half], world_transform) for p in outer2]
    for i in range(len(outer_front)):
        j = (i + 1) % len(outer_front)
        quad = [outer_front[i], outer_front[j], outer_back[j], outer_back[i]]
        _add_polygon3d(commands, doc, project, quad, side_fill, edge, camera, draw_edges=False)
    for group in cutout_groups:
        hole_front = [_transform_point([p[0], p[1], p[2] + half], world_transform) for p in group]
        hole_back = [_transform_point([p[0], p[1], p[2] - half], world_transform) for p in group]
        for i in range(len(hole_front)):
            j = (i + 1) % len(hole_front)
            quad = [hole_front[i], hole_front[j], hole_back[j], hole_back[i]]
            _add_polygon3d(commands, doc, project, quad, side_fill, edge, camera, draw_edges=False)


def _render_capsule(commands: list[dict], project, doc: dict, component: dict, world_transform: dict, fill, edge, camera: dict | None):
    anchors = component.get("anchors") if isinstance(component.get("anchors"), dict) else {}
    base = _anchor_obj(anchors.get("base"))
    top = _anchor_obj(anchors.get("top"))
    radius = float(component.get("radius", 0.04) or 0.04)
    if base and top:
        pa = _transform_point(_vec3(base.get("position"), (0.0, -0.5, 0.0)), world_transform)
        pb = _transform_point(_vec3(top.get("position"), (0.0, 0.5, 0.0)), world_transform)
    else:
        length = float(component.get("length", 1.0) or 1.0)
        center = _transform_point([0.0, 0.0, 0.0], world_transform)
        pa = [center[0], center[1] - length * 0.5, center[2]]
        pb = [center[0], center[1] + length * 0.5, center[2]]
    if abs(pb[1] - pa[1]) >= abs(pb[0] - pa[0]):
        rect = [
            [pa[0] - radius, pa[1], pa[2]],
            [pa[0] + radius, pa[1], pa[2]],
            [pb[0] + radius, pb[1], pb[2]],
            [pb[0] - radius, pb[1], pb[2]],
        ]
    else:
        rect = [
            [pa[0], pa[1] - radius, pa[2]],
            [pa[0], pa[1] + radius, pa[2]],
            [pb[0], pb[1] + radius, pb[2]],
            [pb[0], pb[1] - radius, pb[2]],
        ]
    _add_polygon3d(commands, doc, project, rect, fill, edge, camera)


def _render_lobe_chain(commands: list[dict], project, doc: dict, component: dict, world_transform: dict, fill, edge, mirror_axis: str | None, camera: dict | None):
    points_ref = component.get("points_ref")
    if not isinstance(points_ref, str):
        return
    raw = _resolve_points_ref(doc, points_ref)
    anchors = component.get("anchors") if isinstance(component.get("anchors"), dict) else {}
    anchor = _anchor_obj(anchors.get("mid_attach"))
    attach = _vec3(anchor.get("position"), (0.0, 0.0, 0.0)) if anchor else [0.0, 0.0, 0.0]
    if mirror_axis:
        attach = _mirrored_point(attach, mirror_axis)
    attach_world = _transform_point(attach, world_transform)
    tips: list[list[float]] = []
    for pt in raw:
        if isinstance(pt, list) and len(pt) >= 2:
            p = [float(pt[0]), float(pt[1]), 0.0]
            if mirror_axis:
                p = _mirrored_point(p, mirror_axis)
            tips.append(_transform_point(p, world_transform))
    if len(tips) >= 2:
        _add_polygon3d(commands, doc, project, [attach_world] + tips, fill, edge, camera, depth_bias=-0.01)
    for pt in tips:
        (x0, y0), (x1, y1) = _project_points(project, [attach_world, pt], camera)
        commands.append({"op": "line", "x0": x0, "y0": y0, "x1": x1, "y1": y1, "r": edge[0], "g": edge[1], "b": edge[2]})


def _ring_points(component: dict, world_transform: dict) -> list[list[float]]:
    radius_x = float(component.get("radius_x", component.get("radius", 0.3)) or 0.3)
    radius_z = float(component.get("radius_z", component.get("radius", 0.3)) or 0.3)
    segment_count = max(6, int(component.get("segment_count", 12) or 12))
    points: list[list[float]] = []
    for i in range(segment_count):
        t = (math.pi * 2.0 * i) / segment_count
        local = [math.cos(t) * radius_x, 0.0, math.sin(t) * radius_z]
        points.append(_transform_point(local, world_transform))
    return points


def _render_ring_section(commands: list[dict], project, component: dict, world_transform: dict, edge, camera: dict | None):
    points = _ring_points(component, world_transform)
    projected = _project_points(project, points, camera)
    for i in range(len(projected)):
        x0, y0 = projected[i]
        x1, y1 = projected[(i + 1) % len(projected)]
        commands.append({"op": "line", "x0": x0, "y0": y0, "x1": x1, "y1": y1, "r": edge[0], "g": edge[1], "b": edge[2]})


def _render_instance_body(commands: list[dict], project, doc: dict, component_doc: dict, component: dict, instance: dict, fill: tuple[float, float, float], edge: tuple[float, float, float], camera: dict | None):
    world_transform = _compose_transform(instance.get("transform"), component.get("local_transform"))
    primitive = str(component.get("primitive", ""))
    mirror_axis = instance.get("_mirror_axis") if isinstance(instance.get("_mirror_axis"), str) else None
    if primitive == "box":
        _render_box(commands, project, doc, world_transform, component.get("size"), fill, edge, camera)
    elif primitive == "capsule":
        _render_capsule(commands, project, doc, component, world_transform, fill, edge, camera)
    elif primitive == "polygon_fan":
        _render_polygon_fan(commands, project, component_doc, component, world_transform, fill, edge, mirror_axis, camera)
    elif primitive == "thin_shell_from_outline":
        _render_thin_shell_from_outline(commands, project, component_doc, component, world_transform, fill, edge, mirror_axis, camera)
    elif primitive == "wall_shell_with_cutouts":
        _render_wall_shell_with_cutouts(commands, project, component_doc, component, world_transform, fill, edge, mirror_axis, camera)
    elif primitive == "lobe_tip_chain":
        _render_lobe_chain(commands, project, component_doc, component, world_transform, fill, edge, mirror_axis, camera)
    elif primitive == "ring_section":
        _render_ring_section(commands, project, component, world_transform, edge, camera)


def _render_loft_sections(commands: list[dict], project, doc: dict, operation: dict, components: dict, instance_map: dict[str, dict], camera: dict | None, errors: list[str]):
    section_ids = operation.get("section_instances")
    if not isinstance(section_ids, list) or len(section_ids) < 2:
        errors.append("loft_sections requires section_instances with at least two entries")
        return
    section_instances: list[tuple[dict, dict]] = []
    for section_id in section_ids:
        instance = instance_map.get(str(section_id))
        if not isinstance(instance, dict):
            errors.append(f"loft_sections missing instance '{section_id}'")
            return
        component, _ = _resolve_component_ref(doc, str(instance.get("component", "")))
        if not isinstance(component, dict) or component.get("primitive") != "ring_section":
            errors.append(f"loft_sections instance '{section_id}' is not a ring_section")
            return
        section_instances.append((instance, component))
    material_id = str(operation.get("material") or section_instances[0][1].get("material") or "default")
    fill = _color(doc, material_id, (0.42, 0.28, 0.18))
    edge = _darken(fill, 0.55)
    side_fill = _color(doc, str(operation.get("side_material") or material_id), _darken(fill, 0.9))
    cap_start = bool(operation.get("cap_start", True))
    cap_end = bool(operation.get("cap_end", True))
    point_loops: list[list[list[float]]] = []
    for instance, component in section_instances:
        world_transform = _compose_transform(instance.get("transform"), component.get("local_transform"))
        point_loops.append(_ring_points(component, world_transform))
    for i in range(len(point_loops) - 1):
        a = point_loops[i]
        b = point_loops[i + 1]
        segment_count = min(len(a), len(b))
        for j in range(segment_count):
            k = (j + 1) % segment_count
            quad = [a[j], a[k], b[k], b[j]]
            _add_polygon3d(commands, doc, project, quad, side_fill, edge, camera, draw_edges=False)
    if cap_start and point_loops:
        _add_polygon3d(commands, doc, project, list(reversed(point_loops[0])), fill, edge, camera, depth_bias=0.015, draw_edges=False)
    if cap_end and point_loops:
        _add_polygon3d(commands, doc, project, point_loops[-1], fill, edge, camera, depth_bias=-0.015, draw_edges=False)


def _loft_connection_pairs(doc: dict) -> set[tuple[str, str]]:
    out: set[tuple[str, str]] = set()
    operations = doc.get("operations")
    if not isinstance(operations, list):
        return out
    for operation in operations:
        if not isinstance(operation, dict) or operation.get("op") != "loft_sections":
            continue
        section_ids = operation.get("section_instances")
        if not isinstance(section_ids, list):
            continue
        normalized = [str(item) for item in section_ids if isinstance(item, str)]
        for i in range(len(normalized) - 1):
            a = normalized[i]
            b = normalized[i + 1]
            out.add((a, b))
            out.add((b, a))
    return out


def _loft_instance_ids(doc: dict) -> set[str]:
    out: set[str] = set()
    operations = doc.get("operations")
    if not isinstance(operations, list):
        return out
    for operation in operations:
        if not isinstance(operation, dict) or operation.get("op") != "loft_sections":
            continue
        section_ids = operation.get("section_instances")
        if not isinstance(section_ids, list):
            continue
        for item in section_ids:
            if isinstance(item, str):
                out.add(item)
    return out


def build_runtime_preview(doc: dict, camera: dict | None = None) -> tuple[list[dict], list[str]]:
    errors: list[str] = []
    commands: list[dict] = [{"op": "clear", "r": 0.025, "g": 0.03, "b": 0.04}]
    project = _projector(doc)
    components = _component_map(doc)
    preview_instances = _preview_instances(doc)
    preview_instance_map = {
        str(item.get("id", "")): item
        for item in preview_instances
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    loft_pairs = _loft_connection_pairs(doc)
    loft_ids = _loft_instance_ids(doc)

    operations = doc.get("operations")
    if isinstance(operations, list):
        for operation in operations:
            if isinstance(operation, dict) and operation.get("op") == "loft_sections":
                _render_loft_sections(commands, project, doc, operation, components, preview_instance_map, camera, errors)

    for instance in preview_instances:
        component, component_doc = _resolve_component_ref(doc, str(instance.get("component", "")))
        if not isinstance(component, dict):
            errors.append(f"missing component '{instance.get('component')}' for instance '{instance.get('id')}'")
            continue
        if str(instance.get("id", "")) in loft_ids and component.get("primitive") == "ring_section":
            continue
        if component.get("kind") != "primitive":
            continue
        material_id = instance.get("material_override") or component.get("material") or "default"
        fill = _color(component_doc, str(material_id), (0.55, 0.65, 0.72))
        edge = _darken(fill)
        _render_instance_body(commands, project, doc, component_doc, component, instance, fill, edge, camera)
        anchors = component.get("anchors") if isinstance(component.get("anchors"), dict) else {}
        first_anchor = next(iter(anchors.keys()), "")
        label_pos = None
        if first_anchor:
            try:
                label_pos = _resolve_instance_anchor(doc, str(instance.get("id", "")), first_anchor)["world_position"]
            except Exception:
                label_pos = None
        if label_pos is None:
            label_pos = _transform_value(instance.get("transform"), "position", (0.0, 0.0, 0.0))
        lx, ly = project(label_pos, camera)
        commands.append({"op": "text", "x": lx + 10.0, "y": ly - 8.0, "text": str(instance.get("id", "")), "size": 12, "r": 0.84, "g": 0.88, "b": 0.92})

    connections = doc.get("connections")
    if not isinstance(connections, list):
        errors.append("document has no connections list")
        connections = []
    y_text = 38
    for connection in connections:
        if not isinstance(connection, dict):
            errors.append("connection entry is not an object")
            continue
        try:
            a = _resolve_instance_anchor(doc, str(connection.get("from_instance", "")), str(connection.get("from_anchor", "")))
            b = _resolve_instance_anchor(doc, str(connection.get("to_instance", "")), str(connection.get("to_anchor", "")))
        except ValueError as exc:
            errors.append(str(exc))
            continue
        joint = connection.get("joint")
        if not isinstance(joint, dict):
            errors.append(f"connection '{connection.get('id', '?')}' missing joint object")
            continue
        mode = str(joint.get("mode", "unknown"))
        if mode == "rigid" and (a["instance_id"], b["instance_id"]) in loft_pairs:
            continue
        c = _joint_color(mode)
        ax, ay = project(a["world_position"], camera)
        bx, by = project(b["world_position"], camera)
        commands.append({"op": "line", "x0": ax, "y0": ay, "x1": bx, "y1": by, "r": c[0], "g": c[1], "b": c[2]})
        for px, py in ((ax, ay), (bx, by)):
            commands.append({"op": "rect", "x": px - 4.0, "y": py - 4.0, "w": 8.0, "h": 8.0, "r": c[0], "g": c[1], "b": c[2]})
        commands.append({
            "op": "text",
            "x": 26,
            "y": y_text,
            "text": f"{connection.get('id', 'connection')}  {a['instance_id']}.{a['anchor_name']} -> {b['instance_id']}.{b['anchor_name']}  {_range_text(joint)}",
            "size": 14,
            "r": c[0], "g": c[1], "b": c[2],
        })
        y_text += 18

    top_anchors = doc.get("anchors")
    if isinstance(top_anchors, dict):
        for name, value in top_anchors.items():
            anchor = _anchor_obj(value)
            if not anchor:
                continue
            pos = anchor.get("position")
            if not isinstance(pos, list) or len(pos) < 3:
                continue
            px, py = project([float(pos[0]), float(pos[1]), float(pos[2])], camera)
            commands.append({"op": "rect", "x": px - 3.0, "y": py - 3.0, "w": 6.0, "h": 6.0, "r": 0.95, "g": 0.95, "b": 0.95})
            commands.append({"op": "text", "x": px + 8.0, "y": py - 4.0, "text": str(name), "size": 12, "r": 0.86, "g": 0.9, "b": 0.94})

    commands.append({"op": "text", "x": 24, "y": HEIGHT - 48, "text": f"E3D runtime preview: {doc.get('name', 'artifact')}", "size": 18, "r": 0.95, "g": 0.96, "b": 0.98})
    commands.append({"op": "text", "x": 24, "y": HEIGHT - 26, "text": "Pre-mesh component bodies plus anchor interconnects and movement ranges.", "size": 13, "r": 0.67, "g": 0.74, "b": 0.80})

    if errors:
        y = HEIGHT - 120
        commands.append({"op": "text", "x": 24, "y": y, "text": "Validation errors:", "size": 15, "r": 0.96, "g": 0.34, "b": 0.30})
        y += 18
        for error in errors[:8]:
            commands.append({"op": "text", "x": 24, "y": y, "text": error, "size": 13, "r": 0.96, "g": 0.52, "b": 0.48})
            y += 16
    return commands, errors


def load_runtime_preview(path: Path, camera: dict | None = None) -> tuple[dict, list[dict], list[str]]:
    doc = load_e3d_document(path)
    commands, errors = build_runtime_preview(doc, camera)
    return doc, commands, errors
