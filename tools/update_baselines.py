#!/usr/bin/env python3
"""Update committed baselines from current Profile artifacts."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


VISUALS = [
    ("gl_screenshot.bmp", "baseline_gl_water_ball_test.png"),
    ("gl_solver_smoke.bmp", "baseline_gl_solver_smoke.png"),
    ("dx11_screenshot.bmp", "baseline_dx11_water_ball_test.png"),
    ("dx11_solver_smoke.bmp", "baseline_dx11_solver_smoke.png"),
    ("dx12_screenshot.bmp", "baseline_dx12_water_ball_test.png"),
    ("dx12_solver_smoke.bmp", "baseline_dx12_solver_smoke.png"),
]

PERF = [
    ("gl_perf.json", "gl_perf.json"),
    ("dx11_perf.json", "dx11_perf.json"),
    ("dx12_perf.json", "dx12_perf.json"),
]


def update_visuals(repo: Path, require: bool) -> int:
    try:
        from PIL import Image
    except ModuleNotFoundError as exc:
        raise SystemExit("Pillow is required. Install with: py -m pip install Pillow") from exc

    profile = repo / "Profile"
    baselines = repo / "TestOutput" / "baselines"
    baselines.mkdir(parents=True, exist_ok=True)

    updated = 0
    missing: list[str] = []
    for src_name, dst_name in VISUALS:
        src = profile / src_name
        dst = baselines / dst_name
        if not src.exists():
            missing.append(str(src.relative_to(repo)))
            continue
        Image.open(src).save(dst)
        print(f"updated {dst.relative_to(repo)}")
        updated += 1

    if require and missing:
        for item in missing:
            print(f"missing {item}")
        return 1
    if updated == 0:
        print("no visual artifacts found")
    return 0


def update_perf(repo: Path, require: bool) -> int:
    profile = repo / "Profile"
    baselines = repo / "TestOutput" / "baselines"
    baselines.mkdir(parents=True, exist_ok=True)

    updated = 0
    missing: list[str] = []
    for src_name, dst_name in PERF:
        src = profile / src_name
        dst = baselines / dst_name
        if not src.exists():
            missing.append(str(src.relative_to(repo)))
            continue
        shutil.copy2(src, dst)
        print(f"updated {dst.relative_to(repo)}")
        updated += 1

    if require and missing:
        for item in missing:
            print(f"missing {item}")
        return 1
    if updated == 0:
        print("no perf artifacts found")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--visuals", action="store_true", help="update screenshot PNG baselines")
    parser.add_argument("--perf", action="store_true", help="update perf JSON baselines")
    parser.add_argument("--require", action="store_true", help="fail if any selected artifact is missing")
    args = parser.parse_args()

    do_visuals = args.visuals or not args.perf
    do_perf = args.perf or not args.visuals

    status = 0
    if do_visuals:
        status |= update_visuals(args.repo.resolve(), args.require)
    if do_perf:
        status |= update_perf(args.repo.resolve(), args.require)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
