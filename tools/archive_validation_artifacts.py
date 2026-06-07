#!/usr/bin/env python3
"""Archive current Profile validation artifacts into TestOutput/NNN_commit."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
from pathlib import Path


VISUALS = [
    ("gl_screenshot.bmp", "gl_water_ball_test.png"),
    ("gl_solver_smoke.bmp", "gl_solver_smoke.png"),
    ("dx11_screenshot.bmp", "dx11_water_ball_test.png"),
    ("dx11_solver_smoke.bmp", "dx11_solver_smoke.png"),
    ("dx12_screenshot.bmp", "dx12_water_ball_test.png"),
    ("dx12_solver_smoke.bmp", "dx12_solver_smoke.png"),
]

PERF = ["gl_perf.json", "dx11_perf.json", "dx12_perf.json", "physics_bench.json"]


def short_commit(repo: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=repo,
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def archive_dir(repo: Path, commit: str) -> Path:
    root = repo / "TestOutput"
    root.mkdir(exist_ok=True)
    pattern = re.compile(r"^(\d+)_")
    dirs = [p for p in root.iterdir() if p.is_dir() and pattern.match(p.name)]
    for path in dirs:
        if path.name.split("_", 1)[1] == commit:
            path.mkdir(exist_ok=True)
            return path
    next_seq = max((int(pattern.match(p.name).group(1)) for p in dirs), default=0) + 1
    path = root / f"{next_seq:03d}_{commit}"
    path.mkdir()
    return path


def copy_visuals(repo: Path, out: Path, require: bool) -> int:
    try:
        from PIL import Image
    except ModuleNotFoundError as exc:
        raise SystemExit("Pillow is required. Install with: py -m pip install Pillow") from exc

    status = 0
    for src_name, dst_name in VISUALS:
        src = repo / "Profile" / src_name
        dst = out / dst_name
        if not src.exists():
            print(f"missing {src.relative_to(repo)}")
            status = 1 if require else status
            continue
        Image.open(src).save(dst)
        print(f"archived {dst.relative_to(repo)}")
    return status


def copy_perf(repo: Path, out: Path, require: bool) -> int:
    status = 0
    for name in PERF:
        src = repo / "Profile" / name
        dst = out / name
        if not src.exists():
            if name != "physics_bench.json":
                print(f"missing {src.relative_to(repo)}")
                status = 1 if require else status
            continue
        shutil.copy2(src, dst)
        print(f"archived {dst.relative_to(repo)}")
    return status


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--commit", default=None)
    parser.add_argument("--visuals", action="store_true")
    parser.add_argument("--perf", action="store_true")
    parser.add_argument("--require", action="store_true")
    args = parser.parse_args()

    repo = args.repo.resolve()
    commit = args.commit or short_commit(repo)
    out = archive_dir(repo, commit)
    print(f"archive {out.relative_to(repo)}")

    do_visuals = args.visuals or not args.perf
    do_perf = args.perf or not args.visuals

    status = 0
    if do_visuals:
        status |= copy_visuals(repo, out, args.require)
    if do_perf:
        status |= copy_perf(repo, out, args.require)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
