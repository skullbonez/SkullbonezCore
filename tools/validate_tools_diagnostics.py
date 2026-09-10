"""Verify Escape, Solver Lab entry and floating diagnostics through native input."""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
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
        latest["comparison"] = send("comparison.state")["result"]["comparison"]
        (session / f"{label}.json").write_text(json.dumps(latest, indent=2), encoding="utf-8")
        return latest["ui.presentation"]

    def click(x: float, y: float) -> None:
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0)

    def key(code: int, label: str) -> dict:
        send("input.set_key", key=code, down=True)
        pressed = sample(label + "-held")
        send("input.set_key", key=code, down=False)
        released = sample(label)
        assert pressed["layout"] == released["layout"]
        assert pressed["toolsVisible"] == released["toolsVisible"]
        return released

    def capture(label: str) -> None:
        send("input.pointer_position", x=900, y=450, enabled=True)
        send("capture.screenshot", path=str(session / f"{label}.png"))

    def fullscreen(ui: dict) -> None:
        assert ui["workspace"] == "Scene" and ui["layout"] == "Canvas", ui
        assert ui["viewport"] == [0, 0, *ui["window"]], ui
        assert not ui["toolsVisible"] and ui["replayControlsBounds"][2] == 0, ui
        assert ui["causeControlsBounds"][2] == 0, ui

    def loaded(label: str, suffix: str) -> dict:
        deadline = time.monotonic() + 90
        while True:
            ui = sample(label)
            comparison = latest["comparison"]
            assert not comparison["loadError"], comparison
            if comparison["active"] and not comparison["loading"]:
                assert comparison["bundle"].replace("\\", "/").endswith(suffix), comparison
                assert all(comparison["coverage"]) and comparison["lastTick"] > 0, comparison
                return ui
            assert time.monotonic() < deadline, comparison

    try:
        capabilities = send("capabilities.get")["commands"]
        assert {"input.set_key", "input.pointer_drag", "capture.screenshot", "comparison.state"} <= set(capabilities)
        send("state.subscribe", topics=[], detail="normal")
        ui = sample("initial")
        width, height = ui["window"]
        fullscreen(ui)
        ui = key(0x74, "canvas-f5")
        ui = key(0x75, "canvas-both")
        assert ui["markerHistoryVisible"] and ui["memoryWaterlineVisible"]
        fullscreen(ui)
        hx, hy, hw, hh = ui["markerHistoryBounds"]
        send("input.pointer_drag", button="left", x=int(hx+hw/2), y=int(hy+27), deltaX=120,
             deltaY=60, moveClient=True)
        ui = sample("floating-moved")
        assert ui["markerHistoryBounds"][0] != hx, ui
        floating = ui["markerHistoryBounds"]
        capture("canvas-floating")
        for layout in ("Editor", "Canvas"):
            click(width - 110, 20)
            ui = sample(layout + "-layout")
            assert ui["layout"] == layout
            viewport = ui["viewport"]
            assert ui["markerHistoryBounds"] == floating
            click(width - 38, 20)
            ui = sample(layout + "-menu")
            assert ui["toolsVisible"] and ui["markerHistoryVisible"] and ui["memoryWaterlineVisible"]
            menu_viewport = ui["viewport"]
            tools = ui["toolsContentBounds"]
            ui = key(0x74, layout + "-f5-off")
            assert not ui["markerHistoryVisible"] and ui["memoryWaterlineVisible"]
            assert ui["viewport"] == menu_viewport and ui["toolsContentBounds"] == tools
            ui = key(0x74, layout + "-f5-on")
            assert ui["markerHistoryBounds"] == floating
            hx, hy, hw, hh = ui["markerHistoryBounds"]
            send("input.pointer_drag", button="left", x=int(hx+hw/2), y=int(hy+12), deltaX=30,
                 deltaY=20, moveClient=True)
            ui = sample(layout + "-menu-dragged")
            assert ui["markerHistoryBounds"][0] == hx+30, ui
            assert ui["viewport"] == menu_viewport and ui["toolsContentBounds"] == tools
            floating = ui["markerHistoryBounds"]
            capture(layout + "-menu-floating")
            click(width - 38, 20)
            ui = sample(layout + "-menu-closed")
            assert ui["viewport"] == viewport and ui["markerHistoryVisible"] and ui["memoryWaterlineVisible"]
        click(width - 110, 20)
        ui = key(0x1b, "escape-editor")
        fullscreen(ui)
        assert ui["markerHistoryVisible"] and ui["memoryWaterlineVisible"]
        key(0x74, "hide-f5")
        ui = key(0x75, "hide-f6")
        click(width - 38, 20)
        ui = sample("profiler-menu")
        cx, cy, cw, ch = ui["toolsContentBounds"]
        click(cx+24, cy-34)
        ui = sample("profiler-top")
        assert ui["activeTool"] == 0
        assert ui["workerSliderBounds"][1] > cy + 142
        capture("profiler-top")
        # Collapse the root and scroll the worker controls into the content clip.
        click(cx+25, cy+47)
        ui = sample("profiler-root-folded")
        before = ui["workerThreads"]
        for attempt in range(30):
            sx, sy, sw, sh = ui["workerSliderBounds"]
            if cy+32 <= sy and sy+sh <= cy+ch:
                break
            send("input.pointer_wheel", x=int(cx+cw/2), y=int(cy+ch/2), wheelDelta=-120)
            ui = sample("profiler-scrolled")
        else:
            raise AssertionError(ui)
        capture("profiler-worker-controls")
        click(sx+118, sy+17)
        ui = sample("workers-zero")
        assert ui["workerThreads"] == 0, ui
        tx, ty, tw, th = ui["workerToggleBounds"]
        click(tx+12, ty+12)
        ui = sample("workers-restored")
        assert ui["workerThreads"] > 0, ui
        ui = key(0x1b, "escape-tools")
        fullscreen(ui)
        rx, ry, rw, rh = ui["replayDetailsBounds"]
        click(rx+rw/2, ry+rh/2)
        ui = sample("canvas-details")
        assert ui["replayControlsBounds"][2] > 0
        ui = key(0x1b, "escape-details")
        fullscreen(ui)
        click(width - 403, 20)
        ui = loaded("first-lab", "ragdoll-wall/comparison.json")
        assert ui["workspace"] == "Solver Lab"
        capture("first-lab")
        rx, ry, rw, rh = ui["replayDetailsBounds"]
        click(rx+rw/2, ry+rh/2)
        ui = sample("lab-details")
        cx, cy, cw, ch = ui["replayControlsBounds"]
        assert cw > 0
        click(cx+30, cy+44)
        ui = sample("lab-library")
        assert latest["comparison"]["libraryPopupOpen"]
        px, py, pw, ph = latest["comparison"]["libraryPopup"]
        click(px+pw/2, py+ph*.75)
        ui = loaded("second-lab", "wall-only/comparison.json")
        capture("second-lab")
        cx, cy, cw, ch = ui["replayControlsBounds"]
        click(cx+cw*.75, cy+114)
        ui = sample("mouse-exit-lab")
        fullscreen(ui)
        assert latest["comparison"]["active"]
        click(width - 403, 20)
        ui = loaded("retained-lab", "wall-only/comparison.json")
        ui = key(0x1b, "escape-lab")
        fullscreen(ui)
        # Escape has priority over a focused native popup on a docked surface.
        click(width - 110, 20)
        click(width - 38, 20)
        ui = sample("docked-tools")
        click(width - 247, 20)
        ui = sample("camera-popup")
        assert ui["cameraPopupOpen"], ui
        ui = key(0x1b, "escape-popup-and-docks")
        fullscreen(ui)
        assert not ui["cameraPopupOpen"]
        capture("escape-fullscreen")
        print("PASS: native Escape, first Solver Lab load, replacement, mouse exit, floating F5/F6 and worker controls", flush=True)
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    run(parser.parse_args().session.resolve())

