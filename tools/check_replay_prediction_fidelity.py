#
# File: tools/check_replay_prediction_fidelity.py
# Purpose:
#   Runs the fixed-step prediction fidelity interaction probe and verifies that
#   its frozen predicted solver hashes match the later retained live horizon.
#
# Summary:
#   The runtime predicts from seed tick T, then live simulation advances with no
#   further input. The interaction assertion compares prediction[T+k] with the
#   retained live solver sample T+k for every requested tick.
#
# Glossary:
#   Fidelity horizon: Consecutive future ticks compared byte-exactly by solver
#     hash after live simulation reaches them.
#   Interaction report: Bounded JSON artifact written by the runtime validation
#     driver after the scripted launch.
#
# Invariants:
#   - The launch uses fixed-step simulation; after the harness releases replay,
#     no physics-mutating or replay-event action occurs in the compared window.
#   - A missing, vacuous, evicted, or mismatched horizon is a hard failure.
#   - Captured stdout/stderr are bounded before they are persisted or printed.
#
# Related:
#   - SkullbonezData/interaction/prediction_fidelity_probe.json
#   - SkullbonezSource/Runtime/InteractionAutomationController.cpp
#

import json
import os
from pathlib import Path
import subprocess
import sys


REPO = Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1])).resolve()
EXE = REPO / "Debug" / "SKULLBONEZ_CORE.exe"
SCENE = "SkullbonezData/scenes/three_body_chaos.scene.json"
SCRIPT = "SkullbonezData/interaction/prediction_fidelity_probe.json"
OUT_DIR = REPO / "TestOutput" / "validation" / "replay_prediction_fidelity"
REPORT = OUT_DIR / "prediction_fidelity.json"
STDOUT_LOG = OUT_DIR / "prediction_fidelity.stdout.txt"
STDERR_LOG = OUT_DIR / "prediction_fidelity.stderr.txt"
MAX_LOG_CHARS = 60000
LOG_HEAD_CHARS = 20000
LOG_TAIL_CHARS = MAX_LOG_CHARS - LOG_HEAD_CHARS


def bounded_output(text):
    if len(text) <= MAX_LOG_CHARS:
        return text
    omitted = len(text) - MAX_LOG_CHARS
    return (
        text[:LOG_HEAD_CHARS]
        + f"\n\n[check_replay_prediction_fidelity truncated {omitted} characters]\n\n"
        + text[-LOG_TAIL_CHARS:]
    )


def remove_if_exists(path):
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def display_command(args):
    displayed = []
    for arg in args:
        text = str(arg)
        try:
            displayed.append(str(Path(text).resolve().relative_to(REPO)))
        except (OSError, ValueError):
            displayed.append(text)
    return " ".join(displayed)


def run_probe():
    if not EXE.exists():
        raise RuntimeError(f"Debug executable not found: {EXE}")
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for path in (REPORT, STDOUT_LOG, STDERR_LOG):
        remove_if_exists(path)

    command = [
        str(EXE),
        "--renderer",
        "dx12",
        "--vsync",
        "off",
        "--shadows",
        "off",
        "--hide-top-text",
        "--scene",
        SCENE,
        "--interaction-script",
        SCRIPT,
        "--interaction-report",
        str(REPORT),
        "--frames",
        "260",
        "--replay",
        "on",
        "--replay-seconds",
        "3",
        "--fixed-step",
    ]
    print("  Fidelity command:")
    print("    " + display_command(command))
    result = subprocess.run(
        command,
        cwd=str(REPO),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    stdout = bounded_output(result.stdout)
    stderr = bounded_output(result.stderr)
    STDOUT_LOG.write_text(stdout, encoding="utf-8")
    STDERR_LOG.write_text(stderr, encoding="utf-8")
    if result.returncode != 0:
        print(stdout, end="")
        print(stderr, end="", file=sys.stderr)
        raise RuntimeError(f"fidelity launch failed with exit code {result.returncode}")
    if not REPORT.exists():
        raise RuntimeError(f"interaction report was not produced: {REPORT}")
    return json.loads(REPORT.read_text(encoding="utf-8"))


def require_fidelity_assertion(payload):
    if not payload.get("ok"):
        raise RuntimeError(f"interaction report failed: {payload.get('failure', '')}")
    matches = [
        row
        for row in (payload.get("assertions") or [])
        if row.get("name") == "predictionMatchesLiveHorizon"
    ]
    if len(matches) != 1:
        raise RuntimeError(f"expected one predictionMatchesLiveHorizon assertion, found {len(matches)}")
    assertion = matches[0]
    if assertion.get("passed") is not True:
        raise RuntimeError(f"fidelity assertion failed: {json.dumps(assertion, sort_keys=True)}")
    if assertion.get("actual") != "all hashes matched":
        raise RuntimeError(f"fidelity assertion did not report a complete comparison: {assertion.get('actual')}")
    return assertion


def main():
    try:
        payload = run_probe()
        assertion = require_fidelity_assertion(payload)
        print(
            "  PASS: {actual} ({expected}, assertion frame {frame})".format(
                actual=assertion.get("actual"),
                expected=assertion.get("expected"),
                frame=assertion.get("frame"),
            )
        )
        print(f"  Report bytes: {REPORT.stat().st_size}")
        print(f"  Stdout bytes: {STDOUT_LOG.stat().st_size}")
        print(f"  Stderr bytes: {STDERR_LOG.stat().st_size}")
        return 0
    except Exception as exc:
        print(f"  FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
