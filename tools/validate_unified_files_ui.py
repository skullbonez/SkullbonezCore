"""Verify native comparison file/finding buttons using queued picker responses."""
from __future__ import annotations

import argparse
import copy
import json
import time
from pathlib import Path
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    session = session.resolve()
    scene = REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json"
    bundle = REPO / "SkullbonezData/solver-lab/wall-only/comparison.json"
    assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe", scene, hidden=True) == 0
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
        latest["comparison"] = send("comparison.state")["result"]["comparison"]
        (session / f"{label}.json").write_text(json.dumps(latest, indent=2), encoding="utf-8")
        return latest["ui.presentation"]

    def click(x: float, y: float) -> None:
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0,
             holdMilliseconds=70)

    def file_button(ui: dict, purpose: str, row: int, column: int, path: Path | None) -> dict:
        before = ui["fileDialogResponsesConsumed"]
        send("input.file_dialog_response", purpose=purpose, accepted=path is not None,
             **({"path": str(path)} if path is not None else {}))
        queued = sample("picker-queued")
        assert queued["fileDialogResponsesConsumed"] == before
        x, y, width, _ = ui["replayControlsBounds"]
        assert width > 0, ui
        half = (width - 26) / 2
        click(x + 10 + column * (half + 6) + half / 2, y + 10 + 56 + row * 34 + 14)
        ui = sample("picker-consumed")
        assert ui["fileDialogResponsesConsumed"] == before + 1, ui
        deadline = time.monotonic() + 30
        while latest["comparison"]["loading"] and time.monotonic() < deadline:
            ui = sample("loading")
        assert not latest["comparison"]["loading"], latest["comparison"]
        return ui

    retained_keys = ("active", "bundle", "tick", "selected", "selectedEvent", "mode", "stackedViews",
                     "showA", "xray", "followA", "selectedOnly", "differencesOnly", "speed",
                     "positionThreshold", "loopEnabled", "loopFirst", "loopLast", "cameraEye", "cameraView")

    def retained(before: dict) -> None:
        for key in retained_keys:
            assert latest["comparison"][key] == before[key], (key, latest["comparison"], before)

    try:
        capabilities = send("capabilities.get")
        assert "input.file_dialog_response" in capabilities["commands"]
        send("state.subscribe", topics=[], detail="normal")
        ui = sample("initial")
        width = ui["window"][0]
        click(width - 400, 20)
        ui = sample("empty-lab")
        assert ui["workspace"] == "Solver Lab"
        x, y, w, h = ui["replayDetailsBounds"]
        click(x + w / 2, y + h / 2)
        ui = sample("canvas-details")
        for layout in ("Canvas", "Editor"):
            if ui["layout"] != layout:
                click(width - 110, 20)
                ui = sample(layout)
            ui = file_button(ui, "comparison.open", 0, 0, bundle)
            assert latest["comparison"]["active"] and latest["comparison"]["bundle"] == str(bundle)
            send("comparison.seek", tick=123)
            send("comparison.select", sceneObjectId=1)
            send("comparison.focus")
            send("comparison.mode", mode="heatmap")
            send("comparison.setting", name="stackedViews", value=1)
            send("comparison.setting", name="speed", value=0.5)
            send("comparison.loop", first=100, last=150, enabled=True)
            ui = sample(layout + "-prepared-finding")
            before = copy.deepcopy(latest["comparison"])
            finding = session / f"{layout}-finding.json"
            ui = file_button(ui, "comparison.save", 1, 0, finding)
            saved = json.loads(finding.read_text(encoding="utf-8"))
            assert saved["format"] == "skullbonez.physics-finding"
            assert saved["tick"] == 123 and saved["selected"] == 1
            assert saved["camera"]["eye"] == before["cameraEye"]
            assert saved["settings"]["display"] == 3 and saved["loop"] == [100, 150, 1]
            retained(before)
            for purpose, row, column in (("comparison.open", 0, 0), ("comparison.open", 0, 1),
                                         ("comparison.save", 1, 0)):
                ui = file_button(ui, purpose, row, column, None)
                retained(before)
            send("comparison.seek", tick=10)
            send("comparison.mode", mode="overlay")
            send("comparison.setting", name="stackedViews", value=0)
            send("comparison.select", sceneObjectId=0)
            ui = sample(layout + "-changed")
            ui = file_button(ui, "comparison.open", 0, 1, finding)
            retained(before)
            sample(layout + "-restored")
            send("capture.screenshot", path=str(session / f"{layout}-restored.png"))
            invalid = session / f"{layout}-invalid-finding.json"
            invalid.write_text('{"format":"unknown"}', encoding="utf-8")
            for path, column in ((session / "missing-comparison.json", 0), (invalid, 1)):
                ui = file_button(ui, "comparison.open", 0, column, path)
                assert latest["comparison"]["loadError"], latest["comparison"]
                assert ui["workspace"] == "Solver Lab" and ui["layout"] == layout
                assert not latest["comparison"]["active"]
                send("capture.screenshot", path=str(session / f"{layout}-error-{column}.png"))
                vx, vy, vw, vh = ui["viewport"]
                panel_width = min(560, max(100, vw - 32))
                click(vx + (vw + panel_width) / 2 - 45, vy + (vh - 150) / 2 + 120)
                ui = sample(layout + f"-error-dismissed-{column}")
                assert not latest["comparison"]["loadError"]
                assert not latest["comparison"]["active"]
                ui = file_button(ui, "comparison.open", 0, 1, finding)
                retained(before)
        print("PASS: native Open, Save finding, Load finding, three picker cancellations and two load errors in both layouts")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-files-ui")
    run(parser.parse_args().session)
