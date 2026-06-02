#!/usr/bin/env python3
"""Generate shared Vulkan surface SPIR-V from the proven texture-capable builder."""

from __future__ import annotations

import importlib.util
import pathlib
import sys


def _load_builder():
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    builder_path = repo_root / "tools" / "build_spirv_texture_dat.py"
    spec = importlib.util.spec_from_file_location("build_spirv_texture_dat", builder_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load builder from {builder_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: gen_vulkan_spirv.py <out-path>", file=sys.stderr)
        return 2

    out_path = pathlib.Path(argv[1]).expanduser()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    module = _load_builder()
    out_path.write_bytes(module.build())
    print(str(out_path))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
