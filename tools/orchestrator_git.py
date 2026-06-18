#
# File: tools/orchestrator_git.py
# Purpose:
#   Contains git subprocess helpers used by the roadmap orchestrator.
#
# Mental model:
#   This module owns low-level git command execution and small convenience
#   queries. The orchestrator remains responsible for policy decisions and
#   user-facing error messages.
#
# Invariants:
#   - Helpers must not mutate repository state unless their name makes that
#     behavior explicit through git arguments supplied by the caller.
#   - Policy checks, such as whether committing on main is allowed, stay in
#     tools/orchestrator.py.
#
# Related:
#   - tools/orchestrator.py
#   - Agentic/Orchestrator/README.md
#

from __future__ import annotations

import subprocess
from pathlib import Path


def run_git(repo: Path, git_args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *git_args],
        cwd=repo,
        check=False,
        capture_output=True,
        text=True,
    )


def git_status(repo: Path) -> str:
    result = run_git(repo, ["status", "--short", "--branch"])
    if result.returncode != 0:
        return result.stderr.strip() or "git status failed"
    return result.stdout.strip()


def git_changed_files(repo: Path) -> list[str]:
    result = run_git(repo, ["diff", "--name-only"])
    if result.returncode != 0:
        return []
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def latest_commit(repo: Path) -> str:
    result = run_git(repo, ["rev-parse", "--short", "HEAD"])
    return result.stdout.strip() if result.returncode == 0 else ""


def git_has_staged_changes(repo: Path) -> bool:
    result = run_git(repo, ["diff", "--cached", "--quiet"])
    return result.returncode == 1
