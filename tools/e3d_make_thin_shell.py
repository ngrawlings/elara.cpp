#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


def _load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _save(path: Path, doc: dict) -> None:
    path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")


def build_thin_shell_component(source: dict, points_ref: str, thickness: float, side_material: str | None, back_material: str | None) -> dict:
    component = dict(source)
    component["primitive"] = "thin_shell_from_outline"
    component["points_ref"] = points_ref
    component["thickness"] = thickness
    if side_material:
        component["side_material"] = side_material
    if back_material:
        component["back_material"] = back_material
    return component


def build_wall_shell_component(source: dict, points_ref: str, cutout_refs: list[str], thickness: float, side_material: str | None, back_material: str | None) -> dict:
    component = dict(source)
    component["primitive"] = "wall_shell_with_cutouts"
    component["points_ref"] = points_ref
    component["cutout_refs"] = list(cutout_refs)
    component["thickness"] = thickness
    if side_material:
        component["side_material"] = side_material
    if back_material:
        component["back_material"] = back_material
    return component


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert a 2D outline-driven E3D component into a paper-thin shell")
    parser.add_argument("artifact", help="Path to .e3d.json artifact")
    parser.add_argument("--component", required=True, help="Component id to convert")
    parser.add_argument("--points-ref", help="Override points_ref path; defaults to the component's current points_ref")
    parser.add_argument("--cutout-ref", action="append", default=[], help="Optional cutout points_ref entries for wall-shell conversion")
    parser.add_argument("--thickness", type=float, default=0.001, help="Shell thickness in artifact units")
    parser.add_argument("--side-material", help="Optional material id for the shell walls")
    parser.add_argument("--back-material", help="Optional material id for the back face")
    parser.add_argument("--write", action="store_true", help="Write the converted component back into the artifact")
    args = parser.parse_args()

    path = Path(args.artifact)
    doc = _load(path)
    components = doc.get("components")
    if not isinstance(components, dict):
        raise SystemExit("artifact has no components object")
    component = components.get(args.component)
    if not isinstance(component, dict):
        raise SystemExit(f"component '{args.component}' not found")
    points_ref = args.points_ref or component.get("points_ref")
    if not isinstance(points_ref, str) or not points_ref:
        raise SystemExit("component has no points_ref; pass --points-ref")

    if args.cutout_ref:
        converted = build_wall_shell_component(component, points_ref, list(args.cutout_ref), float(args.thickness), args.side_material, args.back_material)
    else:
        converted = build_thin_shell_component(component, points_ref, float(args.thickness), args.side_material, args.back_material)
    if args.write:
        components[args.component] = converted
        _save(path, doc)
        print(json.dumps({"artifact": str(path), "component": args.component, "written": True}, indent=2))
    else:
        print(json.dumps(converted, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
