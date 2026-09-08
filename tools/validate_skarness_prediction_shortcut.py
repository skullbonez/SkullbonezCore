"""Prove physical P presses pause/arm prediction and clear/resume live play."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path, executable: Path) -> None:
    scene = REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json"
    if launch(session, executable, scene, hidden=True) != 0:
        raise RuntimeError("prediction shortcut fixture could not launch")
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0
    tick = 0

    def send(command: str, **arguments: object) -> dict:
        result = connection.wait(connection.send(command, arguments))
        assert result.get("status") == "applied", result
        return result

    def sample(label: str, frames: int = 3) -> int:
        nonlocal offset, tick
        send("run.step_frames", count=frames)
        with (session / "runtime.skarness.ndjson").open(encoding="utf-8") as trace:
            trace.seek(offset)
            for line in trace:
                event = json.loads(line)
                if "topic" in event:
                    latest[event["topic"]] = event["payload"]
            offset = trace.tell()
        # The scene clock advances during inspection; captured solver frames
        # advance only when the live Physics pipeline actually runs.
        tick = latest["replay.timeline"]["solver"]["nextFrame"]
        (session / f"{label}.json").write_text(
            json.dumps({"solverNextFrame": tick, "topics": latest}, indent=2), encoding="utf-8")
        return tick

    def press(label: str) -> None:
        send("input.set_prediction_key", down=True)
        sample(label)
        send("input.set_prediction_key", down=False)
        sample(label + "-released")

    def run_live(label: str) -> int:
        # Release the harness pause so this tests the P-owned pause, rather than
        # the independent render-only stepping control masking a live tick.
        send("run.resume")
        time.sleep(0.2)
        send("run.pause")
        return sample(label)

    def assert_cleared() -> None:
        replay = latest["replay.state"]
        assert not replay["predictionEnabled"] and not replay["predictionBuilding"]
        assert replay["pathTargetId"] == 0 and replay["selectedCauseRow"] == -1
        assert replay["causeTreeRowCount"] == 0 and not replay["inspectionCameraActive"]
        assert replay["drawnCollisionWireframeCount"] == replay["drawnEndingWireframeCount"] == 0
        assert replay["submittedSegmentCount"] == 0
        assert not latest["replay.timeline"]["scrubber"]["liveAdvanceHeld"]

    try:
        capabilities = send("capabilities.get")
        assert any(row["name"] == "input.set_prediction_key" for row in capabilities["catalog"])
        send("state.subscribe", topics=[], detail="normal")
        send("replay.set_recording_enabled", enabled=False)
        send("replay.set_prediction_detail", highDetail=True)
        press("armed-empty")
        assert latest["replay.state"]["predictionEnabled"]
        assert latest["input.state"]["captureEnabled"]
        assert latest["replay.state"]["pathTargetId"] == 0
        held_tick = sample("paused")
        assert run_live("still-paused") == held_tick, "P did not hold live physics"
        # Supply native pointer samples so the toolbar's fade can advance in
        # this synthetic keyboard session, just as it does with a real cursor.
        for _ in range(12):
            send("input.pointer_wheel", x=1000, y=800, wheelDelta=0)
        send("capture.screenshot", path=str((session / "armed-empty.png").resolve()))
        press("resumed-empty")
        assert_cleared()
        running_tick = tick
        assert run_live("running-empty") > running_tick, "second P did not resume physics"

        send("input.set_prediction_key", down=True)
        sample("armed-held-key")
        assert latest["replay.state"]["predictionEnabled"]
        held_tick = tick
        assert run_live("held-key-no-repeat") == held_tick
        assert latest["replay.state"]["predictionEnabled"], "held P toggled repeatedly"
        send("input.set_prediction_key", down=False)
        sample("released-key")
        send("replay.set_prediction_horizon", seconds=7.5)
        send("prediction.select_target", name="path_striker")
        send("run.until", condition="prediction.complete", maxFrames=3000)
        sample("prediction-ready")
        assert latest["replay.cause"]["rowCount"] > 2
        send("replay.select_cause_row", row=2)
        send("run.until", condition="camera.inspection_settled", maxFrames=1000)
        sample("selected")
        assert latest["replay.state"]["pathTargetId"] == 6
        assert latest["replay.state"]["submittedPredictionTargetId"] == 6
        assert latest["replay.state"]["selectedCauseRow"] == 2
        assert latest["replay.state"]["inspectionCameraActive"]
        press("resumed-selection")
        assert_cleared()
        running_tick = tick
        assert run_live("running-after-selection") > running_tick
        assert_cleared()  # A late worker publication must not restore the old overlay.
        send("capture.screenshot", path=str((session / "cleared-running.png").resolve()))
        print("PASS: P pauses and arms prediction; held P does not repeat; second P clears selection, paths and inspection and advances physics")
    finally:
        try:
            send("input.set_prediction_key", down=False)
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/prediction-shortcut")
    parser.add_argument("--exe", type=Path, default=REPO / "Automation/SKULLBONEZ_CORE.exe")
    args = parser.parse_args()
    run(args.session, args.exe)
