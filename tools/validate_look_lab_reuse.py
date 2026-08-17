"""
File: tools/validate_look_lab_reuse.py
Purpose:
  Proves a generated standalone Look Lab style survives a fresh process.

Summary:
  Runs one test process that produces a complete style and a distinct second
  process that loads it through the production parser without invoking the
  generator or any material catalog, then reports the stable artifact hash.

Invariants:
  - Temporary removal is confined to TestOutput/look_lab_fresh_process.
  - Producer and consumer must be distinct successful OS processes.
  - The consumer test compares a complete reserialization with producer bytes.

Related:
  - SkullbonezTests/TestLookLabSerialization.cpp
  - SkullbonezSource/Runtime/Direction/LookLabBundleWriter.cpp
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
OUTPUT_ROOT = REPO_ROOT / "TestOutput" / "look_lab_fresh_process"
STYLE_PATH = OUTPUT_ROOT / "look.style.json"
TEST_NAME = "Look Lab fresh-process output reloads without generator or catalog input"


def run_phase(executable: Path, phase: str) -> int:
    environment = os.environ.copy()
    environment["SKULLBONEZ_LOOK_LAB_FRESH_PROCESS"] = phase
    process = subprocess.Popen(
        [str(executable), f"--test-case={TEST_NAME}", "--no-version", "--no-colors"],
        cwd=REPO_ROOT,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    output, _ = process.communicate()
    sys.stdout.write(output)

    if process.returncode != 0:
        raise RuntimeError(f"{phase} process failed with exit code {process.returncode}")

    print(f"[look-lab-reuse] phase={phase} pid={process.pid} status=pass")
    return process.pid


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--configuration", choices=("Debug", "Profile"), default="Profile")
    args = parser.parse_args()
    executable = REPO_ROOT / args.configuration / "SKULLBONEZ_TESTS.exe"

    if not executable.is_file():
        parser.error(f"missing test executable: {executable}")

    shutil.rmtree(OUTPUT_ROOT, ignore_errors=True)

    try:
        producer_pid = run_phase(executable, "produce")

        if not STYLE_PATH.is_file():
            raise RuntimeError(f"producer did not create {STYLE_PATH}")

        produced_hash = hashlib.sha256(STYLE_PATH.read_bytes()).hexdigest()
        consumer_pid = run_phase(executable, "consume")

        if producer_pid == consumer_pid:
            raise RuntimeError("producer and consumer unexpectedly reused one process")

        consumed_hash = hashlib.sha256(STYLE_PATH.read_bytes()).hexdigest()

        if consumed_hash != produced_hash:
            raise RuntimeError("style bytes changed between producer and consumer")

        print(
            f"[look-lab-reuse] configuration={args.configuration} "
            f"sha256={produced_hash} distinct_processes=1 status=pass"
        )
        return 0
    finally:
        shutil.rmtree(OUTPUT_ROOT, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
