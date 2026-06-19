#
# File: tools/validate_scene_loads.py
# Purpose:
#   Documents and runs the validate_scene_loads.py developer/validation helper script.
#
# Mental model:
#   Tools are command-line guardrails around builds, validation, screenshots,
#   diagnostics, and artifact handling. They make the safe path repeatable and
#   keep output bounded for humans and agents.
#
# Glossary:
#   Validation gate: Repository script that proves a class of changes before
#   commit or PR.
#
# Invariants:
#   - Tool output should be bounded and readable because agents and humans use
#   it for decisions.
#
# Related:
#   - AGENTS.md
#   - Agentic/Reference/comment-style-guide.md
#
#
"""Load-only sweep for every Skullbonez scene file."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import time


REPO = Path(__file__).resolve().parents[1]
SCENE_DIR = REPO / "SkullbonezData" / "scenes"
EXE = REPO / "Profile" / "SKULLBONEZ_CORE.exe"
LOG_DIR = REPO / "Profile" / "scene_load_sweep"


def rel_scene(path: Path) -> str:
    return path.relative_to(REPO).as_posix()


def safe_name(path: Path) -> str:
    return path.stem.replace(" ", "_")


def run_scene(scene: Path, index: int, count: int, timeout: float, renderer: str) -> bool:
    stdout_path = LOG_DIR / f"{index:03d}_{safe_name(scene)}.stdout.txt"
    stderr_path = LOG_DIR / f"{index:03d}_{safe_name(scene)}.stderr.txt"
    args = [
        str(EXE),
        "--renderer",
        renderer,
        "--vsync",
        "off",
        "--scene-load-only",
        "--scene",
        rel_scene(scene),
    ]

    start = time.perf_counter()
    try:
        result = subprocess.run(
            args,
            cwd=REPO,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        elapsed = time.perf_counter() - start
        stdout_path.write_bytes(exc.stdout or b"")
        stderr_path.write_bytes(exc.stderr or b"")
        print(f"FAIL [{index:02d}/{count:02d}] {rel_scene(scene)} timed out after {elapsed:.2f}s")
        print(f"      stdout: {stdout_path.relative_to(REPO)}")
        print(f"      stderr: {stderr_path.relative_to(REPO)}")
        return False

    elapsed = time.perf_counter() - start
    stdout_path.write_bytes(result.stdout)
    stderr_path.write_bytes(result.stderr)

    if result.returncode != 0:
        print(f"FAIL [{index:02d}/{count:02d}] {rel_scene(scene)} exited {result.returncode} after {elapsed:.2f}s")
        print(f"      stdout: {stdout_path.relative_to(REPO)}")
        print(f"      stderr: {stderr_path.relative_to(REPO)}")
        return False

    print(f"PASS [{index:02d}/{count:02d}] {rel_scene(scene)} loaded ({elapsed:.2f}s)")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=20.0, help="Seconds allowed per scene load.")
    parser.add_argument("--renderer", default="dx12", choices=["dx12"], help="Renderer used for the boot sweep.")
    args = parser.parse_args()

    if not EXE.exists():
        print(f"ERROR: Profile executable not found: {EXE}", file=sys.stderr)
        return 2

    scenes = sorted(SCENE_DIR.glob("*.scene.json"), key=lambda p: p.name.lower())
    if not scenes:
        print(f"ERROR: No scenes found in {SCENE_DIR}", file=sys.stderr)
        return 2

    LOG_DIR.mkdir(parents=True, exist_ok=True)
    for old_log in LOG_DIR.glob("*.txt"):
        old_log.unlink()

    print(f"Scene count: {len(scenes)}")
    failures = []
    for index, scene in enumerate(scenes, start=1):
        if not run_scene(scene, index, len(scenes), args.timeout, args.renderer):
            failures.append(scene)

    if failures:
        print("")
        print("FAIL: Scene load sweep found boot failures:")
        for scene in failures:
            print(f"  {rel_scene(scene)}")
        return 1

    print("")
    print(f"PASS: {len(scenes)} scenes loaded without entering the frame loop.")
    print(f"Logs: {LOG_DIR.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
