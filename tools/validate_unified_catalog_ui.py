"""Check Targets and the recording catalog through native Tools controls."""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
from PIL import Image
from skarness import SkarnessConnection, launch
REPO = Path(__file__).resolve().parents[1]

def run(session: Path) -> None:
    session = session.resolve()
    recording = REPO / "TestOutput/recordings" / ("zz-ui-catalog-" + session.name)
    assert not recording.exists(), recording
    recording.mkdir(parents=True)
    manifest = recording / "interaction.json"
    manifest.write_text(json.dumps({"version": 1, "actions": [{"frame": 10, "pressKey": "F5"}]}))
    assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe",
                  REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json", hidden=True) == 0
    connection = SkarnessConnection(session)
    latest = {}
    offset = 0
    def send(command: str, **args):
        result = connection.wait(connection.send(command, args))
        assert result.get("status") == "applied", result
        return result
    def sample(label):
        nonlocal offset
        send("run.step_frames", count=3)
        with (session / "runtime.skarness.ndjson").open() as stream:
            stream.seek(offset)
            for line in stream:
                event = json.loads(line)
                if "topic" in event: latest[event["topic"]] = event["payload"]
            offset = stream.tell()
        ui = latest["ui.presentation"]
        (session / (label + ".json")).write_text(json.dumps(ui, indent=2))
        return ui
    def click(x, y, hold=0):
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0, holdMilliseconds=hold)
    def top(ui):
        return ui["viewport"][1] + ui["viewport"][3] + (28 if ui["layout"] == "Editor" else 0)
    def tab(ui, index):
        click(14 + (ui["window"][0] - 28) * (index + .5) / 11, top(ui) + 66)
        ui = sample("tab-" + str(index))
        assert ui["activeTool"] == index
        return ui
    def capture(label):
        path = session / (label + ".png")
        send("capture.screenshot", path=str(path))
        with Image.open(path) as image: image.save(session / (label + "-view.png"))
    try:
        assert "input.pointer_drag" in send("capabilities.get")["commands"]
        send("state.subscribe", topics=[], detail="normal")
        ui = sample("initial")
        for layout in ("Canvas", "Editor"):
            if ui["layout"] != layout:
                click(ui["window"][0] - 110, 20)
                ui = sample(layout)
            if not ui["toolsVisible"]:
                click(ui["window"][0] - 30, 20)
                ui = sample(layout + "-tools")
            ui = tab(ui, 6)
            count = ui["targetOptions"]
            assert count > 0
            for index in range(count):
                send("input.pointer_wheel", x=300, y=int(top(ui) + 215), wheelDelta=12000)
                ui = sample(layout + "-targets-top")
                click(500, top(ui) + 154)
                ui = sample(layout + "-targets-open")
                while not ui["targetFirstOption"] <= index < ui["targetFirstOption"] + ui["targetVisibleOptions"]:
                    x, y, w, h = ui["targetPopupBounds"]
                    send("input.pointer_wheel", x=int(x+w/2), y=int(y+h/2), wheelDelta=-120 if index >= ui["targetFirstOption"] else 120)
                    ui = sample("target-scroll")
                x, y, w, h = ui["targetPopupBounds"]
                previous = ui["selectedTarget"]
                disabled = ui["targetDisabledMask"] & (1 << index)
                click(x + w/2, y + (index-ui["targetFirstOption"]+.5) * h/ui["targetVisibleOptions"])
                ui = sample(f"{layout}-target-{index}")
                assert ui["selectedTarget"] == (previous if disabled else index), (index, ui)
                click(3, top(ui) + 95)
                send("input.pointer_wheel", x=300, y=int(top(ui) + 215), wheelDelta=-120)
                ui = sample("preview")
                capture(f"{layout}-target-{index}")
            # The test-owned manifest sorts first. A completed child report is the
            # operation result; menu selection or an existing file is insufficient.
            ui = tab(ui, 1)
            send("input.pointer_wheel", x=300, y=int(top(ui)+215), wheelDelta=12000)
            ui = sample(layout + "-scene")
            report = recording / "playback-report.json"
            before = report.stat().st_mtime_ns if report.exists() else 0
            click(300, top(ui) + 186)
            ui = sample(layout + "-recordings-open")
            assert ui["recordingOptions"] > 0 and ui["recordingFirstOption"] == 0, ui
            capture(layout + "-recordings-open")
            x, y, w, h = ui["recordingPopupBounds"]
            click(x+w/2, y+h/ui["recordingVisibleOptions"]/2)
            ui = sample(layout + "-recording-launched")
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                if report.exists() and report.stat().st_mtime_ns > before:
                    break
                time.sleep(.1)
            assert report.exists() and report.stat().st_mtime_ns > before
            result = json.loads(report.read_text())
            (session / (layout + "-playback-report.json")).write_text(json.dumps(result, indent=2))
            assert result["ok"] and Path(result["script"]) == manifest, result
            assert result["actions"] == [{"frame": 10, "type": "pressKey", "target": "F5", "consumed": True, "detail": "key press injected"}], result
            trace = recording / "playback-trace.jsonl"
            assert trace.exists() and trace.stat().st_size > 0
            (session / (layout + "-playback-trace.jsonl")).write_bytes(trace.read_bytes())
        print("PASS: every live/disabled target entry and native recording-catalog playback in both layouts")
    finally:
        try: send("session.stop")
        finally: connection.close()
        # Preserve the evidence but retire the test entry from future catalogs.
        archived = recording / "interaction.test-complete.json"
        if manifest.exists(): manifest.rename(archived)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    run(parser.parse_args().session)
