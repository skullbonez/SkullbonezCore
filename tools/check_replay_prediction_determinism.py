#
# File: tools/check_replay_prediction_determinism.py
# Purpose:
#   Runs the replay prediction interaction probe twice and compares the sampled
#   trajectory fingerprint emitted by the automation report.
#
# Mental model:
#   The runtime owns prediction and trajectory generation. This tool only drives
#   the same interaction script twice from a clean process boundary, then checks
#   that the report-side sampled polyline hash and counts are identical.
#
# Glossary:
#   Prediction fingerprint: FNV-1a (Fowler-Noll-Vo variant) hash of published
#     trajectory records and points, excluding transient record versions and
#     vector capacity.
#   Interaction report: JSON artifact written by --interaction-report after a
#     scripted validation launch finishes.
#
# Invariants:
#   - Both launches must use the same scene, script, fixed-step policy, and frame
#     count before their fingerprints are compared.
#   - The probe fails if the prediction did not become visible or if the
#     fingerprint was not ready in either run.
#   - The active published prefix count is part of the compared summary, so a
#     timing drift that exposes one extra prediction frame still fails.
#
# Related:
#   - SkullbonezSource/Runtime/RunInteractionAutomation.cpp
#   - SkullbonezData/interaction/prediction_determinism_probe.json
#

import json
import os
from pathlib import Path
import subprocess
import sys


REPO = Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1])).resolve()
EXE = REPO / "Debug" / "SKULLBONEZ_CORE.exe"
SCENE = "SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json"
SCRIPT = "SkullbonezData/interaction/prediction_determinism_probe.json"
OUT_DIR = REPO / "TestOutput" / "validation" / "replay_prediction_determinism"
REPORTS = [OUT_DIR / "prediction_determinism_a.json", OUT_DIR / "prediction_determinism_b.json"]
STDOUT_LOGS = [OUT_DIR / "prediction_determinism_a.stdout.txt", OUT_DIR / "prediction_determinism_b.stdout.txt"]
STDERR_LOGS = [OUT_DIR / "prediction_determinism_a.stderr.txt", OUT_DIR / "prediction_determinism_b.stderr.txt"]
MAX_LOG_CHARS = 60000
LOG_HEAD_CHARS = 20000
LOG_TAIL_CHARS = MAX_LOG_CHARS - LOG_HEAD_CHARS
MIN_ACTIVE_PREDICTION_FRAMES = 260


def remove_if_exists(path):
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def display_command(args):
    relative = []
    for arg in args:
        text = str(arg)
        try:
            relative.append(str(Path(text).resolve().relative_to(REPO)))
        except (OSError, ValueError):
            relative.append(text)
    return " ".join(relative)


def bounded_output(text):
    if len(text) <= MAX_LOG_CHARS:
        return text
    omitted = len(text) - MAX_LOG_CHARS
    return (
        text[:LOG_HEAD_CHARS]
        + f"\n\n[check_replay_prediction_determinism truncated {omitted} characters]\n\n"
        + text[-LOG_TAIL_CHARS:]
    )


def run_prediction_probe(label, report, stdout_log, stderr_log):
    if not EXE.exists():
        raise RuntimeError(f"Debug executable not found: {EXE}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    remove_if_exists(report)
    remove_if_exists(stdout_log)
    remove_if_exists(stderr_log)

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
        str(report),
        "--frames",
        "320",
        "--replay",
        "on",
        "--replay-seconds",
        "3",
        "--fixed-step",
    ]
    print(f"  {label} command:")
    print("    " + display_command(command))
    result = subprocess.run(
        command,
        cwd=str(REPO),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    bounded_stdout = bounded_output(result.stdout)
    bounded_stderr = bounded_output(result.stderr)
    stdout_log.write_text(bounded_stdout, encoding="utf-8")
    stderr_log.write_text(bounded_stderr, encoding="utf-8")
    if result.returncode != 0:
        print(bounded_stdout, end="")
        print(bounded_stderr, end="", file=sys.stderr)
        raise RuntimeError(f"{label} launch failed with exit code {result.returncode}")
    if not report.exists():
        raise RuntimeError(f"{label} did not write interaction report: {report}")
    payload = json.loads(report.read_text(encoding="utf-8"))
    if not payload.get("ok"):
        raise RuntimeError(f"{label} interaction report failed: {payload.get('failure', '')}")
    return payload


def require_final_state(label, payload):
    final = payload.get("finalState") or {}
    required_bools = {
        "predictionPathVisible": True,
        "liveSolverHashStableAcrossPrediction": True,
        "predictionTrajectoryFingerprintReady": True,
    }
    for key, expected in required_bools.items():
        actual = final.get(key)
        if actual != expected:
            raise RuntimeError(f"{label} finalState.{key} expected {expected}, got {actual}")

    fingerprint = final.get("predictionTrajectoryFingerprint")
    record_count = int(final.get("predictionTrajectoryRecordCount") or 0)
    point_count = int(final.get("predictionTrajectoryPointCount") or 0)
    if not isinstance(fingerprint, str) or not fingerprint.startswith("0x"):
        raise RuntimeError(f"{label} missing predictionTrajectoryFingerprint")
    if record_count <= 0 or point_count <= 0:
        raise RuntimeError(f"{label} produced empty trajectory fingerprint counts")
    active_frame_count = int(final.get("predictionActiveFrameCount") or 0)
    if active_frame_count < MIN_ACTIVE_PREDICTION_FRAMES:
        raise RuntimeError(
            f"{label} reported only {active_frame_count} active prediction frames; "
            f"expected at least {MIN_ACTIVE_PREDICTION_FRAMES}"
        )
    return {
        "fingerprint": fingerprint,
        "record_count": record_count,
        "point_count": point_count,
        "future_node_count": int(final.get("predictionFutureNodeCount") or 0),
        "future_node_build_frame_count": int(final.get("predictionFutureNodeBuildFrameCount") or 0),
        "active_frame_count": active_frame_count,
    }


def main():
    try:
        first = run_prediction_probe("Run A", REPORTS[0], STDOUT_LOGS[0], STDERR_LOGS[0])
        second = run_prediction_probe("Run B", REPORTS[1], STDOUT_LOGS[1], STDERR_LOGS[1])
        first_summary = require_final_state("Run A", first)
        second_summary = require_final_state("Run B", second)
        if first_summary != second_summary:
            raise RuntimeError(
                "prediction fingerprints differed:\n"
                + json.dumps({"run_a": first_summary, "run_b": second_summary}, indent=2, sort_keys=True)
            )

        print(
            "  PASS: prediction trajectory fingerprint {fingerprint} matched across two runs "
            "({records} records, {points} points, {frames} active frames)".format(
                fingerprint=first_summary["fingerprint"],
                records=first_summary["record_count"],
                points=first_summary["point_count"],
                frames=first_summary["active_frame_count"],
            )
        )
        print(f"  Report A bytes: {REPORTS[0].stat().st_size}")
        print(f"  Report B bytes: {REPORTS[1].stat().st_size}")
        print(f"  Stdout A bytes: {STDOUT_LOGS[0].stat().st_size}")
        print(f"  Stdout B bytes: {STDOUT_LOGS[1].stat().st_size}")
        return 0
    except Exception as exc:
        print(f"  FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
