#!/usr/bin/env python3
#
# File: tools/check_staged_file_sizes.py
# Purpose:
#   Blocks accidental commits or pull requests containing large blobs outside
#   approved data locations.
#
# Summary:
#   This is a tip-tree hygiene guardrail. Local validation reads the git index;
#   hosted CI can compare HEAD with an explicit base ref. Neither mode rewrites
#   history, and both inspect exact git blobs rather than working-tree metadata.
#
# Glossary:
#   Staged blob: The exact file content currently in the git index.
#   Comparison blob: The exact HEAD content of a file added or modified since an
#     explicit base ref.
#   Gitlink: A mode-160000 tree entry that pins a submodule commit; its content
#     lives in the submodule object database rather than in a parent-repo blob.
#   Size allowlist: Repository areas where large tracked data is intentional.
#
# Invariants:
#   - The checker reads the git index, not the working tree, so it matches what
#     a commit would contain unless --base-ref selects CI comparison mode.
#   - Comparison mode reads HEAD blobs changed from merge-base(base, HEAD), so a
#     clean hosted checkout does not silently validate zero files.
#   - Rename detection is disabled so moving an allowlisted blob to an ordinary
#     path is evaluated as a deleted source plus an added destination blob.
#   - Gitlinks contribute zero parent-repository blob bytes; the checker still
#     measures the ordinary .gitmodules blob that makes each pin reproducible.
#   - Baselines and SkullbonezData are the only broad large-file locations;
#     Physics plan executables are a narrow named artifact exception.
#   - Self-tests run without touching the real git index.
#
# Related:
#   - tools/validate_fast.bat
#   - .github/workflows/mandatory-cpu-validation.yml
#
"""Check pending-commit or CI comparison blobs against the size budget."""

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
PHYSICS_EXECUTABLE_ARTIFACT_PREFIX = "Agentic/Plans/Artifacts/ragdoll-physics-unification/"


@dataclass(frozen=True)
class CandidateFile:
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
    if any(normalized.startswith(prefix) for prefix in ALLOWLIST_PREFIXES):
        return True
    artifact_name = normalized.rsplit("/", 1)[-1]
    return normalized.startswith(PHYSICS_EXECUTABLE_ARTIFACT_PREFIX) and (
        artifact_name.startswith("SKULLBONEZ_CORE-") and artifact_name.endswith(".exe")
    )


def check_entries(entries: list[CandidateFile], max_bytes: int) -> list[SizeViolation]:
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
        encoding="utf-8",
        errors="surrogateescape",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def staged_paths(repo: Path) -> list[str]:
    # Invariant: read --cached so this gate reflects the pending commit, not
    # large unstaged scratch files that are already ignored or local-only.
    result = run_git(
        repo,
        ["diff", "--cached", "--no-renames", "--name-only", "--diff-filter=AM", "-z", "--"],
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "git diff --cached failed")
    return [path for path in result.stdout.split("\0") if path]


def staged_blob_size(repo: Path, path: str) -> int:
    mode = staged_entry_mode(repo, path)
    if mode == "160000":
        return 0
    result = run_git(repo, ["cat-file", "-s", f":{path}"])
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"git cat-file failed for {path}")
    return int(result.stdout.strip())


def entry_mode(record: str, path: str) -> str:
    metadata, separator, _ = record.partition("\t")
    fields = metadata.split()
    if not separator or not fields:
        raise RuntimeError(f"cannot parse git entry metadata for {path}")
    return fields[0]


def staged_entry_mode(repo: Path, path: str) -> str:
    result = run_git(repo, ["ls-files", "--stage", "-z", "--", path])
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"git ls-files failed for {path}")
    record = result.stdout.rstrip("\0")
    if not record or "\0" in record:
        raise RuntimeError(f"expected one staged git entry for {path}")
    return entry_mode(record, path)


def comparison_paths(repo: Path, base_ref: str) -> list[str]:
    # Why: triple-dot anchors the file set at the merge base, matching the
    # contribution rather than unrelated commits that may sit on either ref.
    result = run_git(
        repo,
        ["diff", "--no-renames", "--name-only", "--diff-filter=AM", "-z", f"{base_ref}...HEAD", "--"],
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"git diff {base_ref}...HEAD failed")
    return [path for path in result.stdout.split("\0") if path]


def head_blob_size(repo: Path, path: str) -> int:
    mode = head_entry_mode(repo, path)
    if mode == "160000":
        return 0
    result = run_git(repo, ["cat-file", "-s", f"HEAD:{path}"])
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"git cat-file failed for HEAD:{path}")
    return int(result.stdout.strip())


