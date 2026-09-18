"""Reproduce P/R/P through native keys and verify live camera/physics recovery."""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    scene = REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json"
    assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe", scene,
                  hidden=True, fixed_step=True, worker_threads=4, detail="full") == 0
    connection = SkarnessConnection(session)
    latest: dict[str, dict] = {}
    offset = 0

    def send(command: str, **arguments: object) -> dict:
        response = connection.wait(connection.send(command, arguments))
        assert response.get("status") == "applied", response
        return response

    def sample(label: str) -> dict:
        nonlocal offset
        send("run.step_frames", count=3)
        with (session / "runtime.skarness.ndjson").open(encoding="utf-8") as trace:
            trace.seek(offset)
            for line in trace:
                event = json.loads(line)
                if event.get("topic") in {"ui.presentation", "input.state", "camera.state", "scene.objects"}:
                    latest[event["topic"]] = event["payload"]
            offset = trace.tell()
        (session / f"{label}.json").write_text(json.dumps(latest, indent=2), encoding="utf-8")
        return latest["ui.presentation"]

    def key(value: str) -> dict:
        send("input.set_key", key=ord(value), down=True)
        send("run.step_frames", count=2)
        send("input.set_key", key=ord(value), down=False)
        return sample(f"key-{value}-{offset}")

    try:
        commands = send("capabilities.get")["commands"]
        assert all(command in commands for command in ("input.set_key", "input.pointer_position", "scene.object.resolve"))
        send("state.subscribe", topics=[], detail="full")
        # A real cursor is required to exercise the same inspection route as the player.
        send("input.pointer_position", enabled=True, x=850, y=500)
        send("run.step", count=30)
        before = sample("before")
        identity = latest["scene.objects"]
        assert before["cameraMode"] == 1
        assert key("P")["cameraMode"] == 2
        assert latest["input.state"]["predictionEnabled"]
        assert key("R")["cameraMode"] == before["cameraMode"]
        assert key("P")["cameraMode"] == before["cameraMode"]
        assert not latest["input.state"]["predictionEnabled"]
        assert not latest["input.state"]["playbackPaused"]
        assert not latest["input.state"]["scrubPaused"]
        body_before = send("scene.object.resolve", sceneObjectId=6)["result"]["objects"][0]
        send("run.resume")
        time.sleep(0.2)
        send("run.pause")
        after = sample("resumed")
        body_after = send("scene.object.resolve", sceneObjectId=6)["result"]["objects"][0]
        assert after["physicsCompletedSteps"] > before["physicsCompletedSteps"]
        assert body_before["sceneObjectId"] == body_after["sceneObjectId"] == 6
        assert body_after["position"] != body_before["position"]
        assert latest["scene.objects"]["scenePath"] == identity["scenePath"]
        assert latest["scene.objects"]["objectCount"] == identity["objectCount"]
        assert latest["scene.objects"]["manualResetCount"] == identity["manualResetCount"] + 1
        # Explicit user-selected Inspect remains a legitimate mode across reset.
        send("camera.set_pose", eye=[500, 100, 600], target=[500, 80, 500])
        assert sample("explicit-inspect")["cameraMode"] == 2
        assert key("R")["cameraMode"] == 2
        send("replay.set_playback_paused", paused=True)
        ui = sample("paused")
        x, y, _, _ = ui["transportBounds"]
        send("input.pointer_position", enabled=True, x=round(x+18), y=round(y+14))
        time.sleep(0.15)
        sample("icons")
        send("capture.screenshot", path=str((session / "transport-icons.png").resolve()))
        print("PASS: P/R/P restores Scene and live body motion; explicit Inspect survives reset")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/prediction-reset")
    run(parser.parse_args().session)
