"""Verify native Causes folding, evidence selection, colours and input routing."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from PIL import Image
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    scene = REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json"
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
        (session / f"{label}.json").write_text(json.dumps(latest, indent=2), encoding="utf-8")
        return latest["ui.presentation"]

    def click(x: float, y: float) -> None:
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0)

    def key(code: int) -> None:
        send("input.set_key", key=code, down=True)
        send("run.step_frames", count=1)
        send("input.set_key", key=code, down=False)
        send("run.step_frames", count=1)

    def middle(bounds: list[float]) -> None:
        x, y, width, height = bounds
        assert width > 0 and height > 0, bounds
        click(x + width / 2, y + height / 2)

    def capture(label: str) -> None:
        send("capture.screenshot", path=str((session / f"{label}.png").resolve()))
        with Image.open(session / f"{label}.png") as img: img.save(session / f"{label}-view.png")

    try:
        capabilities = send("capabilities.get")
        assert {"input.pointer_drag", "prediction.select_target", "replay.set_prediction_detail"} <= set(capabilities["commands"])
        send("state.subscribe", topics=[], detail="normal")
        send("replay.set_prediction_detail", highDetail=True)
        send("replay.set_prediction_horizon", seconds=7.5)
        send("prediction.select_target", name="path_striker")
        send("replay.set_prediction_enabled", enabled=True)
        send("run.until", condition="prediction.complete", maxFrames=3000)
        ui = sample("prediction-ready")
        target = latest["selection.state"]["pathTargetId"]
        assert target != 0
        assert latest["replay.prediction.controls"]["sourceTargetId"] == target
        cause_topic = next(key for key in latest if key.startswith("replay.") and "rowCount" in latest[key])
        assert latest[cause_topic]["rowCount"] > 2, latest[cause_topic]
        middle(ui["replayDetailsBounds"])
        ui = sample("details")
        assert latest["selection.state"]["pathTargetId"] == target
        middle(ui["detailsCausesTabBounds"])
        ui = sample("canvas-causes")
        bounds = ui["causeControlsBounds"]
        assert bounds[2] > 0 and ui["replayControlsBounds"][2] == 0
        assert latest[cause_topic]["window"] == [int(value) for value in bounds]
        capture("canvas-causes")
        x, y, width, height = bounds
        # Outline controls keep their existing meanings and data colours.
        before = latest[cause_topic]["blueOutlinesVisible"]
        click(x + 90, y + height - 66)
        sample("outline-toggled")
        assert latest[cause_topic]["blueOutlinesVisible"] != before
        click(x + 90, y + height - 66)
        sample("outline-restored")
        assert latest[cause_topic]["blueOutlinesVisible"] == before
        click(x + width * 0.85, y + 88)
        sample("contacts-filter")
        assert latest[cause_topic]["filter"] == 2
        click(x + 45, y + 88)
        sample("all-filter")
        assert latest[cause_topic]["filter"] == 0
        # Filter typing must not execute world shortcuts (notably P, F, J and I).
        selection_before = dict(latest["selection.state"])
        camera_before = dict(latest["camera.state"])
        generation_before = latest["replay.prediction.controls"]["generation"]
        click(x + 80, y + 57)
        sample("cause-filter-focused")
        assert latest[cause_topic]["filterFocused"], latest[cause_topic]
        for char in "PFJI":
            key(ord(char))
        sample("typed-cause-filter")
        assert latest[cause_topic]["filterText"].lower() == "pfji", latest[cause_topic]
        assert latest["selection.state"] == selection_before
        assert latest["camera.state"]["selectedCameraHash"] == camera_before["selectedCameraHash"]
        assert latest["replay.prediction.controls"]["generation"] == generation_before
        key(0x08)
        sample("backspace-cause-filter")
        assert latest[cause_topic]["filterText"].lower() == "pfj"
        key(0x1B)
        sample("clear-cause-filter")
        assert latest[cause_topic]["filterText"] == ""
        assert not latest["ui.presentation"]["toolsVisible"]
        key(0x0D)
        sample("unfocus-cause-filter")
        assert not latest[cause_topic]["filterFocused"]
        # Select a visible retained cause row through the hierarchy hit route.
        click(x + 150, y + 108 + 2 * 38 + 12)
        send("run.until", condition="camera.inspection_settled", maxFrames=1000)
        ui = sample("selected-evidence")
        selected = latest["selection.state"]["selectedCauseRow"]
        assert selected >= 0, latest["selection.state"]
        selected_row = latest[cause_topic]["rows"][selected]
        assert latest["selection.state"]["selectedCausePrimaryId"] == selected_row["id"]
        assert latest["selection.state"]["selectedCauseCounterpartId"] == selected_row["counterpartId"]
        identity = dict(latest["selection.state"])
        if not latest[cause_topic]["drawerOpen"]:
            click(x + width - 48, y + 19)
            sample("evidence-opened")
        assert latest[cause_topic]["drawerOpen"]
        assert latest[cause_topic]["window"][3] == 38
        capture("canvas-evidence")
        click(x + width - 48, y + 19)
        sample("evidence-folded")
        assert not latest[cause_topic]["drawerOpen"]
        assert latest["selection.state"] == identity
        window_width, _ = ui["window"]
        click(window_width - 110, 20)
        ui = sample("editor-causes")
        assert ui["layout"] == "Editor"
        assert latest["selection.state"] == identity
        capture("editor-causes")
        click(window_width - 30, 20)
        ui = sample("tools-causes")
        assert ui["toolsVisible"]
        assert latest[cause_topic]["window"] == [int(value) for value in ui["causeControlsBounds"]]
        x, y, width, _ = ui["causeControlsBounds"]
        click(x + width - 48, y + 19)
        sample("tools-evidence")
        assert latest[cause_topic]["drawerOpen"]
        assert latest["selection.state"] == identity
        capture("tools-evidence")
        # Every tab remains reachable in the smaller dock, with local scrolling.
        for index, label in ((1, "raw"), (2, "iterations"), (0, "summary")):
            click(x + 12 + (width - 24) * (index + 0.5) / 3, y + 38 + 88 + 15)
            sample("tools-" + label)
            assert latest[cause_topic]["activeTab"] == index
            assert latest["selection.state"] == identity
            capture("tools-" + label)
        send("input.pointer_wheel", x=int(x + 100), y=int(y + 230), wheelDelta=-120)
        sample("evidence-scrolled")
        assert latest[cause_topic]["summaryScrollOffset"] > 0
        send("input.pointer_position", x=int(x + width - 48), y=int(y + 19), enabled=True)
        send("state.subscribe", topics=["ui.presentation"], detail="normal")
        until = time.monotonic() + 0.6
        while time.monotonic() < until:
            connection.read_event()
        capture("evidence-tooltip")
        send("state.subscribe", topics=[], detail="normal")
        send("input.pointer_position", x=0, y=0, enabled=False)
        if ui["toolsVisible"]:
            click(ui["window"][0]-30,20);ui=sample("short-tools-closed")
        for layout_name in ("Editor", "Canvas"):
            send("window.resize", width=640, height=360)
            ui=sample(layout_name+"-short-window")
            if ui["layout"] != layout_name:
                click(ui["window"][0]-110,20);ui=sample(layout_name+"-short-layout")
            if ui["causeControlsBounds"][2] == 0:
                middle(ui["replayDetailsBounds"]);ui=sample("short-details")
                middle(ui["detailsCausesTabBounds"]);ui=sample("short-causes")
            x,y,width,height=ui["causeControlsBounds"]
            # The pane edge owns shell scrolling; content owns record scrolling.
            send("input.pointer_wheel",x=int(x+2),y=int(y+height/2),wheelDelta=12000)
            ui=sample("short-shell-top")
            if not latest[cause_topic]["drawerOpen"]:
                click(x+width-48,y+19);ui=sample("short-drawer-open")
            for index,label,field in ((1,"raw","rawRecordFirstRow"),(2,"iterations","iterationsFirstRow"),(0,"summary","summaryScrollOffset")):
                send("input.pointer_wheel",x=int(x+2),y=int(y+height/2),wheelDelta=12000)
                ui=sample("short-tab-top")
                # Raise the tab strip if it is below the physical pane.
                if height<160:
                    send("input.pointer_wheel",x=int(x+2),y=int(y+height/2),wheelDelta=-120)
                    ui=sample("short-tabs-visible")
                outer=latest[cause_topic]["shellScroll"]
                click(x+12+(width-24)*(index+.5)/3,y+141-outer)
                ui=sample(layout_name+"-short-"+label+"-tab")
                assert latest[cause_topic]["activeTab"]==index,latest[cause_topic]
                send("input.pointer_wheel",x=int(x+2),y=int(y+height/2),wheelDelta=-12000)
                ui=sample("short-shell-bottom")
                outer=latest[cause_topic]["shellScroll"]
                content_top=y+168-outer
                py=max(y+4,content_top+12)
                send("input.pointer_wheel",x=int(x+width/2),y=int(py),wheelDelta=-12000)
                ui=sample(layout_name+"-short-"+label+"-last")
                assert latest[cause_topic]["shellScroll"]==outer
                assert latest[cause_topic][field]>0,latest[cause_topic]
                assert latest["selection.state"]["selectedCausePrimaryId"]==identity["selectedCausePrimaryId"]
                assert latest["selection.state"]["selectedCauseCounterpartId"]==identity["selectedCauseCounterpartId"]
                capture(layout_name+"-short-"+label+"-last")
                send("input.pointer_wheel",x=int(x+width/2),y=int(py),wheelDelta=12000)
                ui=sample(layout_name+"-short-"+label+"-first")
                assert latest[cause_topic][field]==0
                assert latest[cause_topic]["shellScroll"]==outer
        for layout_name in ("Canvas", "Editor"):
            if ui["layout"] != layout_name:
                click(ui["window"][0]-110,20);ui=sample("hierarchy-"+layout_name)
            x,y,width,height=ui["causeControlsBounds"]
            send("input.pointer_wheel",x=int(x+2),y=int(y+height/2),wheelDelta=12000)
            ui=sample("hierarchy-shell-top")
            if latest[cause_topic]["drawerOpen"]:
                click(x+width-48,y+19);ui=sample("hierarchy-drawer-closed")
            send("input.pointer_wheel",x=int(x+2),y=int(y+height/2),wheelDelta=-240)
            ui=sample("hierarchy-content-visible")
            outer=latest[cause_topic]["shellScroll"]
            py=y+108-outer+19
            assert y<py<y+height
            send("input.pointer_wheel",x=int(x+width/2),y=int(py),wheelDelta=-12000)
            ui=sample("hierarchy-last-rows")
            assert latest[cause_topic]["scrollY"]>0,latest[cause_topic]
            assert latest[cause_topic]["shellScroll"]==outer
            click(x+width/2,py)
            send("run.until",condition="camera.inspection_settled",maxFrames=1000)
            ui=sample(layout_name+"-short-hierarchy-selected")
            selected=latest["selection.state"]["selectedCauseRow"]
            assert selected>2,latest["selection.state"]
            row=latest[cause_topic]["rows"][selected]
            assert latest["selection.state"]["selectedCausePrimaryId"]==row["id"]
            assert latest["selection.state"]["selectedCauseCounterpartId"]==row["counterpartId"]
            capture(layout_name+"-short-hierarchy-selected")
        print("PASS: native Causes identity/folding, inner Summary/Raw/Iterations scroll, and later hierarchy selection in short panes of both layouts")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-causes-ui")
    run(parser.parse_args().session)
