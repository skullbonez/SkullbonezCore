#
# File: tools/check_dx12_baselines.py
# Purpose:
#   Compares DX12 renderer screenshots against committed DX12 baselines.
#
# Summary:
#   Tools are command-line guardrails around builds, validation, screenshots,
#   diagnostics, and artifact handling. They make the safe path repeatable and
#   keep output bounded for humans and agents.
#
# Glossary:
#   Baseline: Committed reference artifact used to detect visual regression.
#   Manifest: JSON record describing validation inputs, outputs, and artifacts.
#   Validation gate: Repository script that proves a class of changes before
#   commit or PR.
#
# Invariants:
#   - Tool output should be bounded and readable because agents and humans use
#   it for decisions.
#   - A screenshot regression must fail without requiring GL or DX11.
#
# Related:
#   - tools/validate_dx12_renderer.bat
#   - TestOutput/baselines/
#   - AGENTS.md
#
#!/usr/bin/env python3
"""
DX12 screenshot regression check.

Compares the current DX12 Profile screenshots against committed DX12 baselines.
Writes a validation manifest under:
    TestOutput/validation/dx12_renderer/<run-id>/

Exit 0 = DX12 screenshots match baseline within threshold.
Exit 1 = regression, missing artifact, or invalid baseline.
Exit 99 = missing Pillow.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from PIL import Image, ImageChops
except ImportError:
    print("ERROR: Pillow not installed. Run: py -m pip install Pillow")
    sys.exit(99)


DEFAULT_THRESHOLD = 1.0

SCENES = [
    {
        "name": "water_ball_test",
        "scene": "SkullbonezData/scenes/water_ball_test.scene.json",
        "current": "dx12_screenshot.bmp",
        "baseline": "baseline_dx12_water_ball_test.png",
    },
    {
        "name": "solver_smoke",
        "scene": "SkullbonezData/scenes/solver_smoke.scene.json",
        "current": "dx12_solver_smoke.bmp",
        "baseline": "baseline_dx12_solver_smoke.png",
    },
    {
        "name": "space_three_body",
        "scene": "SkullbonezData/scenes/three_body_chaos.scene.json",
        "current": "dx12_space_three_body.bmp",
        "baseline": "baseline_dx12_space_three_body.png",
    },
]

DX12_COMMAND = r"Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --suite SkullbonezData/scenes/render_tests.suite.json"


def run_id() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def repo_relative(repo: Path, path: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo.resolve())).replace("/", "\\")
    except ValueError:
        return str(path)


def read_scene_metadata(repo: Path, scene_path: Path) -> dict[str, Any]:
    metadata: dict[str, Any] = {
        "scene": repo_relative(repo, scene_path),
        "screenshot": None,
        "frames": None,
        "cameras": [],
    }
    if not scene_path.exists():
        metadata["missing"] = True
        return metadata

    try:
        scene = json.loads(scene_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        metadata["invalid"] = str(exc)
        return metadata

    if not isinstance(scene, dict):
        metadata["invalid"] = "root is not an object"
        return metadata

    playback = scene.get("playback")
    if isinstance(playback, dict) and isinstance(playback.get("frames"), int):
        metadata["frames"] = playback["frames"]

    capture = scene.get("capture")
    if isinstance(capture, dict):
        screenshot = capture.get("screenshot")
        if isinstance(screenshot, dict) and isinstance(screenshot.get("path"), str):
            trigger = "frame" if "frame" in screenshot else "ms" if "ms" in screenshot else None
            value = screenshot.get(trigger) if trigger else None
            if isinstance(value, int):
                metadata["screenshot"] = {
                    "path": screenshot["path"],
                    "trigger": trigger,
                    "value": value,
                }
        screenshot_and_exit = capture.get("screenshotAndExit")
        if metadata["screenshot"] is None and isinstance(screenshot_and_exit, str):
            metadata["screenshot"] = {
                "path": screenshot_and_exit,
                "trigger": "frame",
                "value": metadata["frames"] or 0,
            }

    cameras = scene.get("cameras")
    if isinstance(cameras, list):
        for camera in cameras:
            if isinstance(camera, dict) and isinstance(camera.get("name"), str):
                metadata["cameras"].append(camera["name"])
    return metadata


def save_png(source: Path, target: Path) -> tuple[int, int]:
    image = Image.open(source).convert("RGB")
    image.save(target)
    return image.size


def make_side_by_side(a_img: Image.Image, b_img: Image.Image) -> Image.Image:
    composite = Image.new("RGB", (a_img.width + b_img.width, max(a_img.height, b_img.height)), (0, 0, 0))
    composite.paste(a_img, (0, 0))
    composite.paste(b_img, (a_img.width, 0))
    return composite


def make_amplified(diff_img: Image.Image, factor: int) -> Image.Image:
    return diff_img.point(lambda value: min(value * factor, 255))


def diff_stats(diff_img: Image.Image) -> tuple[float, int, int]:
    data = diff_img.tobytes()
    total = sum(data)
    avg = total / max(1, len(data))
    max_diff = 0
    pixels_over_10 = 0
    for index in range(0, len(data), 3):
        pixel_max = max(data[index], data[index + 1], data[index + 2])
        max_diff = max(max_diff, pixel_max)
        if pixel_max > 10:
            pixels_over_10 += 1
    return avg, max_diff, pixels_over_10


def compare_scene(repo: Path, out_dir: Path, scene: dict[str, str], threshold: float) -> dict[str, Any]:
    name = scene["name"]
    current_path = repo / "Profile" / scene["current"]
    baseline_path = repo / "TestOutput" / "baselines" / scene["baseline"]
    scene_path = repo / scene["scene"]

    result: dict[str, Any] = {
        "scene": name,
        "renderer": "dx12",
        "threshold": threshold,
        "sceneMetadata": read_scene_metadata(repo, scene_path),
        "current": repo_relative(repo, current_path),
        "baseline": repo_relative(repo, baseline_path),
    }

    missing = []
    if not current_path.exists():
        missing.append(repo_relative(repo, current_path))
    if not baseline_path.exists():
        missing.append(repo_relative(repo, baseline_path))
    if missing:
        result.update({"status": "missing", "missing": missing})
        return result

    current_img = Image.open(current_path).convert("RGB")
    baseline_img = Image.open(baseline_path).convert("RGB")
    if current_img.size != baseline_img.size:
        result.update(
            {
                "status": "size_mismatch",
                "currentSize": list(current_img.size),
                "baselineSize": list(baseline_img.size),
            }
        )
        return result

    current_png = out_dir / f"{name}_dx12_current.png"
    baseline_png = out_dir / f"{name}_dx12_baseline.png"
    side_by_side = out_dir / f"{name}_dx12_baseline_vs_current.png"
    heatmap = out_dir / f"{name}_dx12_diff.png"
    amplified = out_dir / f"{name}_dx12_diff_x8.png"

    current_img.save(current_png)
    baseline_img.save(baseline_png)
    diff_img = ImageChops.difference(baseline_img, current_img)
    make_side_by_side(baseline_img, current_img).save(side_by_side)
    diff_img.save(heatmap)
    make_amplified(diff_img, 8).save(amplified)

    average_diff, max_diff, pixels_over_10 = diff_stats(diff_img)
    status = "pass" if average_diff <= threshold else "fail"
    result.update(
        {
            "status": status,
            "averageDiff": round(average_diff, 4),
            "maxDiff": max_diff,
            "pixelsOver10": pixels_over_10,
            "size": [current_img.width, current_img.height],
            "artifacts": {
                "current": repo_relative(repo, current_png),
                "baseline": repo_relative(repo, baseline_png),
                "sideBySide": repo_relative(repo, side_by_side),
                "heatmap": repo_relative(repo, heatmap),
                "amplifiedHeatmap": repo_relative(repo, amplified),
            },
        }
    )
    return result


def copy_dx12_validation_log(repo: Path, out_dir: Path) -> str | None:
    source = repo / "dx12_validation.txt"
    if not source.exists():
        return None
    target = out_dir / "dx12_validation.txt"
    shutil.copy2(source, target)
    return repo_relative(repo, target)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def print_summary(repo: Path, manifest_path: Path, summary_path: Path, results: list[dict[str, Any]]) -> None:
    print(f"DX12 artifact manifest: {repo_relative(repo, manifest_path)}")
    print(f"DX12 comparison summary: {repo_relative(repo, summary_path)}")
    print("DX12 baseline comparisons:")
    for item in results:
        scene = item["scene"]
        status = str(item["status"]).upper()
        if item.get("averageDiff") is None:
            print(f"  {scene}: {status}")
            continue
        print(
            f"  {scene}: avg_diff={item['averageDiff']:.4f} "
            f"max_diff={item['maxDiff']} pixels_over_10={item['pixelsOver10']} [{status}]"
        )
        artifacts = item.get("artifacts", {})
        if artifacts.get("sideBySide"):
            print(f"    side_by_side: {artifacts['sideBySide']}")
        if artifacts.get("heatmap"):
            print(f"    heatmap: {artifacts['heatmap']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1])))
    parser.add_argument("--out-root", type=Path, default=None)
    parser.add_argument("--run-id", default=os.environ.get("SKORE_VALIDATION_RUN_ID") or run_id())
    parser.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD)
    args = parser.parse_args()

    repo = args.repo.resolve()
    out_root = args.out_root.resolve() if args.out_root else repo / "TestOutput" / "validation" / "dx12_renderer"
    out_dir = out_root / args.run_id
    out_dir.mkdir(parents=True, exist_ok=True)

    results = [compare_scene(repo, out_dir, scene, args.threshold) for scene in SCENES]
    overall_status = "pass" if all(item.get("status") == "pass" for item in results) else "fail"

    summary_path = out_dir / "summary.json"
    manifest_path = out_dir / "manifest.json"
    dx12_log = copy_dx12_validation_log(repo, out_dir)
    generated_at = datetime.now(timezone.utc).isoformat()

    summary = {
        "runId": args.run_id,
        "generatedAtUtc": generated_at,
        "renderer": "dx12",
        "threshold": args.threshold,
        "status": overall_status,
        "comparisons": results,
    }
    manifest = {
        "runId": args.run_id,
        "generatedAtUtc": generated_at,
        "renderer": "dx12",
        "suite": "SkullbonezData/scenes/render_tests.suite.json",
        "command": DX12_COMMAND,
        "threshold": args.threshold,
        "status": overall_status,
        "scenes": results,
        "summary": repo_relative(repo, summary_path),
        "dx12ValidationLog": dx12_log,
    }

    write_json(summary_path, summary)
    write_json(manifest_path, manifest)
    print_summary(repo, manifest_path, summary_path, results)

    if overall_status == "pass":
        print("PASS: DX12 screenshots match committed baselines.")
        return 0

    print("FAIL: DX12 screenshot regression detected.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
