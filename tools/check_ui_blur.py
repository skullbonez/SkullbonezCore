#!/usr/bin/env python3
"""Validate optional UI suite screenshots and the cached blur effect."""

from __future__ import annotations

import os
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:
    print("ERROR: Pillow is required for UI screenshot validation.")
    raise SystemExit(99) from exc


RENDERERS = ("gl", "dx11", "dx12")
SCENES = ("blur_off", "blur_on", "profiler_hierarchy", "renderer_combo", "small_scroll")


def edge_score(path: Path, box: tuple[int, int, int, int]) -> float:
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        x0, y0, x1, y1 = box
        width, height = rgb.size
        x0 = max(0, min(width - 2, x0))
        y0 = max(0, min(height - 2, y0))
        x1 = max(x0 + 2, min(width, x1))
        y1 = max(y0 + 2, min(height, y1))
        pixels = rgb.load()
        total = 0
        count = 0
        for y in range(y0, y1 - 1):
            for x in range(x0, x1 - 1):
                r, g, b = pixels[x, y]
                rr, gg, bb = pixels[x + 1, y]
                rd, gd, bd = pixels[x, y + 1]
                total += abs(r - rr) + abs(g - gg) + abs(b - bb)
                total += abs(r - rd) + abs(g - gd) + abs(b - bd)
                count += 6
        return total / max(1, count)


def brightness_span(path: Path) -> float:
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        raw = rgb.tobytes()
        if not raw:
            return 0.0
        pixel_step = max(1, (len(raw) // 3) // 12000)
        byte_step = pixel_step * 3
        lumas = []
        for i in range( 0, len( raw ) - 2, byte_step ):
            r = raw[i]
            g = raw[i + 1]
            b = raw[i + 2]
            lumas.append( 0.2126 * r + 0.7152 * g + 0.0722 * b )
        return max(lumas) - min(lumas)


def main() -> int:
    repo = Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1]))
    profile = repo / "Profile"

    missing: list[Path] = []
    for renderer in RENDERERS:
        for scene in SCENES:
            path = profile / f"ui_{renderer}_{scene}.bmp"
            if not path.exists():
                missing.append(path)

    if missing:
        print("ERROR: Missing UI screenshot artifacts:")
        for path in missing:
            print(f"  {path}")
        return 1

    failures = 0
    blur_sample_box = (92, 350, 770, 490)
    for renderer in RENDERERS:
        off_path = profile / f"ui_{renderer}_blur_off.bmp"
        on_path = profile / f"ui_{renderer}_blur_on.bmp"
        off_score = edge_score(off_path, blur_sample_box)
        on_score = edge_score(on_path, blur_sample_box)
        ratio = on_score / off_score if off_score > 0.001 else 1.0
        print(f"{renderer}: blur edge score off={off_score:.3f} on={on_score:.3f} ratio={ratio:.3f}")
        if ratio >= 0.92:
            print(f"ERROR: {renderer} blur did not soften the checker backdrop enough.")
            failures += 1

    for renderer in RENDERERS:
        for scene in SCENES:
            path = profile / f"ui_{renderer}_{scene}.bmp"
            span = brightness_span(path)
            print(f"{renderer}/{scene}: brightness span={span:.1f}")
            if span < 35.0:
                print(f"ERROR: {path.name} looks too flat or blank.")
                failures += 1

    if failures:
        print(f"UI screenshot validation failed with {failures} issue(s).")
        return 2

    print("UI screenshot validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
