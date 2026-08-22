#
# File: tools/validate_concepts.py
# Purpose:
#   Documents and runs the validate_concepts.py developer/validation helper script.
#
# Summary:
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
#
#
#!/usr/bin/env python3
#
# File: tools/validate_concepts.py
# Purpose:
#   Documents and runs the validate_concepts.py developer/validation helper script.
#
# Concept:
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
#
#
"""Run finite concept-scene validation tiers and write concise artifacts."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


TIERS = {
    "smoke": [
        "SkullbonezData/scenes/concept_01_golden_hour_realism.scene.json",
        "SkullbonezData/scenes/concept_04_neon_cyberpunk.scene.json",
        "SkullbonezData/scenes/concept_12_low_poly_art_style.scene.json",
    ],
    "core": [
        "SkullbonezData/scenes/concept_01_golden_hour_realism.scene.json",
        "SkullbonezData/scenes/concept_04_neon_cyberpunk.scene.json",
        "SkullbonezData/scenes/concept_07_painterly.scene.json",
        "SkullbonezData/scenes/concept_10_ocean_world.scene.json",
        "SkullbonezData/scenes/concept_12_low_poly_art_style.scene.json",
        "SkullbonezData/scenes/concept_14_storm_front.scene.json",
        "SkullbonezData/scenes/concept_16_tron_grid.scene.json",
        "SkullbonezData/scenes/concept_20_pixar_inspired.scene.json",
    ],
}

RENDERERS = {"dx12": ["dx12"]}

LOG_NEEDLES = ("error", "warning", "failed")


def repo_relative(repo: Path, path: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo.resolve())).replace("/", "\\")
    except ValueError:
        return str(path)


def run_id() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def read_full_suite(repo: Path) -> list[str]:
    suite = repo / "SkullbonezData" / "scenes" / "concepts.suite.json"
    payload = json.loads(suite.read_text(encoding="utf-8"))
    if payload.get("format") != "skullbonez.suite.json":
        raise RuntimeError(f"{suite} must declare format skullbonez.suite.json")
    scenes = payload.get("scenes")
    if not isinstance(scenes, list) or any(not isinstance(scene, str) for scene in scenes):
        raise RuntimeError(f"{suite} must contain a scenes array of strings")
    return [scene.replace("\\", "/") for scene in scenes]


def scene_slug(scene: str) -> str:
    name = Path(scene).stem
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", name)


def command_for(repo: Path, renderer: str, scene: str, frames: int) -> list[str]:
    exe = repo / "Profile" / "SKULLBONEZ_CORE.exe"
    command = [str(exe), "--renderer", renderer, "--vsync", "off", "--frames", str(frames), "--scene", scene]
    return command


def shell_join(command: list[str]) -> str:
    return " ".join(f'"{part}"' if " " in part else part for part in command)


def scan_log(path: Path) -> list[str]:
    if not path.exists():
        return [f"log missing: {path}"]
    matches: list[str] = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        lower = line.lower()
        if any(needle in lower for needle in LOG_NEEDLES):
            matches.append(line)
    return matches


def dx12_validation_count(repo: Path) -> tuple[int | None, str]:
    log_path = repo / "dx12_validation.txt"
    if not log_path.exists():
        return None, "missing"
    lines = [line.strip() for line in log_path.read_text(encoding="utf-8", errors="ignore").splitlines() if line.strip()]
    if not lines:
        return None, "empty"
    try:
        return int(lines[-1]), "available"
    except ValueError:
        return None, "unreadable"


def copy_dx12_log(repo: Path, out_dir: Path, renderer: str, scene: str) -> str | None:
    source = repo / "dx12_validation.txt"
    if not source.exists():
        return None
    target = out_dir / f"{renderer}_{scene_slug(scene)}_dx12_validation.txt"
    target.write_bytes(source.read_bytes())
    return repo_relative(repo, target)


def run_one(repo: Path, out_dir: Path, renderer: str, scene: str, frames: int) -> dict[str, Any]:
    stdout_path = out_dir / f"{renderer}_{scene_slug(scene)}_stdout.txt"
    stderr_path = out_dir / f"{renderer}_{scene_slug(scene)}_stderr.txt"
    command = command_for(repo, renderer, scene, frames)
    print(f"Running: {shell_join(command)}")

    if renderer == "dx12":
        try:
            (repo / "dx12_validation.txt").unlink()
        except FileNotFoundError:
            pass

    with stdout_path.open("w", encoding="utf-8") as stdout, stderr_path.open("w", encoding="utf-8") as stderr:
        completed = subprocess.run(command, cwd=repo, stdout=stdout, stderr=stderr, text=True)

    log_matches = scan_log(stdout_path) + scan_log(stderr_path)
    status = "pass" if completed.returncode == 0 and not log_matches else "fail"
    dx12_status = None
    dx12_errors = None
    dx12_log = None
    if renderer == "dx12":
        dx12_errors, dx12_status = dx12_validation_count(repo)
        dx12_log = copy_dx12_log(repo, out_dir, renderer, scene)
        print(f"DX12 validation status: {dx12_status}")
        print(f"DX12 validation errors: {dx12_errors if dx12_errors is not None else 'unavailable'}")
        if dx12_errors != 0:
            status = "fail"

    result: dict[str, Any] = {
        "scene": scene,
        "renderer": renderer,
        "frames": frames,
        "command": shell_join(command),
        "returnCode": completed.returncode,
        "status": status,
        "stdout": repo_relative(repo, stdout_path),
        "stderr": repo_relative(repo, stderr_path),
        "logMatches": log_matches,
    }
    if renderer == "dx12":
        result["dx12ValidationStatus"] = dx12_status
        result["dx12ValidationErrors"] = dx12_errors
        result["dx12ValidationLog"] = dx12_log
    return result


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--tier", choices=["smoke", "core", "full"], default="smoke")
    parser.add_argument("--renderer", choices=sorted(RENDERERS), default="dx12")
    parser.add_argument("--frames", type=int, default=2)
    parser.add_argument("--run-id", default=run_id())
    args = parser.parse_args()

    if args.frames <= 0:
        print("ERROR: --frames must be positive.")
        return 1

    repo = args.repo.resolve()
    exe = repo / "Profile" / "SKULLBONEZ_CORE.exe"
    if not exe.exists():
        print(f"ERROR: Profile executable not found: {repo_relative(repo, exe)}")
        return 1

    scenes = read_full_suite(repo) if args.tier == "full" else TIERS[args.tier]
    renderers = RENDERERS[args.renderer]
    out_dir = repo / "TestOutput" / "validation" / "concepts" / args.run_id
    out_dir.mkdir(parents=True, exist_ok=True)

    results: list[dict[str, Any]] = []
    for renderer in renderers:
        for scene in scenes:
            results.append(run_one(repo, out_dir, renderer, scene, args.frames))

    status = "pass" if all(item["status"] == "pass" for item in results) else "fail"
    manifest_path = out_dir / "manifest.json"
    summary_path = out_dir / "summary.json"
    payload = {
        "runId": args.run_id,
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "tier": args.tier,
        "renderer": args.renderer,
        "frames": args.frames,
        "status": status,
        "scenes": scenes,
        "results": results,
    }
    write_json(manifest_path, payload)
    write_json(summary_path, {"status": status, "results": results})

    print(f"Concept validation manifest: {repo_relative(repo, manifest_path)}")
    print(f"Concept validation summary: {repo_relative(repo, summary_path)}")
    for item in results:
        print(f"  {item['renderer']} {Path(item['scene']).stem}: {item['status'].upper()}")

    if status == "pass":
        print("PASS: Concept validation tier passed.")
        return 0

    print("FAIL: Concept validation tier failed.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
