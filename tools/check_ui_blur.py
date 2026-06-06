#!/usr/bin/env python3
"""Validate optional UI suite screenshots and the cached blur effect."""

from __future__ import annotations

import os
import sys
import csv
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:
    print("ERROR: Pillow is required for UI screenshot validation.")
    raise SystemExit(99) from exc


RENDERERS = ("gl", "dx11", "dx12")
SCENES = (
    "blur_off",
    "blur_on",
    "profiler_default",
    "profiler_hierarchy",
    "profiler_timeline",
    "physics_toggles",
    "scene_options",
    "controls",
    "renderer_combo",
    "scene_complete",
    "small_scroll",
    "minimized",
)
TIMELINE_EPSILON_MS = 0.05


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


def average_marker_ms(path: Path) -> tuple[dict[str, float], list[str]]:
    header: list[str] | None = None
    rows: list[list[str]] = []
    with path.open(newline="") as handle:
        reader = csv.reader(handle)
        for row in reader:
            if not row or row[0].startswith("#"):
                continue
            if row[0] == "pass":
                header = row
                continue
            if header is not None:
                rows.append(row)

    if header is None or not rows:
        raise ValueError(f"{path.name} has no profiler rows")

    tail = rows[-min(30, len(rows)) :]
    values: dict[str, float] = {}
    marker_order: list[str] = []
    for column, name in enumerate(header[2:], start=2):
        if name.endswith("_gpu"):
            continue
        total = 0.0
        count = 0
        for row in tail:
            if column < len(row):
                try:
                    total += float(row[column])
                    count += 1
                except ValueError:
                    pass
        if count:
            values[name] = total / count
            marker_order.append(name)
    return values, marker_order


def validate_timeline_csv(path: Path, renderer: str) -> int:
    marker_ms, marker_order = average_marker_ms(path)
    if "Frame/PipelineSync" in marker_ms:
        print(f"ERROR: {renderer} timeline CSV still contains Frame/PipelineSync.")
        return 1
    required_markers = ("Frame/UI", "Frame/UI/Quads", "Frame/UI/Text")
    for marker in required_markers:
        if marker not in marker_ms:
            print(f"ERROR: {renderer} timeline CSV is missing {marker}.")
            return 1
        if marker_ms[marker] <= 0.0:
            print(f"ERROR: {renderer} {marker} marker did not record positive time.")
            return 1
    print(
        f"{renderer}: Frame/UI={marker_ms['Frame/UI']:.4f}ms "
        f"Quads={marker_ms['Frame/UI/Quads']:.4f}ms "
        f"Text={marker_ms['Frame/UI/Text']:.4f}ms"
    )

    children: dict[str | None, list[str]] = {None: []}
    for name in marker_order:
        slash = name.rfind("/")
        parent = name[:slash] if slash >= 0 and name[:slash] in marker_ms else None
        children.setdefault(parent, []).append(name)
        children.setdefault(name, [])

    filled_segments: list[tuple[float, float, str]] = []
    failures = 0

    def assign(name: str, start_ms: float) -> None:
        nonlocal failures
        duration_ms = max(0.0, marker_ms.get(name, 0.0))
        direct_children = children.get(name, [])
        if direct_children:
            cursor = start_ms
            for child in direct_children:
                assign(child, cursor)
                cursor += max(0.0, marker_ms.get(child, 0.0))
            child_total = cursor - start_ms
            if child_total > duration_ms + TIMELINE_EPSILON_MS:
                print(f"ERROR: {renderer} child timeline exceeds {name}: children={child_total:.4f}ms parent={duration_ms:.4f}ms")
                failures += 1
        elif duration_ms > 0.0:
            filled_segments.append((start_ms, start_ms + duration_ms, name))

    cursor = 0.0
    for root in children.get(None, []):
        assign(root, cursor)
        cursor += max(0.0, marker_ms.get(root, 0.0))

    filled_segments.sort(key=lambda item: (item[0], item[1], item[2]))
    previous_end = 0.0
    for start_ms, end_ms, name in filled_segments:
        if start_ms < previous_end - TIMELINE_EPSILON_MS:
            print(f"ERROR: {renderer} timeline overlap at {name}: start={start_ms:.4f}ms previous_end={previous_end:.4f}ms")
            failures += 1
        previous_end = max(previous_end, end_ms)

    print(f"{renderer}: timeline segments={len(filled_segments)} max_end={previous_end:.4f}ms")
    return failures


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

    for renderer in RENDERERS:
        try:
            failures += validate_timeline_csv(profile / f"ui_{renderer}_profiler_timeline_perf.csv", renderer)
        except (OSError, ValueError) as exc:
            print(f"ERROR: {renderer} timeline numeric validation failed: {exc}")
            failures += 1

    if failures:
        print(f"UI screenshot validation failed with {failures} issue(s).")
        return 2

    print("UI screenshot validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
