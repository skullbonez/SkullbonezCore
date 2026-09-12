"""Check F5/F6 retention and viewport placement through native shell changes."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from PIL import Image
from skarness import SkarnessConnection, launch
REPO = Path(__file__).resolve().parents[1]

def run(session: Path):
    session = session.resolve()
    assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe", REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json", hidden=True) == 0
    connection = SkarnessConnection(session)
    offset = 0
    latest = {}
    def send(command, **arguments):
        result = connection.wait(connection.send(command, arguments))
        assert result.get("status") == "applied", result
        return result
    def sample(label):
        nonlocal offset
        send("run.step_frames", count=4)
        with (session / "runtime.skarness.ndjson").open() as trace:
            trace.seek(offset)
            for line in trace:
                event = json.loads(line)
                if event.get("topic") == "ui.presentation": latest.update(event["payload"])
            offset = trace.tell()
        (session / (label + ".json")).write_text(json.dumps(latest, indent=2))
        return dict(latest)
    def click(bounds):
        x,y,w,h = bounds
        send("input.pointer_drag", button="left", x=int(x+w/2), y=int(y+h/2), deltaX=0, deltaY=0)
    def verify(label):
        ui = sample(label)
        x,y,w,h = ui["viewport"]
        y = max(y, 42 if ui["window"][1] >= 280 else ui["window"][1]*.15)
        bottom = ui["transportBounds"][1]
        right = x+w
        if ui["layout"] == "Canvas" and ui["replayDetailsBounds"][2] and ui["replayControlsBounds"][2]:
            right = min(right, ui["replayControlsBounds"][0])
        for key in ("markerHistoryBounds", "memoryWaterlineBounds"):
            px,py,pw,ph = ui[key]
            assert pw>0 and ph>0, (label,key,ui)
            assert px >= x-.01 and py >= y-.01 and px+pw <= right+.01 and py+ph <= bottom+.01, (label,key,ui)
        path = session / (label + ".png")
        send("capture.screenshot", path=str(path))
        with Image.open(path) as image: image.save(session / (label + "-view.png"))
        return ui
    try:
        commands = send("capabilities.get")["commands"]
        assert {"input.set_key", "input.pointer_drag", "window.resize"} <= set(commands)
        send("state.subscribe", topics=[], detail="normal")
        for key in (116,117):
            send("input.set_key", key=key, down=True)
            send("run.step_frames", count=2)
            send("input.set_key", key=key, down=False)
        ui = verify("canvas")
        click(ui["headerLayoutBounds"])
        ui = verify("editor")
        assert ui["layout"] == "Editor"
        for key in ("leftFoldBounds", "rightFoldBounds"):
            click(ui[key])
            ui = verify("expanded-" + key)
        assert ui["viewport"][0] > 24

        click(ui["replayDetailsBounds"])
        ui = verify("tools-open")
        assert ui["toolsVisible"], ui
        click(ui["replayDetailsBounds"])
        ui = verify("tools-closed")
        assert not ui["toolsVisible"], ui
        for key in ("markerHistoryBounds", "memoryWaterlineBounds"):
            x,y,w,h = ui[key]
            send("input.pointer_drag", button="left", x=int(x+30), y=int(y+12), deltaX=-1500, deltaY=-1000, moveClient=True)
            ui = verify("drag-" + key)
        for width,height in ((640,480),(320,240),(1784,961)):
            send("window.resize", width=width, height=height)
            ui = verify(str(width))
        print("PASS: floating diagnostics persist and stay inside the viewport")
    finally:
        try: send("session.stop")
        finally: connection.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    run(parser.parse_args().session)
