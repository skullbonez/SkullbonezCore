"""
File: tools/analyze_replay_prediction_spikes.py
Purpose:
  Prepares, bounds, and analyzes the replay-prediction frame-spike diagnostic.

Summary:
  Generates an isolated perf-enabled scene copy, launches the engine under a
  caller-selected watchdog, verifies prediction interactions do not overlap,
  attributes the largest frames, and reports named marker ranges plus final
  prediction-oracle facts without imposing a performance budget.

Invariants:
  - The source scene is read-only; perf logging is injected into TestOutput.
  - The engine process is killed when its diagnostic watchdog expires.
  - Dynamic CSV headers govern only the rows that follow them.
  - Spike magnitude is diagnostic data and never changes the process exit code.
  - Harness and target-restart marker groups remain explicitly excluded evidence.

Related:
  - SkullbonezData/interaction/replay_prediction_120s_frame_spike.json
  - tools/validate_replay_prediction_frame_spikes.bat
  - SkullbonezSource/Core/Profiler.cpp
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path
from typing import Any


MARKER_GROUPS = (
    (
        "completion_trajectory_publication",
        "Frame/Replay/Prediction/PublishCompletedFrame/TrajectoryStore",
        False,
    ),
    (
        "child_marker_context",
        "Frame/Replay/Prediction/PrepareOverlay/BuildChildMarkerContext",
        False,
    ),
    (
        "predict_off_cache_clear",
        "Frame/Replay/Prediction/ClearCache",
        False,
    ),
    (
        "automation_report_serialization",
        "Frame/PostDraw/InteractionAutomation",
        True,
    ),
    (
        "prediction_restart_input",
        "Frame/Input",
        True,
    ),
)


def prepare_scene(source: Path, output: Path, perf_log: str) -> None:
    payload = json.loads(source.read_text(encoding="utf-8"))
    payload["logging"] = {
        "perfLog": perf_log,
        "perfLogFlush": False,
        "perfLogFlushInterval": 0,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def run_workload(
    executable: Path,
    scene: Path,
    script: Path,
    report: Path,
    log: Path,
    *,
    frames: int,
    timeout_seconds: float,
) -> None:
    if frames <= 0:
        raise ValueError("workload frame count must be positive")

    if timeout_seconds <= 0.0:
        raise ValueError("workload timeout must be positive")

    command = [
        str(executable),
        "--renderer",
        "dx12",
        "--vsync",
        "off",
        "--shadows",
        "off",
        "--cinematic",
        "off",
        "--hide-top-text",
        "--automation-hidden-window",
        "--scene",
        str(scene),
        "--interaction-script",
        str(script),
        "--interaction-report",
        str(report),
        "--frames",
        str(frames),
        "--replay",
        "on",
        "--replay-seconds",
        "121",
        "--fixed-step",
    ]
    log.parent.mkdir(parents=True, exist_ok=True)

    try:
        with log.open("w", encoding="utf-8") as output:
            result = subprocess.run(
                command,
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=timeout_seconds,
                check=False,
            )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f"replay-prediction workload exceeded {timeout_seconds:g} seconds") from exc

    if result.returncode != 0:
        raise RuntimeError(f"replay-prediction workload exited with code {result.returncode}")


def parse_perf_csv(path: Path) -> list[dict[str, Any]]:
    active_columns: list[str] = []
    rows: list[dict[str, Any]] = []

    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.strip()

        if not line or line.startswith("#"):
            continue

        columns = line.split(",")

        if line.startswith("pass,frame,"):
            active_columns = columns[2:]
            continue

        if not active_columns:
            raise ValueError(f"perf row precedes its header at {path}:{line_number}")

        expected_count = len(active_columns) + 2

        if len(columns) != expected_count:
            raise ValueError(
                f"perf row at {path}:{line_number} has {len(columns)} columns; expected {expected_count}"
            )

        timings = {name: float(value) for name, value in zip(active_columns, columns[2:])}
        rows.append({"pass": int(columns[0]), "frame": int(columns[1]), "timings": timings})

    if not rows:
        raise ValueError(f"no profiler frame rows found in {path}")

    return rows


def _cpu_marker_names(timings: dict[str, float]) -> list[str]:
    return [
        name
        for name in timings
        if not name.startswith("Counter/") and not name.endswith("_worker") and not name.endswith("_gpu")
    ]


def _immediate_children(parent: str, marker_names: list[str]) -> list[str]:
    prefix = parent + "/"
    children = [name for name in marker_names if name.startswith(prefix)]
    return [
        name
        for name in children
        if not any(other != name and name.startswith(other + "/") for other in children)
    ]


def _unattributed_frame_ms(timings: dict[str, float]) -> float:
    marker_names = _cpu_marker_names(timings)
    direct_children_ms = sum(timings[child] for child in _immediate_children("Frame", marker_names))
    return round(max(0.0, timings.get("Frame", 0.0) - direct_children_ms), 4)


def _direct_cpu_markers(timings: dict[str, float], limit: int = 8) -> list[dict[str, Any]]:
    marker_names = _cpu_marker_names(timings)
    direct: list[dict[str, Any]] = []

    for name in marker_names:
        if name == "Frame":
            continue

        inclusive_ms = timings[name]
        child_ms = sum(timings[child] for child in _immediate_children(name, marker_names))
        direct_ms = max(0.0, inclusive_ms - child_ms)

        if direct_ms <= 0.0:
            continue

        direct.append(
            {
                "name": name,
                "ms": round(direct_ms, 4),
                "inclusive_ms": round(inclusive_ms, 4),
            }
        )

    direct.sort(key=lambda marker: marker["ms"], reverse=True)
    return direct[:limit]


def _worker_markers(timings: dict[str, float], limit: int = 8) -> list[dict[str, Any]]:
    workers = [
        {"name": name, "ms": round(value, 4)}
        for name, value in timings.items()
        if name.endswith("_worker") and value > 0.0
    ]
    workers.sort(key=lambda marker: marker["ms"], reverse=True)
    return workers[:limit]


def _events(interaction: dict[str, Any]) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []

    for action in interaction.get("actions", []):
        events.append(
            {
                "frame": int(action["frame"]),
                "type": action.get("type", "action"),
                "target": action.get("target", ""),
                "passed": action.get("consumed", True),
            }
        )

    for assertion in interaction.get("assertions", []):
        events.append(
            {
                "frame": int(assertion["frame"]),
                "type": f"assert:{assertion.get('name', 'unknown')}",
                "target": assertion.get("expected", ""),
                "passed": assertion.get("passed", False),
            }
        )

    events.sort(key=lambda event: event["frame"])
    return events


def _percentile(values: list[float], percentile: float) -> float:
    if not values:
        return 0.0

    ordered = sorted(values)
    position = (len(ordered) - 1) * percentile
    lower = math.floor(position)
    upper = math.ceil(position)

    if lower == upper:
        return ordered[lower]

    fraction = position - lower
    return ordered[lower] + (ordered[upper] - ordered[lower]) * fraction


def _marker_groups(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    groups: list[dict[str, Any]] = []

    for name, marker, excluded in MARKER_GROUPS:
        observations = [row for row in rows if row["timings"].get(marker, 0.0) > 0.0]

        if not observations:
            groups.append(
                {
                    "name": name,
                    "marker": marker,
                    "excluded": excluded,
                    "observation_count": 0,
                    "frame_ms": None,
                    "marker_ms": None,
                    "worst_frame": None,
                }
            )
            continue

        worst = max(observations, key=lambda row: row["timings"][marker])
        frame_values = [row["timings"].get("Frame", 0.0) for row in observations]
        marker_values = [row["timings"][marker] for row in observations]
        groups.append(
            {
                "name": name,
                "marker": marker,
                "excluded": excluded,
                "observation_count": len(observations),
                "frame_ms": {
                    "minimum": round(min(frame_values), 4),
                    "maximum": round(max(frame_values), 4),
                },
                "marker_ms": {
                    "minimum": round(min(marker_values), 4),
                    "maximum": round(max(marker_values), 4),
                },
                "worst_frame": worst["frame"],
            }
        )

    groups.sort(
        key=lambda group: group["marker_ms"]["maximum"] if group["marker_ms"] is not None else -1.0,
        reverse=True,
    )
    return groups


def analyze(
    rows: list[dict[str, Any]],
    interaction: dict[str, Any],
    *,
    top_count: int = 20,
    correlation_radius: int = 4,
) -> dict[str, Any]:
    frame_rows = [row for row in rows if "Frame" in row["timings"]]

    if not frame_rows:
        raise ValueError("profiler CSV contains no Frame marker")

    frame_rows.sort(key=lambda row: row["timings"]["Frame"], reverse=True)
    events = _events(interaction)
    spikes: list[dict[str, Any]] = []

    for row in frame_rows[:top_count]:
        frame = row["frame"]
        nearby_events = [event for event in events if abs(event["frame"] - frame) <= correlation_radius]
        spikes.append(
            {
                "pass": row["pass"],
                "frame": frame,
                "frame_ms": round(row["timings"]["Frame"], 4),
                "unattributed_frame_ms": _unattributed_frame_ms(row["timings"]),
                "nearby_events": nearby_events,
                "direct_cpu_markers": _direct_cpu_markers(row["timings"]),
                "worker_markers": _worker_markers(row["timings"]),
            }
        )

    frame_values = [row["timings"]["Frame"] for row in frame_rows]
    final_state = interaction.get("finalState", {})
    return {
        "schema_version": 2,
        "diagnostic_only": True,
        "automation_ok": bool(interaction.get("ok", False)),
        "frames_analyzed": len(frame_rows),
        "frame_ms": {
            "maximum": round(max(frame_values), 4),
            "p99": round(_percentile(frame_values, 0.99), 4),
            "p99_9": round(_percentile(frame_values, 0.999), 4),
        },
        "prediction_generation_count": final_state.get("predictionGenerationCount"),
        "prediction_oracle": {
            "trajectory_fingerprint_ready": final_state.get("predictionTrajectoryFingerprintReady"),
            "trajectory_fingerprint": final_state.get("predictionTrajectoryFingerprint"),
            "trajectory_record_count": final_state.get("predictionTrajectoryRecordCount"),
            "trajectory_point_count": final_state.get("predictionTrajectoryPointCount"),
            "steady_state_no_reserve_growth": final_state.get("predictionTrajectorySteadyStateNoReserveGrowth"),
            "reserve_growth_events_at_start": final_state.get("predictionTrajectoryReserveGrowthEventsAtStart"),
            "reserve_growth_events_at_end": final_state.get("predictionTrajectoryReserveGrowthEventsAtEnd"),
        },
        "marker_groups": _marker_groups(frame_rows),
        "spikes": spikes,
    }


def validate_interaction_actions(actions: list[dict[str, Any]]) -> None:
    prediction_enabled = False
    prediction_pending = False
    horizon_is_120_seconds = False

    for action in sorted(actions, key=lambda entry: int(entry["frame"])):
        frame = int(action["frame"])
        assertion = action.get("assert")

        if isinstance(assertion, dict) and assertion.get("predictionFullHorizonComplete") is True:
            prediction_pending = False
            continue

        interaction_name = next(
            (
                name
                for name in (
                    "setReplayPathTarget",
                    "setReplayPredictionHorizonSeconds",
                    "clickReplayControl",
                    "scrubReplaySolverTrack",
                )
                if name in action
            ),
            None,
        )

        if interaction_name is not None and prediction_pending:
            raise ValueError(f"frame {frame} performs {interaction_name} before the active prediction completed")

        if "setReplayPredictionHorizonSeconds" in action:
            horizon_is_120_seconds = float(action["setReplayPredictionHorizonSeconds"]) == 120.0

            if prediction_enabled:
                prediction_pending = True

        if "setReplayPathTarget" in action and prediction_enabled:
            prediction_pending = True

        if action.get("clickReplayControl") == "predict":
            prediction_enabled = not prediction_enabled
            prediction_pending = prediction_enabled

    if not horizon_is_120_seconds:
        raise ValueError("interaction script must set a 120-second prediction horizon")

    if prediction_pending:
        raise ValueError("interaction script ends before the active prediction completed")


def validate_interaction_script(path: Path) -> None:
    payload = json.loads(path.read_text(encoding="utf-8"))
    actions = payload.get("actions")

    if not isinstance(actions, list):
        raise ValueError(f"interaction script has no actions array: {path}")

    validate_interaction_actions(actions)


def print_report(report: dict[str, Any]) -> None:
    frame_stats = report["frame_ms"]
    print(
        "Replay prediction frame-spike diagnostic: "
        f"frames={report['frames_analyzed']} generations={report['prediction_generation_count']} "
        f"max={frame_stats['maximum']:.4f} ms p99={frame_stats['p99']:.4f} ms "
        f"p99.9={frame_stats['p99_9']:.4f} ms"
    )

    print("\nGrouped marker ranges (worst marker first):")

    for group in report["marker_groups"]:
        scope = "excluded" if group["excluded"] else "in scope"

        if group["marker_ms"] is None:
            print(f"  {group['name']}: not observed ({scope})")
            continue

        print(
            f"  {group['name']}: marker {group['marker_ms']['minimum']:.4f}-"
            f"{group['marker_ms']['maximum']:.4f} ms; frame {group['frame_ms']['minimum']:.4f}-"
            f"{group['frame_ms']['maximum']:.4f} ms; observations={group['observation_count']} "
            f"worst_frame={group['worst_frame']} ({scope})"
        )

    for spike in report["spikes"]:
        print(
            f"\nframe {spike['frame']}: {spike['frame_ms']:.4f} ms "
            f"({spike['unattributed_frame_ms']:.4f} ms outside child markers)"
        )

        for event in spike["nearby_events"]:
            target = f" {event['target']}" if event["target"] != "" else ""
            print(f"  event {event['frame']}: {event['type']}{target}")

        for marker in spike["direct_cpu_markers"][:5]:
            print(f"  cpu {marker['ms']:.4f} ms direct ({marker['inclusive_ms']:.4f} ms inclusive): {marker['name']}")

        for marker in spike["worker_markers"][:5]:
            print(f"  worker {marker['ms']:.4f} ms: {marker['name']}")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Prepare or analyze the replay-prediction spike diagnostic.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    prepare = subparsers.add_parser("prepare-scene")
    prepare.add_argument("--source", type=Path, required=True)
    prepare.add_argument("--output", type=Path, required=True)
    prepare.add_argument("--perf-log", required=True)

    validate = subparsers.add_parser("validate-script")
    validate.add_argument("--script", type=Path, required=True)

    run = subparsers.add_parser("run-workload")
    run.add_argument("--executable", type=Path, required=True)
    run.add_argument("--scene", type=Path, required=True)
    run.add_argument("--script", type=Path, required=True)
    run.add_argument("--report", type=Path, required=True)
    run.add_argument("--log", type=Path, required=True)
    run.add_argument("--frames", type=int, required=True)
    run.add_argument("--timeout-seconds", type=float, required=True)

    analyze_parser = subparsers.add_parser("analyze")
    analyze_parser.add_argument("--csv", type=Path, required=True)
    analyze_parser.add_argument("--interaction-report", type=Path, required=True)
    analyze_parser.add_argument("--output", type=Path, required=True)
    analyze_parser.add_argument("--top-count", type=int, default=20)
    analyze_parser.add_argument("--correlation-radius", type=int, default=4)
    return parser


def main() -> int:
    args = _build_parser().parse_args()

    if args.command == "prepare-scene":
        prepare_scene(args.source, args.output, args.perf_log)
        print(f"Prepared diagnostic scene: {args.output}")
        return 0

    if args.command == "validate-script":
        validate_interaction_script(args.script)
        print(f"Validated non-overlapping 120-second prediction sequence: {args.script}")
        return 0

    if args.command == "run-workload":
        run_workload(
            args.executable,
            args.scene,
            args.script,
            args.report,
            args.log,
            frames=args.frames,
            timeout_seconds=args.timeout_seconds,
        )
        print(f"Completed bounded replay-prediction workload: {args.report}")
        return 0

    interaction = json.loads(args.interaction_report.read_text(encoding="utf-8"))

    if not interaction.get("ok", False):
        raise ValueError(f"interaction automation failed: {interaction.get('failure', 'unknown failure')}")

    report = analyze(
        parse_perf_csv(args.csv),
        interaction,
        top_count=args.top_count,
        correlation_radius=args.correlation_radius,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print_report(report)
    print(f"\nDiagnostic artifact: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
