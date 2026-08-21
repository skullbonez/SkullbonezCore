#
# File: tools/check_physics_commit_gate.py
# Purpose:
#   Requires fresh deterministic physics validation before affected commits.
#
# Summary:
#   The pre-commit hook classifies staged simulation inputs, rejects partial
#   worktree/index mixtures, and runs validate_physics once per exact relevant
#   index state. A local success stamp makes retries immediate without allowing
#   a later source or golden change to reuse stale evidence.
#
# Glossary:
#   Relevant index state: Git blob identities for staged source, project,
#   authored scene, shader, and physics-validation inputs.
#   Validation stamp: Local Git metadata recording one successful relevant
#   index fingerprint and approved golden digest.
#
# Invariants:
#   - Documentation-only commits do not launch the engine.
#   - Relevant unstaged or untracked inputs fail closed before a build.
#   - A failed runtime comparison removes prior success evidence.
#   - The stamp is cache evidence only; the byte-exact comparator remains the oracle.
#
# Related:
#   - AGENTS.md
#   - tools/check_physics_baseline_guard.py
#   - tools/validate_physics.bat
#
"""Run or reuse physics validation for the exact staged simulation inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys


SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl", ".hlsl"}
BUILD_SUFFIXES = {".sln", ".vcxproj", ".props", ".targets"}
EXACT_TOOL_INPUTS = {
    "tools/check_physics_baseline_guard.py",
    "tools/check_physics_commit_gate.py",
    "tools/check_physics_regression.py",
    "tools/physics_baseline_approval.json",
    "tools/validate_physics.bat",
}
BASELINE_PATH = "TestOutput/baselines/physics_regression_varied.csv"
STAMP_RELATIVE_PATH = Path("skore-validation") / "physics-precommit.json"


class GateFailure(RuntimeError):
    """A bounded staged-state or validation failure."""


def run_git(repo: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if check and result.returncode != 0:
        raise GateFailure(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result


def normalized_paths(output: str) -> list[str]:
    return [line.strip().replace("\\", "/") for line in output.splitlines() if line.strip()]


def affects_physics(path: str) -> bool:
    normalized = path.replace("\\", "/")
    value = PurePosixPath(normalized)
    suffix = value.suffix.lower()
    if normalized == BASELINE_PATH or normalized in EXACT_TOOL_INPUTS:
        return True
    if normalized.startswith("SkullbonezSource/") and suffix in SOURCE_SUFFIXES:
        return True
    if normalized.startswith("ThirdPtySource/"):
        return True
    if normalized.startswith("SkullbonezData/") and suffix == ".json":
        return True
    if suffix in BUILD_SUFFIXES:
        return True
    return False


def changed_paths(repo: Path, *git_args: str) -> list[str]:
    return normalized_paths(run_git(repo, *git_args).stdout)


def relevant_index_lines(repo: Path) -> list[str]:
    lines = normalized_paths(run_git(repo, "ls-files", "-s").stdout)
    relevant: list[str] = []
    for line in lines:
        fields = line.split(maxsplit=3)
        if len(fields) == 4 and affects_physics(fields[3]):
            relevant.append(line)
    return sorted(relevant)


def fingerprint(repo: Path, approved_digest: str) -> str:
    payload = "\n".join([approved_digest, *relevant_index_lines(repo)]).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def git_common_dir(repo: Path) -> Path:
    raw = run_git(repo, "rev-parse", "--git-common-dir").stdout.strip()
    path = Path(raw)
    return (path if path.is_absolute() else repo / path).resolve()


def stamp_path(repo: Path) -> Path:
    return git_common_dir(repo) / STAMP_RELATIVE_PATH


def load_stamp(repo: Path) -> dict[str, object] | None:
    path = stamp_path(repo)
    if not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def write_stamp(repo: Path, state_fingerprint: str, approved_digest: str) -> None:
    path = stamp_path(repo)
    path.parent.mkdir(parents=True, exist_ok=True)
    value = {
        "schema_version": 1,
        "index_fingerprint": state_fingerprint,
        "approved_baseline_sha256": approved_digest,
    }
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def remove_stamp(repo: Path) -> None:
    try:
        stamp_path(repo).unlink()
    except FileNotFoundError:
        pass


def approved_digest(repo: Path) -> str:
    command = [
        sys.executable,
        str(repo / "tools" / "check_physics_baseline_guard.py"),
        "--repo",
        str(repo),
        "--staged",
    ]
    result = subprocess.run(command, check=False)
    if result.returncode != 0:
        raise GateFailure("staged physics golden approval check failed")
    staged_record = run_git(repo, "show", ":tools/physics_baseline_approval.json").stdout
    record = json.loads(staged_record)
    return str(record["sha256"])


def validate_clean_relevant_worktree(repo: Path) -> None:
    unstaged = [
        path
        for path in changed_paths(repo, "diff", "--name-only", "--diff-filter=ACMRTUXB")
        if affects_physics(path)
    ]
    untracked = [
        path
        for path in changed_paths(repo, "ls-files", "--others", "--exclude-standard")
        if affects_physics(path)
    ]
    if unstaged or untracked:
        shown = [*(f"unstaged: {path}" for path in unstaged[:8]), *(f"untracked: {path}" for path in untracked[:8])]
        raise GateFailure(
            "cannot validate the staged snapshot while relevant working-tree inputs differ:\n  "
            + "\n  ".join(shown)
        )


def run_commit_gate(repo: Path) -> None:
    staged = changed_paths(repo, "diff", "--cached", "--name-only", "--diff-filter=ACMRTUXB")
    if not any(affects_physics(path) for path in staged):
        print("PASS: staged change does not affect deterministic physics inputs")
        return

    digest = approved_digest(repo)
    validate_clean_relevant_worktree(repo)
    state = fingerprint(repo, digest)
    stamp = load_stamp(repo)
    if stamp == {
        "schema_version": 1,
        "index_fingerprint": state,
        "approved_baseline_sha256": digest,
    }:
        print(f"PASS: reusing physics validation for staged fingerprint {state[:12]}")
        return

    remove_stamp(repo)
    print("RUN: staged source can affect deterministic physics; validating before commit...")
    command = ["cmd.exe", "/d", "/c", str(repo / "tools" / "validate_physics.bat"), "--commit-gate"]
    result = subprocess.run(command, cwd=repo, check=False)
    if result.returncode != 0:
        raise GateFailure(f"physics commit validation failed with exit code {result.returncode}")
    write_stamp(repo, state, digest)
    print(f"PASS: physics validation recorded for staged fingerprint {state[:12]}")


def self_test() -> None:
    positive = [
        "SkullbonezSource/Runtime/App/Run.cpp",
        "SkullbonezSource/Physics/PhysicsApi.h",
        "SkullbonezSource/Rendering/Physics.hlsl",
        "SkullbonezData/scenes/physics_bench_varied.scene.json",
        "SkullbonezData/config/physics.json",
        "ThirdPtySource/JoltPhysics",
        "SKULLBONEZ_CORE.sln",
        "SKULLBONEZ_CORE.vcxproj",
        BASELINE_PATH,
        "tools/validate_physics.bat",
    ]
    negative = [
        "README.md",
        "Agentic/Plans/TODO/example.md",
        "SkullbonezData/unrelated.txt",
        "tools/README.md",
    ]
    if any(not affects_physics(path) for path in positive):
        raise GateFailure("self-test missed a physics-affecting path")
    if any(affects_physics(path) for path in negative):
        raise GateFailure("self-test classified documentation as physics-affecting")
    print("PASS: physics commit gate self-tests")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
        else:
            run_commit_gate(args.repo.resolve())
    except (GateFailure, OSError, json.JSONDecodeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
