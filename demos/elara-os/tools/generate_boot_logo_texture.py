#!/usr/bin/env python3
import argparse
from pathlib import Path

from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate an EPA texture include from the Elara boot PNG.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--size", type=int, default=40)
    args = parser.parse_args()

    src = Path(args.input)
    dst = Path(args.output)
    size = max(1, min(int(args.size), 64))

    image = Image.open(src).convert("RGB")
    image.thumbnail((size, size), Image.Resampling.LANCZOS)

    canvas = Image.new("RGB", (size, size), (0, 0, 0))
    x = (size - image.width) // 2
    y = (size - image.height) // 2
    canvas.paste(image, (x, y))

    dst.parent.mkdir(parents=True, exist_ok=True)
    with dst.open("w", encoding="utf-8") as out:
        out.write("// Generated from logo.png by tools/generate_boot_logo_texture.py.\n")
        out.write(f"  frame_texture_begin({size}, {size});\n")
        for r, g, b in canvas.getdata():
            out.write(f"  frame_texture_pixel({(r << 16) | (g << 8) | b});\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
