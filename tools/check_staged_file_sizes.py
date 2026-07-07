#!/usr/bin/env python3
#
# File: tools/check_staged_file_sizes.py
# Purpose:
#   Blocks accidental commits of large staged blobs outside approved data
#   locations.
#
# Mental model:
#   This is a tip-tree hygiene guardrail. It does not shrink existing history;
#   it stops new oversized files from entering ordinary commits.
#
# Glossary:
#   Staged blob: The exact file content currently in the git index.
#   Size allowlist: Repository areas where large tracked data is intentional.
#
# Invariants:
#   - The checker reads the git index, not the working tree, so it matches what
#     a commit would contain.
#   - Baselines and SkullbonezData are the only broad large-file locations.
#   - Self-tests run without touching the real git index.
#
# Related:
#   - fable_plans/04-build-layering-and-repo-hygiene-progress.md
#   - tools/validate_fast.bat
#
"""Check staged file sizes against the repo hygiene budget."""

from __future__ import annotations

import argparse
import json
import subprocess
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


DEFAULT_MAX_BYTES = 5 * 1024 * 1024
ALLOWLIST_PREFIXES = (
    "SkullbonezData/",
    "TestOutput/baselines/",
)


@dataclass(frozen=True)
class StagedFile:
    path: str
    size_bytes: int


@dataclass(frozen=True)
class SizeViolation:
    path: str
    size_bytes: int
    max_bytes: int


def repo_relative_path(path: str) -> str:
    return path.replace("\\", "/").lstrip("/")


def is_allowlisted(path: str) -> bool:
    normalized = repo_relative_path(path)
    return any(normalized.startswith(prefix) for prefix in ALLOWLIST_PREFIXES)


def check_entries(entries: list[StagedFile], max_bytes: int) -> list[SizeViolation]:
    violations: list[SizeViolation] = []
    for entry in entries:
        if entry.size_bytes > max_bytes and not is_allowlisted(entry.path):
            violations.append(SizeViolation(entry.path, entry.size_bytes, max_bytes))
    return violations


def run_git(repo: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def staged_paths(repo: Path) -> list[str]:
    # Invariant: read --cached so this gate reflects the pending commit, not
    # large unstaged scratch files that are already ignored or local-only.
    result = run_git(repo, ["diff", "--cached", "--name-only", "--diff-filter=AM", "--"])
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "git diff --cached failed")
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def staged_blob_size(repo: Path, path: str) -> int:
    result = run_git(repo, ["cat-file", "-s", f":{path}"])
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"git cat-file failed for {path}")
    return int(result.stdout.strip())


def collect_staged_entries(repo: Path) -> list[StagedFile]:
    return [StagedFile(path, staged_blob_size(repo, path)) for path in staged_paths(repo)]


def write_summary(
    repo: Path,
    summary_path: Path,
    entries: list[StagedFile],
    violations: list[SizeViolation],
    max_bytes: int,
) -> None:
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "max_bytes": max_bytes,
        "allowlist_prefixes": list(ALLOWLIST_PREFIXES),
        "staged_file_count": len(entries),
        "violations": [violation.__dict__ for violation in violations],
    }
    summary_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def run_self_tests() -> list[str]:
    failures: list[str] = []
    max_bytes = DEFAULT_MAX_BYTES
    large = max_bytes + 1

    def expect_clean(name: str, entries: list[StagedFile]) -> None:
        violations = check_entries(entries, max_bytes)
        if violations:
            failures.append(f"{name}: expected clean, got {violations}")

    def expect_violation(name: str, entries: list[StagedFile], path: str) -> None:
        violations = check_entries(entries, max_bytes)
        if not any(violation.path == path for violation in violations):
            failures.append(f"{name}: expected violation for {path}, got {violations}")

    expect_clean("small source file", [StagedFile("SkullbonezSource/Foo.cpp", max_bytes)])
    expect_clean("large baseline", [StagedFile("TestOutput/baselines/large.png", large)])
    expect_clean("large data asset", [StagedFile("SkullbonezData/assets/large.assets.json", large)])
    expect_violation("large temp report", [StagedFile("Agentic/Temp/huge.txt", large)], "Agentic/Temp/huge.txt")
    expect_violation("large root file", [StagedFile("huge.bin", large)], "huge.bin")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="Repository root. Defaults to cwd.")
    parser.add_argument("--self-test", action="store_true", help="Run synthetic tests without reading the git index.")
    parser.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES, help="Maximum staged file size.")
    parser.add_argument(
        "--json-out",
        type=Path,
        default=None,
        help="Optional JSON summary path. Defaults under TestOutput/validation.",
    )
    args = parser.parse_args()

    failures = run_self_tests()
    if failures:
        for failure in failures:
            print(f"ERROR: staged file size self-test failed: {failure}")
        return 1
    if args.self_test:
        print("SELF_TEST_PASS: staged file size checker synthetic cases passed")
        return 0

    repo = args.repo.resolve()
    try:
        entries = collect_staged_entries(repo)
    except RuntimeError as exc:
        print(f"ERROR: {exc}")
        return 2

    violations = check_entries(entries, args.max_bytes)
    summary_path = args.json_out or repo / "TestOutput" / "validation" / "staged_file_sizes" / "summary.json"
    write_summary(repo, summary_path, entries, violations, args.max_bytes)

    print(f"Staged file size summary: {summary_path.relative_to(repo)} ({len(violations)} violation(s))")
    if violations:
        for violation in violations:
            print(
                "ERROR: staged file exceeds size budget: "
                f"{violation.path} size={violation.size_bytes} max={violation.max_bytes}"
            )
        return 1
    print("PASS: staged file size check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
