"""Verify native target picking and rendered predictions after dense horizon edits."""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    session = session.resolve()
    assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe",
                  REPO / "SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json",
                  hidden=True, worker_threads=4, allocation_guard="gameplay") == 0
    connection = SkarnessConnection(session)
    latest: dict = {}
    offset = 0
    checks = []

    def send(command: str, **arguments):
        result = connection.wait(connection.send(command, arguments))
        assert result.get("status") == "applied", (command, result)
        return result

    def sample():
        nonlocal offset
        send("run.step_frames", count=4)
        with (session / "runtime.skarness.ndjson").open("rb") as stream:
            stream.seek(offset)
            for line in stream:
                if not line.endswith(b"\n"):
                    break
                offset += len(line)
                event = json.loads(line)
                if "topic" in event:
                    latest[event["topic"]] = event["payload"]
        return latest["ui.presentation"]

    def save(label):
        (session / (label + ".json")).write_text(json.dumps(latest, indent=2), encoding="utf-8")

    def click(x, y):
        send("input.pointer_position", enabled=True, x=round(x), y=round(y))
        sample()
        send("input.pointer_drag", button="left", x=round(x), y=round(y), deltaX=0, deltaY=0)
        return sample()

    def center(bounds):
        x, y, width, height = bounds
        assert width > 0 and height > 0, bounds
        return click(x + width / 2, y + height / 2)

    def pick(name):
        body = send("scene.object.resolve", name=name)["result"]["objects"][0]
        ui = sample()
        camera = latest["camera.state"]

        def subtract(a, b):
            return [x - y for x, y in zip(a, b)]

        def dot(a, b):
            return sum(x * y for x, y in zip(a, b))

        def normalize(a):
            length = dot(a, a) ** 0.5
            return [x / length for x in a]

        def cross(a, b):
            return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]

        forward = normalize(subtract(camera["renderView"], camera["renderEye"]))
        right = normalize(cross(forward, camera["renderUp"]))
        up = cross(right, forward)
        delta = subtract(body["position"], camera["renderEye"])
        depth = dot(delta, forward)
        assert depth > 0
        x, y, width, height = ui["viewport"]
        sx, sy = ui["projectionScale"]
        px = x + width * 0.5 * (1 + dot(delta, right) * sx / depth)
        py = y + height * 0.5 * (1 - dot(delta, up) * sy / depth)
        assert x < px < x + width and y < py < y + height, (name, px, py)
        ui = click(px, py)
        target = body["sceneObjectId"]
        assert latest["selection.state"]["pathTargetId"] == target, (name, latest["selection.state"])
        assert ui["pointerGesture"][0] == 0 and not ui["pointerWorldSuppressed"]
        return target

    def ready(target, seconds, label):
        # prediction.complete can briefly describe the superseded generation.
        # Require the requested target, horizon and production submission together.
        deadline = time.monotonic() + 180
        expected_frames = round(seconds * 120) + 1
        while time.monotonic() < deadline:
            sample()
            replay = latest["replay.state"]
            controls = latest["replay.prediction.controls"]
            if (replay["predictionComplete"] and not replay["predictionBuilding"]
                    and not controls["dirty"] and not controls["restartPending"]
                    and replay["pathTargetId"] == replay["publishedPredictionTargetId"] == replay["submittedPredictionTargetId"] == target
                    and replay["publishedPredictionFrames"] == expected_frames
                    and replay["submittedGeometryBytes"] > 0 and replay["submittedFutureTreeReady"]):
                assert replay["predictionSourceFrame"] == replay["submittedPredictionSourceFrame"]
                assert replay["publishedPredictionTopologyVersion"] == replay["submittedPredictionTopologyVersion"]
                save(label)
                send("capture.screenshot", path=str(session / (label + ".png")))
                checks.append({"case": label, "target": target, "seconds": seconds, "frames": expected_frames})
                print("PASS", checks[-1], flush=True)
                return
        save(label + "-failed")
        send("capture.screenshot", path=str(session / (label + "-failed.png")))
        raise AssertionError((label, latest["replay.prediction.controls"], latest["replay.state"]))

    def horizon(seconds, label):
        ui = sample()
        x, y, width, height = ui["replayControlsBounds"]
        assert width > 74 and height > 160
        # The native horizon row spans 1..120 seconds in integral steps.
        click(x + 18 + (width - 74) * (seconds - 1) / 119, y + 147)
        actual = latest["replay.prediction.controls"]["horizonSeconds"]
        assert abs(actual - seconds) <= 1, (actual, seconds)
        save(label)
        return actual

    try:
        commands = set(send("capabilities.get")["commands"])
        assert {"input.pointer_position", "input.pointer_drag", "replay.set_prediction_horizon", "capture.screenshot"} <= commands
        send("state.subscribe", topics=[], detail="normal")
        send("input.set_focus", focused=True)
        send("replay.set_prediction_detail", highDetail=True)
        send("replay.set_prediction_horizon", seconds=120)
        send("prediction.select_target", name="prediction_striker_ball")
        send("replay.set_prediction_enabled", enabled=True)
        ui = sample()
        if ui["layout"] != "Editor":
            ui = center(ui["headerLayoutBounds"])
        if ui["replayControlsBounds"][2] == 0:
            ui = center(ui["editorReplayTabBounds"])
        assert latest["replay.prediction.controls"]["building"], "Active-horizon fixture finished before the edit"
        running_generation = latest["replay.state"]["predictionGeneration"]
        short = horizon(1, "trim-active-worker")
        assert latest["replay.visual_packet"]["header"]["revealFrame"] <= round(short * 120)
        ready(1, short, "active-worker-trim")
        assert latest["replay.state"]["predictionGeneration"] == running_generation
        initial = horizon(20, "extend-stopped-worker")
        ready(1, initial, "initial")
        assert latest["replay.state"]["predictionGeneration"] == running_generation
        prefix = list(latest["replay.prediction.frames"]["frames"])
        evidence = list(latest["replay.prediction.evidence"]["frames"])
        generation = latest["replay.state"]["predictionGeneration"]
        seconds = horizon(61, "long-horizon")
        ready(1, seconds, "same-target-extension")
        assert latest["replay.state"]["predictionGeneration"] == generation, "Horizon extension restarted Physics"
        assert latest["replay.prediction.frames"]["frames"][:len(prefix)] == prefix
        assert latest["replay.prediction.evidence"]["frames"][:len(evidence)] == evidence
        cached_frames = latest["replay.state"]["selectedFutureRootPointCount"]
        short = horizon(5, "immediate-trim")
        assert latest["replay.visual_packet"]["header"]["revealFrame"] <= round(short * 120)
        assert latest["replay.state"]["predictionGeneration"] == generation
        ready(1, short, "same-target-trim")
        seconds = horizon(61, "cached-extension")
        ready(1, seconds, "same-target-cached-extension")
        assert latest["replay.state"]["predictionGeneration"] == generation
        assert latest["replay.state"]["selectedFutureRootPointCount"] == cached_frames
        maximum = horizon(120, "maximum-horizon")
        ready(1, maximum, "same-target-maximum-extension")
        assert latest["replay.state"]["predictionGeneration"] == generation
        assert latest["replay.prediction.frames"]["frames"][:len(prefix)] == prefix
        assert latest["replay.prediction.evidence"]["frames"][:len(evidence)] == evidence
        seconds = horizon(61, "maximum-trim")
        ready(1, seconds, "same-target-maximum-trim")
        target = pick("prediction_wall_brick_r03_c10")
        ready(target, seconds, "long-horizon-new-target")
        # A second selection must succeed after the long build has filled its
        # optional evidence bank; shortening must also recover in this session.
        target = pick("prediction_wall_brick_r04_c10")
        ready(target, seconds, "long-horizon-second-target")
        seconds = horizon(5, "short-horizon")
        target = pick("prediction_wall_brick_r03_c10")
        ready(target, seconds, "short-horizon-new-target")
        (session / "result.json").write_text(json.dumps({"passed": True, "checks": checks}, indent=2))
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    run(parser.parse_args().session)
