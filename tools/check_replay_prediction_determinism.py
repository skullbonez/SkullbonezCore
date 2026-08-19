#
# File: tools/check_replay_prediction_determinism.py
# Purpose:
#   Runs the replay prediction interaction probe with inline and one-worker
#   execution, then compares private values and rendered trajectory witnesses.
#
# Summary:
#   The runtime owns prediction and trajectory generation. This tool drives the
#   same interaction script from clean process boundaries with worker counts 0
#   and 1, then checks that private body/contact values and submitted geometry
#   are identical. The reveal-paced trajectory-store fingerprint is readiness
#   evidence only because its visible prefix legitimately follows wall time.
#
# Glossary:
#   Private simulation hash: FNV-1a (Fowler-Noll-Vo variant) hash of the bounded
#     prediction's deterministic frame indices and body/contact values.
#   Submission fingerprint: FNV-1a hash of the exact replay-ribbon vertex bytes
#     submitted by the tracer after the reveal/build path reaches a steady
#     window.
#   Interaction report: JSON artifact written by --interaction-report after a
#     scripted validation launch finishes.
#   Steady window: Consecutive rendered frames where submitted geometry and the
#     replay reserve-growth counter remain unchanged.
#   Replay reserve growth: Approved runtime capacity increase for replay-owned
#     buffers, counted by RuntimeReserveAllocator.
#
# Invariants:
#   - Both launches use the same scene, script, fixed-step policy, and frame
#     count; only the worker-count execution policy differs.
#   - The probe fails if the full 120-second prediction did not become visible or
#     if either private-value or submitted-geometry witness is absent.
#   - The submitted geometry probe must find a 120-frame steady window with no
#     replay reserve growth, so draw-side flicker and steady-state allocation
#     regressions fail the scrub gate.
#
# Related:
#   - SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp
#   - SkullbonezData/interaction/continuous_orbit_of0_baseline.json
#

import json
import os
from pathlib import Path
import subprocess
import sys


REPO = Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1])).resolve()
EXE = REPO / "Automation" / "SKULLBONEZ_CORE.exe"
SCENE = "SkullbonezData/scenes/solar_system.scene.json"
SCRIPT = "SkullbonezData/interaction/continuous_orbit_of0_baseline.json"
OUT_DIR = REPO / "TestOutput" / "validation" / "replay_prediction_determinism"
REPORTS = [OUT_DIR / "prediction_determinism_a.json", OUT_DIR / "prediction_determinism_b.json"]
STDOUT_LOGS = [OUT_DIR / "prediction_determinism_a.stdout.txt", OUT_DIR / "prediction_determinism_b.stdout.txt"]
STDERR_LOGS = [OUT_DIR / "prediction_determinism_a.stderr.txt", OUT_DIR / "prediction_determinism_b.stderr.txt"]
MAX_LOG_CHARS = 60000
LOG_HEAD_CHARS = 20000
LOG_TAIL_CHARS = MAX_LOG_CHARS - LOG_HEAD_CHARS
MIN_ACTIVE_PREDICTION_FRAMES = 14401
MIN_SUBMISSION_STABLE_FRAMES = 120


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


def run_prediction_probe(label, worker_count, report, stdout_log, stderr_log):
    if not EXE.exists():
        raise RuntimeError(f"Automation executable not found: {EXE}")

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
        "2602",
        "--replay",
        "on",
        "--replay-seconds",
        "120",
        "--fixed-step",
        "--workers",
        str(worker_count),
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
        "predictionTrajectoryFingerprintReady": True,
        "predictionTrajectorySubmissionStable": True,
        "predictionTrajectorySteadyStateNoReserveGrowth": True,
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
    submission_hash = final.get("predictionTrajectorySubmissionHash")
    submission_frame_count = int(final.get("predictionTrajectorySubmissionFrameCount") or 0)
    submission_vertex_bytes = int(final.get("predictionTrajectorySubmissionVertexBytes") or 0)
    submission_vertex_count = int(final.get("predictionTrajectorySubmissionVertexCount") or 0)
    submission_segment_count = int(final.get("predictionTrajectorySubmissionSegmentCount") or 0)
    reserve_growth_start = int(final.get("predictionTrajectoryReserveGrowthEventsAtStart") or 0)
    reserve_growth_end = int(final.get("predictionTrajectoryReserveGrowthEventsAtEnd") or 0)
    if submission_frame_count < MIN_SUBMISSION_STABLE_FRAMES:
        raise RuntimeError(
            f"{label} reported only {submission_frame_count} stable submitted-geometry frames; "
            f"expected at least {MIN_SUBMISSION_STABLE_FRAMES}"
        )
    if not isinstance(submission_hash, str) or not submission_hash.startswith("0x"):
        raise RuntimeError(f"{label} missing predictionTrajectorySubmissionHash")
    if submission_vertex_bytes <= 0 or submission_vertex_count <= 0 or submission_segment_count <= 0:
        raise RuntimeError(f"{label} produced empty submitted-geometry probe counts")
    if reserve_growth_start != reserve_growth_end:
        raise RuntimeError(
            f"{label} replay reserve growth changed during steady window: "
            f"{reserve_growth_start} -> {reserve_growth_end}"
        )
    private_hash_keys = (
        "predictionPrivateSimulationHash",
        "predictionPrivateFrameIndexHash",
        "predictionPrivatePoseHash",
        "predictionPrivateVelocityHash",
        "predictionPrivateSleepHash",
        "predictionPrivateContactHash",
    )
    for key in private_hash_keys:
        value = final.get(key)
        if not isinstance(value, str) or not value.startswith("0x"):
            raise RuntimeError(f"{label} missing {key}")
    return {
        "private_simulation_hash": final.get("predictionPrivateSimulationHash"),
        "private_frame_index_hash": final.get("predictionPrivateFrameIndexHash"),
        "private_pose_hash": final.get("predictionPrivatePoseHash"),
        "private_velocity_hash": final.get("predictionPrivateVelocityHash"),
        "private_sleep_hash": final.get("predictionPrivateSleepHash"),
        "private_contact_hash": final.get("predictionPrivateContactHash"),
        "active_frame_count": active_frame_count,
        "submission_hash": submission_hash,
        "submission_vertex_bytes": submission_vertex_bytes,
        "submission_vertex_count": submission_vertex_count,
        "submission_segment_count": submission_segment_count,
    }


def main():
    try:
        first = run_prediction_probe("Inline run", 0, REPORTS[0], STDOUT_LOGS[0], STDERR_LOGS[0])
        second = run_prediction_probe("One-worker run", 1, REPORTS[1], STDOUT_LOGS[1], STDERR_LOGS[1])
        first_summary = require_final_state("Inline run", first)
        second_summary = require_final_state("One-worker run", second)
        if first_summary != second_summary:
            raise RuntimeError(
                "prediction fingerprints differed:\n"
                + json.dumps({"run_a": first_summary, "run_b": second_summary}, indent=2, sort_keys=True)
            )

        print(
            "  PASS: private prediction values matched across inline and one-worker runs "
            "({frames} active frames); "
            "submitted geometry {submission_hash} held for >= {stable_frames} frames".format(
                frames=first_summary["active_frame_count"],
                submission_hash=first_summary["submission_hash"],
                stable_frames=MIN_SUBMISSION_STABLE_FRAMES,
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
