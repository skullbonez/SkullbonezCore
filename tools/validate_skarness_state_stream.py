#!/usr/bin/env python3
"""Validate Skarness snapshots, deltas, and Physics trace correlation."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

from skarness import SkarnessConnection


REPO = Path(__file__).resolve().parents[1]
STATE_KINDS = {"snapshot", "append", "change", "evict", "reset", "state"}
REQUIRED_ENVELOPE = {
    "schemaVersion",
    "sequence",
    "runId",
    "runtimeTurn",
    "sceneGeneration",
    "simulationTick",
    "renderFrame",
    "replayFrame",
    "topic",
    "kind",
    "payload",
}
REQUIRED_TOPICS = {
    "session.state",
    "scene.objects",
    "selection.state",
    "input.state",
    "camera.state",
    "frame.clocks",
    "replay.timeline",
    "replay.prediction.controls",
    "replay.prediction.frames",
    "replay.prediction.evidence",
    "replay.prediction.topology",
    "replay.prediction.trajectories",
    "replay.cause",
    "replay.planning",
    "replay.visual_packet",
    "replay.render_submission",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def launch_session(session: Path, executable: Path, scene: Path) -> None:
    launched = subprocess.run(
        [
            sys.executable,
            str(REPO / "tools" / "skarness.py"),
            "launch",
            "--session",
            str(session),
            "--exe",
            str(executable),
            "--scene",
            str(scene),
            "--hidden",
        ],
        cwd=REPO,
        check=False,
        capture_output=True,
        text=True,
    )
    require(launched.returncode == 0, f"Skarness launch failed: {launched.stderr or launched.stdout}")


def complete_rows(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    deadline = time.monotonic() + 10.0
    while True:
        try:
            with path.open("rb") as source:
                for raw_line in source:
                    if not raw_line.endswith(b"\n"):
                        break
                    rows.append(json.loads(raw_line))
            break
        except (FileNotFoundError, PermissionError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.05)
    return rows


def validate(session: Path, executable: Path, scene: Path) -> dict[str, object]:
    launch_session(session, executable, scene)
    connection = SkarnessConnection(session)
    try:
        capabilities = connection.wait(connection.send("capabilities.get", request_id="state-capabilities"))
        topic_names = {str(row["name"]) for row in capabilities.get("topics", [])}
        require(REQUIRED_TOPICS <= topic_names, f"missing state topics: {sorted(REQUIRED_TOPICS - topic_names)}")
        require(capabilities.get("stateDetail") == ["summary", "normal", "full"], "detail catalog changed")

        invalid = connection.wait(
            connection.send(
                "state.subscribe",
                {"topics": ["not.a.topic"], "detail": "full"},
                request_id="state-invalid-topic",
            )
        )
        require(invalid.get("status") == "rejected", f"unknown state topic was accepted: {invalid}")

        summary = connection.wait(
            connection.send(
                "state.subscribe",
                {"topics": ["replay.timeline"], "detail": "summary"},
                request_id="state-summary-subscription",
            )
        )
        require(summary.get("status") == "applied", f"summary subscription failed: {summary}")
        recording = connection.wait(
            connection.send(
                "replay.set_recording_enabled",
                {"enabled": True},
                request_id="state-recording",
            )
        )
        require(recording.get("status") == "applied", f"recording enable failed: {recording}")
        filled = connection.wait(connection.send("run.step", {"count": 130}, request_id="state-fill-ring"))
        require(filled.get("status") == "applied", f"replay ring fill failed: {filled}")
        reset = connection.wait(connection.send("scene.reset", request_id="state-scene-reset"))
        require(reset.get("status") == "applied", f"scene reset failed: {reset}")

        selected = ["frame.clocks", "replay.prediction.frames", "replay.visual_packet"]
        subscribed = connection.wait(
            connection.send(
                "state.subscribe",
                {"topics": selected, "detail": "full"},
                request_id="state-full-subscription",
            )
        )
        require(subscribed.get("status") == "applied", f"full subscription failed: {subscribed}")
        stepped = connection.wait(connection.send("run.step", {"count": 3}, request_id="state-step"))
        require(stepped.get("status") == "applied", f"state stream step failed: {stepped}")
        stopped = connection.wait(connection.send("session.stop", request_id="state-stop"))
        require(stopped.get("status") == "applied", f"session.stop failed: {stopped}")
    finally:
        connection.close()

    runtime_rows = complete_rows(session / "runtime.skarness.ndjson")
    state_rows = [row for row in runtime_rows if row.get("kind") in STATE_KINDS]
    sequences = [int(row["sequence"]) for row in runtime_rows]
    require(sequences == sorted(sequences) and len(sequences) == len(set(sequences)), "trace sequence is not strict")
    for row in state_rows:
        missing = REQUIRED_ENVELOPE - row.keys()
        require(not missing, f"state row omitted envelope fields {sorted(missing)}: {row}")

    applied = next(
        row
        for row in runtime_rows
        if row.get("requestId") == "state-full-subscription" and row.get("status") == "applied"
    )
    for topic in selected:
        topic_rows = [row for row in state_rows if row.get("topic") == topic]
        require(topic_rows and topic_rows[0].get("kind") == "snapshot", f"{topic} did not begin with a snapshot")
        require(int(topic_rows[0]["sequence"]) < int(applied["sequence"]), f"{topic} snapshot was not durable first")
    require(
        any(row.get("kind") in {"append", "change"} and row.get("topic") in selected for row in state_rows),
        "subscription emitted no post-snapshot delta",
    )
    require(any(row.get("kind") == "evict" for row in state_rows), "replay ring wrap emitted no eviction")
    require(any(row.get("kind") == "reset" for row in state_rows), "scene generation emitted no topic reset")
    visual_rows = [row for row in state_rows if row.get("topic") == "replay.visual_packet"]
    require(
        any(
            "values" in row.get("payload", {}).get("combinedLines", {})
            for row in visual_rows
        ),
        "full detail omitted visual-packet float values",
    )

    physics_rows = complete_rows(session / "physics.physicsdiag.ndjson")
    correlations = [row for row in physics_rows if row.get("kind") == "correlation"]
    require(correlations, "Automation emitted no Physics correlation rows")
    runtime_keys = {
        (int(row["runtimeTurn"]), int(row["sceneGeneration"]), int(row["simulationTick"]))
        for row in state_rows
    }
    correlation_keys = {
        (int(row["runtimeTurn"]), int(row["sceneGeneration"]), int(row["simulationTick"]))
        for row in correlations
    }
    require(correlation_keys <= runtime_keys, "Physics correlation could not join exactly to the runtime trace")

    return {
        "ok": True,
        "topicCount": len(topic_names),
        "stateRows": len(state_rows),
        "physicsCorrelationRows": len(correlations),
        "evictionRows": sum(row.get("kind") == "evict" for row in state_rows),
        "resetRows": sum(row.get("kind") == "reset" for row in state_rows),
        "selectedTopics": selected,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput" / "skarness" / "state-stream")
    parser.add_argument("--exe", type=Path, default=REPO / "Automation" / "SKULLBONEZ_CORE.exe")
    parser.add_argument(
        "--scene",
        type=Path,
        default=REPO / "SkullbonezData" / "scenes" / "replay_prediction_simple.scene.json",
    )
    args = parser.parse_args()
    try:
        print(json.dumps(validate(args.session.resolve(), args.exe.resolve(), args.scene.resolve()), indent=2))
        return 0
    except (OSError, RuntimeError, KeyError, ValueError, json.JSONDecodeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
