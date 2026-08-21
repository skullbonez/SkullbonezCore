# File: tools/validate_governance_inventories.py
# Purpose:
#   Runs repository governance inventory self-tests and scans concurrently.
#
# Summary:
#   Executes all 7 governance inventory tools in parallel across worker threads,
#   reducing wall-clock validation time during validate_fast while preserving
#   exact failure attribution, strict-mode exit codes, and stdout/stderr reporting.
#
# Invariants:
#   - Returns non-zero immediately if any inventory self-test or repository scan fails.
#   - Preserves all strict-mode rules, exit codes, and diagnostic outputs.
#
# Related:
#   - AGENTS.md
#   - tools/validate_fast.bat
#   - tools/time_validation_pipeline.py

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys


SELF_TEST_COMMANDS = [
    ["tools/inventory_authority_free_aggregates.py", "--self-test"],
    ["tools/inventory_extraction_scars.py", "--self-test"],
    ["tools/inventory_wide_signatures.py", "--self-test"],
    ["tools/inventory_function_complexity.py", "--self-test"],
    ["tools/inventory_glossary_terms.py", "--self-test"],
    ["tools/check_build_config_consistency.py", "--self-test"],
    ["tools/check_determinism_math_policy.py", "--self-test"],
]

SCAN_COMMANDS = [
    ["tools/inventory_authority_free_aggregates.py", "--strict"],
    ["tools/inventory_extraction_scars.py"],
    ["tools/inventory_wide_signatures.py", "--threshold", "12", "--format", "json", "--strict"],
    ["tools/inventory_function_complexity.py", "--strict"],
    ["tools/inventory_glossary_terms.py", "--strict", "--format", "json"],
    ["tools/check_build_config_consistency.py", "--format", "json"],
    ["tools/check_determinism_math_policy.py", "--format", "json"],
]


def run_command(cmd: list[str], repo: Path) -> tuple[list[str], int, str, str]:
    script_path = repo / cmd[0]
    full_cmd = [sys.executable, str(script_path)] + ["--repo", str(repo)] + cmd[1:]
    result = subprocess.run(
        full_cmd,
        cwd=str(repo),
        capture_output=True,
        text=True,
        errors="replace"
    )
    return cmd, result.returncode, result.stdout, result.stderr


def run_self_test(cmd: list[str], repo: Path) -> tuple[list[str], int, str, str]:
    script_path = repo / cmd[0]
    full_cmd = [sys.executable, str(script_path)] + cmd[1:]
    result = subprocess.run(
        full_cmd,
        cwd=str(repo),
        capture_output=True,
        text=True,
        errors="replace"
    )
    return cmd, result.returncode, result.stdout, result.stderr


def main() -> int:
    parser = argparse.ArgumentParser(description="Concurrent Governance Inventory Validator")
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    args = parser.parse_args()

    repo = args.repo.resolve()

    # Phase A: Self-tests in parallel
    with ThreadPoolExecutor(max_workers=min(len(SELF_TEST_COMMANDS), os.cpu_count() or 4)) as executor:
        self_test_results = list(executor.map(lambda c: run_self_test(c, repo), SELF_TEST_COMMANDS))

    for cmd, code, out, err in self_test_results:
        if code != 0:
            print(f"FAILED self-test: {' '.join(cmd)}", file=sys.stderr)
            if out:
                print(out, file=sys.stderr)
            if err:
                print(err, file=sys.stderr)
            return 8

    # Phase B: Repository scans in parallel
    with ThreadPoolExecutor(max_workers=min(len(SCAN_COMMANDS), os.cpu_count() or 4)) as executor:
        scan_results = list(executor.map(lambda c: run_command(c, repo), SCAN_COMMANDS))

    for cmd, code, out, err in scan_results:
        if code != 0:
            print(f"FAILED governance scan: {' '.join(cmd)}", file=sys.stderr)
            if out:
                print(out, file=sys.stderr)
            if err:
                print(err, file=sys.stderr)
            return 8

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