def head_entry_mode(repo: Path, path: str) -> str:
    result = run_git(repo, ["ls-tree", "-z", "HEAD", "--", path])
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"git ls-tree failed for HEAD:{path}")
    record = result.stdout.rstrip("\0")
    if not record or "\0" in record:
        raise RuntimeError(f"expected one HEAD git entry for {path}")
    return entry_mode(record, path)


def collect_entries(repo: Path, base_ref: str | None) -> list[CandidateFile]:
    if base_ref:
        return [CandidateFile(path, head_blob_size(repo, path)) for path in comparison_paths(repo, base_ref)]
    return [CandidateFile(path, staged_blob_size(repo, path)) for path in staged_paths(repo)]


def write_summary(
    summary_path: Path,
    entries: list[CandidateFile],
    violations: list[SizeViolation],
    max_bytes: int,
    source: str,
) -> None:
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "max_bytes": max_bytes,
        "allowlist_prefixes": list(ALLOWLIST_PREFIXES),
        "source": source,
        "candidate_file_count": len(entries),
        "violations": [violation.__dict__ for violation in violations],
    }
    summary_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def display_summary_path(summary_path: Path, repo: Path) -> str:
    try:
        return summary_path.resolve().relative_to(repo).as_posix()
    except ValueError:
        return str(summary_path.resolve())


def run_self_tests() -> list[str]:
    failures: list[str] = []
    max_bytes = DEFAULT_MAX_BYTES
    large = max_bytes + 1

    def expect_clean(name: str, entries: list[CandidateFile]) -> None:
        violations = check_entries(entries, max_bytes)
        if violations:
            failures.append(f"{name}: expected clean, got {violations}")

    def expect_violation(name: str, entries: list[CandidateFile], path: str) -> None:
        violations = check_entries(entries, max_bytes)
        if not any(violation.path == path for violation in violations):
            failures.append(f"{name}: expected violation for {path}, got {violations}")

    expect_clean("small source file", [CandidateFile("SkullbonezSource/Foo.cpp", max_bytes)])
    expect_clean("large baseline", [CandidateFile("TestOutput/baselines/large.png", large)])
    expect_clean("large data asset", [CandidateFile("SkullbonezData/assets/large.assets.json", large)])
    expect_clean(
        "retained physics executable",
        [
            CandidateFile(
                "Agentic/Plans/Artifacts/ragdoll-physics-unification/FP2/SKULLBONEZ_CORE-Debug.exe",
                large,
            )
        ],
    )
    expect_violation(
        "unrelated plan executable",
        [CandidateFile("Agentic/Plans/Artifacts/other-plan/SKULLBONEZ_CORE-Debug.exe", large)],
        "Agentic/Plans/Artifacts/other-plan/SKULLBONEZ_CORE-Debug.exe",
    )
    expect_violation("large temp report", [CandidateFile("Agentic/Temp/huge.txt", large)], "Agentic/Temp/huge.txt")
    expect_violation("large root file", [CandidateFile("huge.bin", large)], "huge.bin")
    if entry_mode("160000 deadbeef 0\tThirdPtySource/imgui", "ThirdPtySource/imgui") != "160000":
        failures.append("staged gitlink mode: expected 160000")
    if entry_mode("100644 blob deadbeef\tREADME.md", "README.md") != "100644":
        failures.append("HEAD blob mode: expected 100644")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="Repository root. Defaults to cwd.")
    parser.add_argument("--self-test", action="store_true", help="Run synthetic tests without reading the git index.")
    parser.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES, help="Maximum candidate blob size.")
    parser.add_argument(
        "--base-ref",
        default=None,
        help="Compare added/modified HEAD blobs with merge-base(base-ref, HEAD) instead of reading the index.",
    )
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
        entries = collect_entries(repo, args.base_ref)
    except RuntimeError as exc:
        print(f"ERROR: {exc}")
        return 2

    violations = check_entries(entries, args.max_bytes)
    summary_path = args.json_out or repo / "TestOutput" / "validation" / "staged_file_sizes" / "summary.json"
    if not summary_path.is_absolute():
        summary_path = repo / summary_path
    source = f"diff:{args.base_ref}...HEAD" if args.base_ref else "git-index"
    try:
        write_summary(summary_path, entries, violations, args.max_bytes, source)
    except OSError as exc:
        print(f"ERROR: cannot write file size summary {summary_path}: {exc}")
        return 2

    print(
        f"File size summary: {display_summary_path(summary_path, repo)} "
        f"source={source} candidates={len(entries)} violations={len(violations)}"
    )
    if violations:
        for violation in violations:
            print(
                "ERROR: candidate blob exceeds size budget: "
                f"{violation.path} size={violation.size_bytes} max={violation.max_bytes}"
            )
        return 1
    print("PASS: file size check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
