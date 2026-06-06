#!/usr/bin/env python3
"""Convert an engine BMP screenshot to a shareable PNG."""

from __future__ import annotations

import argparse
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:
    print("ERROR: Pillow is required for screenshot PNG export.")
    raise SystemExit(99) from exc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Source BMP screenshot")
    parser.add_argument("output", type=Path, help="Destination PNG")
    parser.add_argument("--max-width", type=int, default=1080, help="Downscale to this width when larger; 0 disables")
    parser.add_argument("--max-height", type=int, default=0, help="Downscale to this height when larger; 0 disables")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.input.exists():
        print(f"ERROR: Input screenshot does not exist: {args.input}")
        return 1

    with Image.open(args.input) as source:
        image = source.convert("RGB")
        width, height = image.size
        scale = 1.0
        if args.max_width > 0 and width > args.max_width:
            scale = min(scale, args.max_width / float(width))
        if args.max_height > 0 and height > args.max_height:
            scale = min(scale, args.max_height / float(height))
        if scale < 1.0:
            new_size = (max(1, int(width * scale)), max(1, int(height * scale)))
            try:
                resample = Image.Resampling.LANCZOS
            except AttributeError:
                resample = Image.LANCZOS
            image = image.resize(new_size, resample)

        args.output.parent.mkdir(parents=True, exist_ok=True)
        image.save(args.output, "PNG", optimize=True)
        out_w, out_h = image.size

    print(f"PNG exported: {args.output} ({out_w}x{out_h})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
