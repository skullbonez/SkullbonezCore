# Purpose:
#   Documents and runs the check_ui_blur.py developer/validation helper script.
#
# Concept:
#   Tools are command-line guardrails around builds, validation, screenshots,
#   diagnostics, and artifact handling. They make the safe path repeatable and
#   keep output bounded for humans and agents.

# Invariants:
#   - Tool output should be bounded and readable because agents and humans use
#   it for decisions.

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


RENDERERS = ("dx12",)
SCENES = (
    "blur_off",
    "blur_on",
    "blur_moved_off",
    "blur_moved_on",
    "profiler_default",
    "profiler_hierarchy",
    "profiler_timeline",
    "physics_toggles",
    "scene_options",
    "controls",
    "renderer_combo",
    "water_combo",
    "scene_complete",
    "small_scroll",
    "controls_clip_scroll",
    "controls_bottom",
    "controls_bottom_bg",
    "min_size",
    "min_size_bg",
    "minimized",
    "performance_histogram",
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


def image_delta(path_a: Path, path_b: Path) -> tuple[int, float]:
    with Image.open(path_a) as image_a, Image.open(path_b) as image_b:
        a = image_a.convert("RGB")
        b = image_b.convert("RGB")
        if a.size != b.size:
            raise ValueError(f"{path_a.name} and {path_b.name} sizes differ")

        pixels_a = a.load()
        pixels_b = b.load()
        width, height = a.size
        changed = 0
        total = 0
        for y in range(height):
            for x in range(width):
                ar, ag, ab = pixels_a[x, y]
                br, bg, bb = pixels_b[x, y]
                delta = abs(ar - br) + abs(ag - bg) + abs(ab - bb)
                total += delta
                if delta > 42:
                    changed += 1
        mean = total / max(1, width * height * 3)
        return changed, mean


def changed_pixels_outside_window(
    ui_path: Path,
    background_path: Path,
    window: tuple[int, int, int, int],
    pad: int = 14,
) -> int:
    with Image.open(ui_path) as ui_image, Image.open(background_path) as background_image:
        ui = ui_image.convert("RGB")
        background = background_image.convert("RGB")
        if ui.size != background.size:
            raise ValueError(f"{ui_path.name} and {background_path.name} sizes differ")

        wx, wy, ww, wh = window
        x0 = wx - pad
        y0 = wy - pad
        x1 = wx + ww + pad
        y1 = wy + wh + pad
        ui_pixels = ui.load()
        bg_pixels = background.load()
        width, height = ui.size
        changed = 0
        suite_hud_x0 = max(0, width - 220)
        suite_hud_y1 = min(height, 70)
        for y in range(height):
            inside_y = y0 <= y <= y1
            for x in range(width):
                if inside_y and x0 <= x <= x1:
                    continue
                # Why: test-suite progress text is drawn by the runner outside
                # the window under test; containment should measure UI leakage.
                if y <= suite_hud_y1 and x >= suite_hud_x0:
                    continue
                ur, ug, ub = ui_pixels[x, y]
                br, bg, bb = bg_pixels[x, y]
                if abs(ur - br) + abs(ug - bg) + abs(ub - bb) > 42:
                    changed += 1
        return changed


def yellow_pixels_in_box(path: Path, box: tuple[int, int, int, int]) -> int:
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        x0, y0, x1, y1 = box
        width, height = rgb.size
        x0 = max(0, min(width, x0))
        y0 = max(0, min(height, y0))
        x1 = max(x0, min(width, x1))
        y1 = max(y0, min(height, y1))
        pixels = rgb.load()
        yellow = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                r, g, b = pixels[x, y]
                if r > 200 and g > 140 and 40 < b < 150 and r - g < 90:
                    yellow += 1
        return yellow


def neutral_bright_pixels_in_box(path: Path, box: tuple[int, int, int, int]) -> int:
    """Count light, low-saturation glyph pixels in a label-only UI region."""
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        x0, y0, x1, y1 = box
        width, height = rgb.size
        x0 = max(0, min(width, x0))
        y0 = max(0, min(height, y0))
        x1 = max(x0, min(width, x1))
        y1 = max(y0, min(height, y1))
        pixels = rgb.load()
        count = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                red, green, blue = pixels[x, y]
                if red + green + blue >= 450 and max(red, green, blue) - min(red, green, blue) < 55:
                    count += 1
        return count



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
        if name.endswith("_gpu") or name == "Frame/VsyncWait":
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
    required_markers = ("Frame/UI", "Frame/UI/DrawBuild", "Frame/UI/Blur", "Frame/UI/Draw")
    for marker in required_markers:
        if marker not in marker_ms:
            print(f"ERROR: {renderer} timeline CSV is missing {marker}.")
            return 1
        if marker_ms[marker] <= 0.0:
            print(f"ERROR: {renderer} {marker} marker did not record positive time.")
            return 1
    print(
        f"{renderer}: Frame/UI={marker_ms['Frame/UI']:.4f}ms "
        f"DrawBuild={marker_ms['Frame/UI/DrawBuild']:.4f}ms "
        f"Blur={marker_ms['Frame/UI/Blur']:.4f}ms "
        f"Draw={marker_ms['Frame/UI/Draw']:.4f}ms"
    )

    children: dict[str | None, list[str]] = {None: []}

    def nearest_recorded_parent(name: str) -> str | None:
        slash = name.rfind("/")
        while slash >= 0:
            candidate = name[:slash]
            if candidate in marker_ms:
                return candidate
            slash = candidate.rfind("/")
        return None

    for name in marker_order:
        parent = nearest_recorded_parent(name)
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
    moved_blur_sample_box = (336, 320, 924, 520)
    for renderer in RENDERERS:
        off_path = profile / f"ui_{renderer}_blur_off.bmp"
        on_path = profile / f"ui_{renderer}_blur_on.bmp"
        off_score = edge_score(off_path, blur_sample_box)
        on_score = edge_score(on_path, blur_sample_box)
        changed, mean_delta = image_delta(off_path, on_path)
        print(
            f"{renderer}: blur edge score off={off_score:.3f} on={on_score:.3f}; "
            f"changed={changed} mean_delta={mean_delta:.3f}"
        )
        if changed < 5000 or mean_delta < 0.10:
            print(f"ERROR: {renderer} blur toggle did not create a measurable screenshot change.")
            failures += 1

        moved_off_path = profile / f"ui_{renderer}_blur_moved_off.bmp"
        moved_on_path = profile / f"ui_{renderer}_blur_moved_on.bmp"
        moved_off_score = edge_score(moved_off_path, moved_blur_sample_box)
        moved_on_score = edge_score(moved_on_path, moved_blur_sample_box)
        moved_changed, moved_mean_delta = image_delta(moved_off_path, moved_on_path)
        print(
            f"{renderer}: moved blur edge score off={moved_off_score:.3f} on={moved_on_score:.3f}; "
            f"changed={moved_changed} mean_delta={moved_mean_delta:.3f}"
        )
        if moved_changed < 5000 or moved_mean_delta < 0.10:
            print(f"ERROR: {renderer} moved blur toggle did not create a measurable screenshot change.")
            failures += 1

    for renderer in RENDERERS:
        for scene in SCENES:
            path = profile / f"ui_{renderer}_{scene}.bmp"
            span = brightness_span(path)
            print(f"{renderer}/{scene}: brightness span={span:.1f}")
            if span < 35.0:
                print(f"ERROR: {path.name} looks too flat or blank.")
                failures += 1

    # Regression: a failed text shader used to return startup success and turn
    # every glyph draw into a no-op while colorful panels kept the generic
    # brightness checks green. These boxes contain title/body labels but no
    # slider knobs or window buttons, so both must retain visible glyph ink.
    text_presence_boxes = (
        ("window title", (35, 12, 330, 52), 200),
        ("controls labels", (35, 105, 150, 450), 300),
    )
    for renderer in RENDERERS:
        controls_path = profile / f"ui_{renderer}_controls.bmp"
        for label, box, minimum in text_presence_boxes:
            try:
                pixels = neutral_bright_pixels_in_box(controls_path, box)
            except (OSError, ValueError) as exc:
                print(f"ERROR: {renderer} UI text-presence check failed: {exc}")
                failures += 1
                continue
            print(f"{renderer}/controls: {label} glyph pixels={pixels}")
            if pixels < minimum:
                print(
                    f"ERROR: {renderer}/controls has no visible {label} text "
                    f"(pixels={pixels}, required={minimum})."
                )
                failures += 1

    for renderer in RENDERERS:
        try:
            failures += validate_timeline_csv(profile / f"ui_{renderer}_profiler_timeline_perf.csv", renderer)
        except (OSError, ValueError) as exc:
            print(f"ERROR: {renderer} timeline numeric validation failed: {exc}")
            failures += 1

    window_guards = (
        ("controls_bottom", "controls_bottom_bg", (64, 70, 520, 280)),
        ("min_size", "min_size_bg", (64, 70, 520, 250)),
    )
    for renderer in RENDERERS:
        for scene, background, window in window_guards:
            try:
                changed = changed_pixels_outside_window(
                    profile / f"ui_{renderer}_{scene}.bmp",
                    profile / f"ui_{renderer}_{background}.bmp",
                    window,
                )
            except (OSError, ValueError) as exc:
                print(f"ERROR: {renderer}/{scene} window containment check failed: {exc}")
                failures += 1
                continue
            print(f"{renderer}/{scene}: outside-window changed pixels={changed}")
            if changed > 90:
                print(f"ERROR: {renderer}/{scene} appears to draw UI outside the expected window bounds.")
                failures += 1

    content_clip_band = (74, 156, 486, 180)
    for renderer in RENDERERS:
        try:
            yellow = yellow_pixels_in_box(
                profile / f"ui_{renderer}_controls_clip_scroll.bmp",
                content_clip_band,
            )
        except (OSError, ValueError) as exc:
            print(f"ERROR: {renderer}/controls_clip_scroll content clipping check failed: {exc}")
            failures += 1
            continue
        print(f"{renderer}/controls_clip_scroll: pre-content yellow pixels={yellow}")
        if yellow > 0:
            print(f"ERROR: {renderer}/controls_clip_scroll appears to leak scrolled content above the viewport.")
            failures += 1

    if failures:
        print(f"UI screenshot validation failed with {failures} issue(s).")
        return 2

    print("UI screenshot validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
