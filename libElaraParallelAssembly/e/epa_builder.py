#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_project_path(project_root: Path, value: str) -> Path:
    p = Path(value)
    if p.is_absolute():
        return p
    return project_root / p


def target_list(config: dict) -> list:
    targets = config.get("targets")
    if isinstance(targets, list):
        return targets
    legacy = config.get("epa")
    if isinstance(legacy, list):
        return [{"name": "default", "out": "build/epa.bin", "files": legacy}]
    raise ValueError("epa_config.json must contain a targets array")


def build_target(repo_root: Path, project_root: Path, e2epabin: Path, target: dict) -> None:
    name = str(target.get("name") or "")
    out = target.get("out")
    files = target.get("files")
    env = target.get("env") or {}

    if not name:
        raise ValueError("target is missing name")
    if not isinstance(out, str) or not out:
        raise ValueError(f"target {name} is missing out")
    if not isinstance(files, list) or not files:
        raise ValueError(f"target {name} must list files")
    if not isinstance(env, dict):
        raise ValueError(f"target {name} env must be an object")

    out_path = resolve_project_path(project_root, out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    cmd = [str(e2epabin), "--out", str(out_path)]
    for key in sorted(env.keys()):
        value = env[key]
        cmd.extend(["--env", f"{key}={value}"])
    for item in files:
        if not isinstance(item, str) or not item:
            raise ValueError(f"target {name} has an invalid file entry")
        cmd.append(str(resolve_project_path(project_root, item)))

    print(f"[epa-builder] {name} -> {out_path}")
    subprocess.run(cmd, cwd=str(repo_root), check=True)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Build EPA targets from epa_config.json")
    parser.add_argument("--project", default=".", help="project root containing epa_config.json")
    parser.add_argument("--config", default=None, help="config path, default: <project>/epa_config.json")
    parser.add_argument("--target", action="append", help="target name to build; may be repeated")
    parser.add_argument("--e2epabin", default=None, help="path to e2epabin")
    args = parser.parse_args(argv)

    project_root = Path(args.project).resolve()
    config_path = Path(args.config).resolve() if args.config else project_root / "epa_config.json"
    if not config_path.exists():
        raise FileNotFoundError(f"missing EPA config: {config_path}")

    repo_root = repo_root_from_script()
    e2epabin = Path(args.e2epabin).resolve() if args.e2epabin else repo_root / "libElaraParallelAssembly" / "build" / "e" / "e2epabin"
    if not e2epabin.exists():
        raise FileNotFoundError(f"missing e2epabin: {e2epabin}")

    with config_path.open("r", encoding="utf-8") as f:
        config = json.load(f)

    selected = set(args.target or [])
    built = 0
    built_names = set()
    for target in target_list(config):
        name = str(target.get("name") or "")
        if selected and name not in selected:
            continue
        build_target(repo_root, project_root, e2epabin, target)
        built += 1
        built_names.add(name)

    if selected and built_names != selected:
        missing = sorted(selected - built_names)
        raise ValueError(f"requested target(s) not found or not built: {', '.join(missing)}")
    if built == 0:
        raise ValueError("no EPA targets built")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except Exception as exc:
        print(f"epa-builder: {exc}", file=sys.stderr)
        raise SystemExit(1)
