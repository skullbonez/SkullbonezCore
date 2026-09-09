"""Drive the shared Replay controls through their native pointer routes.

Preparatory commands supply retained history and a target. Assertions read the
Replay owners after UI clicks; command acknowledgements alone are insufficient.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    scene = REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json"
    if launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe", scene, hidden=True) != 0:
        raise RuntimeError("Replay UI fixture could not launch")
    connection = SkarnessConnection(session)
    latest: dict[str, dict] = {}
    offset = 0

    def send(command: str, **arguments: object) -> dict:
        result = connection.wait(connection.send(command, arguments))
        assert result.get("status") == "applied", result
        return result

    def sample(label: str) -> dict:
        nonlocal offset
        send("run.step_frames", count=3)
        with (session / "runtime.skarness.ndjson").open(encoding="utf-8") as trace:
            trace.seek(offset)
            for line in trace:
                event = json.loads(line)
                if "topic" in event:
                    latest[event["topic"]] = event["payload"]
            offset = trace.tell()
        (session / f"{label}.json").write_text(json.dumps(latest, indent=2), encoding="utf-8")
        return latest["ui.presentation"]

    def click(x: float, y: float) -> None:
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0)

    def details(ui: dict) -> None:
        x, y, width, height = ui["replayDetailsBounds"]
        click(x + width / 2, y + height / 2)

    def row(ui: dict, index: int) -> None:
        x, y, width, height = ui["replayControlsBounds"]
        scroll = ui["replayScroll"] * max(0, 370 - height)
        click(x + width / 2, y + 19 + index * 32 - scroll)

    def seek(ui: dict, normalized: float) -> None:
        x, y, width, height = ui["transportBounds"]
        inset = min(72, width * 0.22)
        track_x = x + inset
        track_width = max(1, width - inset - 12)
        # Start away from the visible four-pixel stroke to verify the generous
        # hit target, then move the captured pointer to the requested cursor.
        send("input.pointer_drag", button="left", x=int(track_x + track_width * (normalized - 0.1)), y=int(y + 2),
             deltaX=int(track_width * 0.1), deltaY=0, moveClient=True)

    def cursor() -> float:
        return latest["replay.timeline"]["scrubber"]["position"]

    try:
        capabilities = send("capabilities.get")
        assert {"input.pointer_drag", "input.pointer_wheel", "input.pointer_position"} <= set(capabilities["commands"])
        send("state.subscribe", topics=["*"], detail="normal")
        send("run.resume")
        until = time.monotonic() + 0.4
        while time.monotonic() < until:
            connection.read_event()
        send("run.pause")
        ui = sample("history")
        assert latest["replay.timeline"]["solver"]["sampleCount"] >= 2
        scene_identity = dict(latest["scene.objects"])
        details(ui)
        ui = sample("canvas-details")
        assert ui["replayControlsBounds"][2] > 0
        send("capture.screenshot", path=str((session / "canvas-details.png").resolve()))
        x, y, controls_width, _ = ui["replayControlsBounds"]
        send("input.pointer_position", x=int(x + 100), y=int(y + 19), enabled=True)
        until = time.monotonic() + 0.6
        while time.monotonic() < until:
            connection.read_event()
        send("capture.screenshot", path=str((session / "disabled-branch-tooltip.png").resolve()))
        send("input.pointer_position", x=0, y=0, enabled=False)

        horizon_width = controls_width - 74
        horizon_x = x + 18
        horizon_y = y + 6 + 4 * 32 + 13
        send("input.pointer_drag", button="left", x=int(horizon_x + horizon_width * 0.3), y=int(horizon_y),
             deltaX=int(horizon_width * 0.3), deltaY=0, moveClient=True)
        sample("horizon-drag")
        assert abs(latest["replay.prediction.controls"]["horizonSeconds"] - (1 + 119 * 0.6)) < 1.0
        click(horizon_x + horizon_width * (19 / 119), horizon_y)
        sample("horizon-restored")
        assert abs(latest["replay.prediction.controls"]["horizonSeconds"] - 20) < 1.0

        for index, topic, key in (
            (1, "replay.prediction.controls", "highDetail"),
            (2, "input.state", "velocityEditEnabled"),
            (3, "input.state", "predictionEnabled"),
            (5, "input.state", "ragdollVisualsEnabled"),
        ):
            before = latest[topic][key]
            row(ui, index)
            sample(key + "-toggled")
            assert latest[topic][key] != before, (key, latest[topic])
            row(ui, index)
            sample(key + "-restored")
            assert latest[topic][key] == before, (key, latest[topic])

        send("replay.set_path_target", name="path_striker")
        sample("path-target")
        target_id = latest["selection.state"]["pathTargetId"]
        assert target_id != 0
        before = latest["input.state"]["pastPathVisible"]
        row(ui, 6)
        sample("past-path-toggled")
        assert latest["input.state"]["pastPathVisible"] != before
        assert latest["selection.state"]["pathTargetId"] == target_id
        row(ui, 6)
        sample("past-path-restored")
        assert latest["input.state"]["pastPathVisible"] == before

        seek(ui, 0.7)
        sample("canvas-seek")
        assert abs(cursor() - 0.7) < 0.01, latest["replay.timeline"]
        assert latest["input.state"]["scrubPaused"]
        held_cursor = cursor()
        width, _ = ui["window"]
        click(width - 110, 20)
        ui = sample("editor-retains-cursor")
        assert ui["layout"] == "Editor" and abs(cursor() - held_cursor) < 1e-6
        x, y, width, height = ui["editorReplayTabBounds"]
        click(x + width / 2, y + height / 2)
        ui = sample("editor-replay-tab")
        width, _ = ui["window"]
        click(width - 30, 20)
        ui = sample("tools-retains-cursor")
        assert ui["toolsVisible"] and abs(cursor() - held_cursor) < 1e-6
        seek(ui, 0.4)
        ui = sample("tools-seek")
        assert abs(cursor() - 0.4) < 0.01, latest["replay.timeline"]
        send("capture.screenshot", path=str((session / "editor-tools-replay.png").resolve()))
        held_cursor = cursor()
        details(ui)
        ui = sample("editor-folded")
        assert ui["replayControlsBounds"][2] == 0
        assert abs(cursor() - held_cursor) < 1e-6
        details(ui)
        ui = sample("editor-unfolded")
        assert ui["replayControlsBounds"][2] > 0
        x, y, _, _ = ui["replayControlsBounds"]
        send("input.pointer_wheel", x=int(x + 100), y=int(y + 30), wheelDelta=-120)
        ui = sample("replay-scrolled")
        assert ui["replayScroll"] > 0
        assert abs(cursor() - held_cursor) < 1e-6
        assert latest["scene.objects"] == scene_identity
        print("PASS: native Replay toggles, selection identity, generous scrub hit area, offset seeking, layout/drawer retention and folding")
    finally:
        try:
            send("input.pointer_position", x=0, y=0, enabled=False)
            send("session.stop")
        except (OSError, RuntimeError):
            connection.close()
            connection = SkarnessConnection(session)
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-replay-ui")
    run(parser.parse_args().session)
