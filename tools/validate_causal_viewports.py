"""Check Causal detail docking and sustained orthographic drags through native input."""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
from native_ui_comparison import current_wall_comparison
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    comparison_fixture = current_wall_comparison()
    assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe",
                  REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json",
                  hidden=True, worker_threads=4, layout_file=session / "layout.preferences",
                  allocation_guard="gameplay") == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0
    checks = []

    def send(command, **arguments):
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

    def click(bounds):
        x, y, w, h = bounds
        assert w > 0 and h > 0, bounds
        send("input.pointer_drag", button="left", x=round(x+w/2), y=round(y+h/2), deltaX=0, deltaY=0)
        return sample()

    def capture(label):
        send("capture.screenshot", path=str((session / (label + ".png")).resolve()))
        sample()
        (session / (label + ".json")).write_text(json.dumps(latest, indent=2))

    def pan_checks(label):
        ui = sample()
        if not ui["fourViews"]:
            ui = click(ui["headerFourViewsBounds"])
        for pane, normal in enumerate((1, 0, 2)):
            for raw, movement_frames in ((True, 1), (False, 1), (True, 30), (False, 30)):
                ui = sample()
                x, y, w, h = ui["editorPaneBounds"][pane]
                px, py = round(x+w*.75), round(y+h*.4)
                send("input.pointer_position", enabled=True, x=px, y=py)
                ui = sample()
                assert ui["activeEditorPane"] == pane
                before = ui["editorPaneEyes"]
                focus = ui["editorPaneFocus"]
                sx, sy = ui["projectionScale"]
                dx, dy = 600, 60
                expected_x = 2*dx/(sx*w)
                expected_y = 2*dy/(sy*h)
                send("input.pointer_drag", button="right", x=px, y=py, deltaX=dx, deltaY=dy,
                     moveClient=True, movementFrames=movement_frames, rawInput=raw, holdAfterMoveMilliseconds=40)
                ui = sample()
                delta = [a-b for a, b in zip(ui["editorPaneEyes"][pane], before[pane])]
                expected = ((-expected_x, 0, -expected_y), (0, expected_y, expected_x), (-expected_x, expected_y, 0))[pane]
                for actual, wanted in zip(delta, expected):
                    assert math.isclose(actual, wanted, rel_tol=.002, abs_tol=.005), (label, pane, raw, delta, expected)
                assert delta[normal] == 0, (pane, delta)
                for axis in range(3):
                    assert math.isclose(ui["editorPaneFocus"][pane][axis]-focus[pane][axis], delta[axis], abs_tol=.005)
                for other in range(4):
                    if other != pane:
                        assert ui["editorPaneEyes"][other] == before[other], (pane, other)
                checks.append(f"{label}-plane-{pane}-{'raw' if raw else 'client'}-{movement_frames}-frame-drag")
        capture(label + "-panned")

    def cause():
        return next(value for value in latest.values() if "drawerOpen" in value and "rowCount" in value)

    def contained(label):
        ui = sample()
        detail = cause()["detailBounds"]
        assert cause()["drawerOpen"] and detail[2] > 0, cause()
        assert ui["editorCanvasBounds"][0]+ui["editorCanvasBounds"][2] <= detail[0]+.01, (label, ui["editorCanvasBounds"], detail)
        for pane in ui["editorPaneBounds"]:
            assert pane[0]+pane[2] <= detail[0]+.01, (label, pane, detail)
        assert detail[0]+detail[2] <= ui["causeControlsBounds"][0]+.01
        checks.append(label + "-detail-does-not-overlap")
        capture(label)
        return ui

    try:
        capabilities = send("capabilities.get")
        assert {"input.pointer_drag", "replay.set_cause_inspector_open", "comparison.load"} <= set(capabilities["commands"])
        send("state.subscribe", topics=[], detail="normal")
        send("run.pause")
        send("window.resize", width=1680, height=1000)
        ui = sample()
        if ui["layout"] != "Editor":
            ui = click(ui["headerLayoutBounds"])
        pan_checks("scene")
        send("replay.set_prediction_detail", highDetail=True)
        send("replay.set_prediction_horizon", seconds=7.5)
        send("prediction.select_target", name="path_striker")
        send("replay.set_prediction_enabled", enabled=True)
        send("run.until", condition="prediction.complete", maxFrames=3000)
        ui = sample()
        if ui["causeControlsBounds"][2] == 0:
            ui = click(ui["rightFoldBounds"])
        send("replay.select_cause_row", row=2)
        send("run.until", condition="camera.inspection_settled", maxFrames=1000)
        send("replay.set_cause_inspector_open", open=True)
        ui = contained("editor-detail")
        pan_checks("causal")
        ui = contained("causal-after-panning")
        selected = dict(latest["selection.state"])
        target = latest["replay.prediction.controls"]["sourceTargetId"]
        assert target == latest["selection.state"]["pathTargetId"]
        for width, height in ((640, 360), (1280, 720), (1680, 1000)):
            send("window.resize", width=width, height=height)
            ui = contained(f"editor-{width}")
            assert latest["selection.state"] == selected
            open_width = ui["editorCanvasBounds"][2]
            send("replay.set_cause_inspector_open", open=False)
            ui = sample()
            assert ui["editorCanvasBounds"][2] > open_width
            send("replay.set_cause_inspector_open", open=True)
            contained(f"reopened-{width}")
        ui = click(ui["replayDetailsBounds"])
        contained("tools-detail")
        send("replay.set_prediction_enabled", enabled=False)
        send("comparison.load", path=str(comparison_fixture))
        sample()
        pan_checks("lab")
        (session / "result.json").write_text(json.dumps({"ok": True, "checks": checks}, indent=2))
        print(json.dumps({"ok": True, "checks": checks}))
    finally:
        send("session.stop")
        connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    run(parser.parse_args().session.resolve())
