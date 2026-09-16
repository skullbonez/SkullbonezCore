"""Verify demand allocation and retirement for a dense, long prediction."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from skarness import SkarnessConnection, launch
from validate_memory_ui import process_memory
from validate_ui_themes import wait_for_exit

REPO = Path(__file__).resolve().parents[1]
MIB = 1024 * 1024


def run(directory: Path) -> None:
    directory = directory.resolve()
    assert launch(directory, REPO / "Automation/SKULLBONEZ_CORE.exe",
                  REPO / "SkullbonezData/scenes/box_pile_throw_300.scene.json",
                  hidden=True, worker_threads=4, allocation_guard="gameplay") == 0
    connection = SkarnessConnection(directory)
    process_id = json.loads((directory / "session.json").read_text())["processId"]
    latest = {}
    offset = 0
    measurements = {}

    def send(command: str, **arguments):
        result = connection.wait(connection.send(command, arguments))
        assert result.get("status") == "applied", result
        return result

    def sample(label: str):
        nonlocal offset
        send("run.step_frames", count=3)
        with (directory / "runtime.skarness.ndjson").open("rb") as stream:
            stream.seek(offset)
            for line in stream:
                if not line.endswith(b"\n"):
                    break
                offset += len(line)
                row = json.loads(line)
                if "topic" in row:
                    latest[row["topic"]] = row["payload"]
        measurements[label] = process_memory(process_id)
        return latest.get("replay.state", {})

    try:
        caps = send("capabilities.get")
        required = {"prediction.select_target", "replay.set_prediction_horizon",
                    "replay.set_prediction_enabled", "replay.set_prediction_detail"}
        assert required <= set(caps["commands"])
        send("state.subscribe", topics=[], detail="normal")
        sample("before")
        assert measurements["before"]["privateCommit"] < 1024 * MIB
        send("prediction.select_target", name="throw_box_000")
        send("replay.set_prediction_detail", highDetail=True)
        send("replay.set_prediction_horizon", seconds=120)
        send("replay.set_prediction_enabled", enabled=True)
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            state = sample("long")
            if state.get("predictionComplete") and state.get("publishedPredictionFrames") == 14401:
                break
        else:
            raise AssertionError(state)
        assert state["pathTargetId"] != 0
        assert state["pathTargetId"] == state["publishedPredictionTargetId"] == state["submittedPredictionTargetId"]
        assert state["trajectorySubmitted"] and state["selectedFutureRootPointCount"] > 0
        assert latest["replay.prediction.evidence"]["publishedFrameCount"] == 14401
        (directory / "long.json").write_text(json.dumps(latest, indent=2))
        send("capture.screenshot", path=str(directory / "long.png"))
        generation = state["predictionGeneration"]
        send("replay.set_prediction_horizon", seconds=20)
        state = sample("shortened")
        assert state["predictionComplete"] and state["publishedPredictionFrames"] == 2401
        assert state["predictionGeneration"] == generation
        send("replay.set_prediction_enabled", enabled=False)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            state = sample("disabled")
        assert not state["predictionEnabled"] and state["publishedPredictionFrames"] == 0
        assert state["trajectoryRecordCount"] == 0
        assert measurements["disabled"]["privateCommit"] < measurements["long"]["privateCommit"] - 500 * MIB
        assert measurements["disabled"]["privateCommit"] < measurements["before"]["privateCommit"] + 128 * MIB
        (directory / "disabled.json").write_text(json.dumps(latest, indent=2))
        send("replay.set_prediction_horizon", seconds=2)
        send("prediction.select_target", name="throw_box_001")
        send("replay.set_prediction_enabled", enabled=True)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            state = sample("small-restart")
            if state.get("predictionComplete") and state.get("publishedPredictionFrames") == 241:
                break
        else:
            raise AssertionError(state)
        assert state["pathTargetId"] == state["publishedPredictionTargetId"] == state["submittedPredictionTargetId"]
        assert state["pathTargetId"] != 1
        assert measurements["small-restart"]["privateCommit"] < measurements["long"]["privateCommit"] / 2

        print(json.dumps(measurements, indent=2))
    finally:
        (directory / "measurements.json").write_text(json.dumps(measurements, indent=2))
        try:
            send("session.stop")
        finally:
            connection.close()
            wait_for_exit(directory)

    shutdown = (directory / "process.stdout.log").read_text(errors="replace")
    assert "gameplay_violations=0" in shutdown and "policy_violations=0" in shutdown
    (directory / "result.json").write_text(json.dumps({"passed": True, "measurements": measurements}, indent=2))
    print("PASS: 120-second prediction, full evidence, identity, memory retirement and strict allocation policy")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--session", type=Path, required=True)
    run(parser.parse_args().session)
