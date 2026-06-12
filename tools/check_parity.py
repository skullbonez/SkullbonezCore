#!/usr/bin/env python3
"""
Cross-renderer visual parity check.

Compares GL vs DX11 and GL vs DX12 screenshots. Reports average pixel
difference per pair. Fails if any pair exceeds threshold (avg_diff > 10.0).

The script also writes renderer validation artifacts under:
    TestOutput/validation/renderers/<run-id>/

Exit 0 = parity acceptable, Exit 1 = parity violation, Exit 99 = missing tool.
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


THRESHOLD = 10.0

SCENES = [
    {
        "name": "water_ball_test",
        "label": "water_ball_test",
        "screenshots": {
            "gl": "gl_screenshot.bmp",
            "dx11": "dx11_screenshot.bmp",
            "dx12": "dx12_screenshot.bmp",
        },
    },
    {
        "name": "solver_smoke",
        "label": "solver_smoke",
        "screenshots": {
            "gl": "gl_solver_smoke.bmp",
            "dx11": "dx11_solver_smoke.bmp",
            "dx12": "dx12_solver_smoke.bmp",
        },
    },
]

COMMANDS = {
    "gl": r"Profile\SKULLBONEZ_CORE.exe --vsync off --suite SkullbonezData/scenes/render_tests.suite",
    "dx11": r"Profile\SKULLBONEZ_CORE.exe --renderer dx11 --vsync off --suite SkullbonezData/scenes/render_tests.suite",
    "dx12": r"Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --suite SkullbonezData/scenes/render_tests.suite",
}

COMPARISON_RENDERERS = ("dx11", "dx12")


def repo_relative(repo: Path, path: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo.resolve())).replace("/", "\\")
    except ValueError:
        return str(path)


def run_id() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def save_final_png(source: Path, target: Path) -> tuple[int, int]:
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


def channel_max_pixels(diff_img: Image.Image) -> tuple[int, int]:
    max_diff = 0
    pixels_over_10 = 0
    data = diff_img.tobytes()
    for index in range(0, len(data), 3):
        r = data[index]
        g = data[index + 1]
        b = data[index + 2]
        pixel_max = max(r, g, b)
        max_diff = max(max_diff, pixel_max)
        if pixel_max > 10:
            pixels_over_10 += 1
    return max_diff, pixels_over_10


def compare_images(
    repo: Path,
    out_dir: Path,
    scene_name: str,
    renderer_a: str,
    renderer_b: str,
    a_path: Path,
    b_path: Path,
) -> dict[str, Any]:
    comparison_id = f"{scene_name}_{renderer_a}_vs_{renderer_b}"
    result: dict[str, Any] = {
        "scene": scene_name,
        "rendererA": renderer_a,
        "rendererB": renderer_b,
        "threshold": THRESHOLD,
        "sourceImages": {
            renderer_a: repo_relative(repo, a_path),
            renderer_b: repo_relative(repo, b_path),
        },
    }

    if not a_path.exists() or not b_path.exists():
        missing = []
        if not a_path.exists():
            missing.append(repo_relative(repo, a_path))
        if not b_path.exists():
            missing.append(repo_relative(repo, b_path))
        result.update(
            {
                "status": "missing",
                "missing": missing,
                "averageDiff": None,
                "maxDiff": None,
                "pixelsOver10": None,
                "artifacts": {},
            }
        )
        return result

    a_img = Image.open(a_path).convert("RGB")
    b_img = Image.open(b_path).convert("RGB")
    if a_img.size != b_img.size:
        result.update(
            {
                "status": "size_mismatch",
                "sizeA": list(a_img.size),
                "sizeB": list(b_img.size),
                "averageDiff": None,
                "maxDiff": None,
                "pixelsOver10": None,
                "artifacts": {},
            }
        )
        return result

    diff_img = ImageChops.difference(a_img, b_img)
    pixel_count = a_img.width * a_img.height * 3
    total_diff = sum(diff_img.tobytes())
    avg_diff = total_diff / pixel_count
    max_diff, pixels_over_10 = channel_max_pixels(diff_img)

    side_by_side = out_dir / f"{comparison_id}_side_by_side.png"
    heatmap = out_dir / f"{comparison_id}_heatmap.png"
    amplified = out_dir / f"{comparison_id}_heatmap_x8.png"

    make_side_by_side(a_img, b_img).save(side_by_side)
    diff_img.save(heatmap)
    make_amplified(diff_img, 8).save(amplified)

    status = "pass" if avg_diff <= THRESHOLD else "fail"
    result.update(
        {
            "status": status,
            "averageDiff": round(avg_diff, 4),
            "maxDiff": max_diff,
            "pixelsOver10": pixels_over_10,
            "size": [a_img.width, a_img.height],
            "artifacts": {
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


def print_summary(repo: Path, manifest_path: Path, summary_path: Path, comparisons: list[dict[str, Any]]) -> None:
    print(f"Renderer artifact manifest: {repo_relative(repo, manifest_path)}")
    print(f"Renderer comparison summary: {repo_relative(repo, summary_path)}")
    print("Renderer comparison artifacts:")
    for item in comparisons:
        scene = item["scene"]
        pair = f"{item['rendererA']} vs {item['rendererB']}"
        status = str(item["status"]).upper()
        if item.get("averageDiff") is None:
            print(f"  {scene} {pair}: {status}")
            continue
        print(
            f"  {scene} {pair}: avg_diff={item['averageDiff']:.4f} "
            f"max_diff={item['maxDiff']} pixels_over_10={item['pixelsOver10']} [{status}]"
        )
        artifacts = item.get("artifacts", {})
        side_by_side = artifacts.get("sideBySide")
        heatmap = artifacts.get("heatmap")
        amplified = artifacts.get("amplifiedHeatmap")
        if side_by_side:
            print(f"    side_by_side: {side_by_side}")
        if heatmap:
            print(f"    heatmap: {heatmap}")
        if amplified:
            print(f"    amplified: {amplified}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1])))
    parser.add_argument("--out-root", type=Path, default=None)
    parser.add_argument("--run-id", default=os.environ.get("SKORE_VALIDATION_RUN_ID") or run_id())
    args = parser.parse_args()

    repo = args.repo.resolve()
    profile = repo / "Profile"
    out_root = args.out_root.resolve() if args.out_root else repo / "TestOutput" / "validation" / "renderers"
    out_dir = out_root / args.run_id
    out_dir.mkdir(parents=True, exist_ok=True)

    screenshots: dict[str, Any] = {}
    for scene in SCENES:
        scene_name = scene["name"]
        screenshots[scene_name] = {}
        for renderer, file_name in scene["screenshots"].items():
            source = profile / file_name
            final_png = out_dir / f"{scene_name}_{renderer}_final.png"
            entry: dict[str, Any] = {
                "source": repo_relative(repo, source),
                "artifact": repo_relative(repo, final_png),
            }
            if source.exists():
                width, height = save_final_png(source, final_png)
                entry["viewport"] = {"width": width, "height": height}
            else:
                entry["missing"] = True
            screenshots[scene_name][renderer] = entry

    comparisons: list[dict[str, Any]] = []
    for scene in SCENES:
        scene_name = scene["name"]
        a_path = profile / scene["screenshots"]["gl"]
        for renderer_b in COMPARISON_RENDERERS:
            b_path = profile / scene["screenshots"][renderer_b]
            comparisons.append(compare_images(repo, out_dir, scene_name, "gl", renderer_b, a_path, b_path))

    overall_status = "pass" if all(item.get("status") == "pass" for item in comparisons) else "fail"
    summary_path = out_dir / "summary.json"
    manifest_path = out_dir / "manifest.json"
    dx12_log = copy_dx12_validation_log(repo, out_dir)

    summary = {
        "runId": args.run_id,
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "threshold": THRESHOLD,
        "status": overall_status,
        "comparisons": comparisons,
    }
    manifest = {
        "runId": args.run_id,
        "generatedAtUtc": summary["generatedAtUtc"],
        "suite": "SkullbonezData/scenes/render_tests.suite",
        "threshold": THRESHOLD,
        "status": overall_status,
        "commands": [{"renderer": renderer, "command": command} for renderer, command in COMMANDS.items()],
        "screenshots": screenshots,
        "summary": repo_relative(repo, summary_path),
        "dx12ValidationLog": dx12_log,
    }

    write_json(summary_path, summary)
    write_json(manifest_path, manifest)
    print_summary(repo, manifest_path, summary_path, comparisons)

    if overall_status == "pass":
        print("PASS: Cross-renderer parity acceptable.")
        return 0

    print("FAIL: Cross-renderer parity violated.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
