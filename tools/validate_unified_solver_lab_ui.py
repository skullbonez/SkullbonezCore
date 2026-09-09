"""Drive shared Solver Lab controls and retained workspace transitions natively."""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]

def run(session: Path) -> None:
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
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0)

    def capture(label: str) -> None:
        send("capture.screenshot", path=str((session / f"{label}.png").resolve()))

    try:
        capabilities = send("capabilities.get")
        assert {"comparison.load", "input.pointer_drag", "scene.load"} <= set(capabilities["commands"])
        send("state.subscribe", topics=[], detail="normal")
        ui = sample("initial")
        width, _ = ui["window"]
        click(width - 110, 20)
        ui = sample("editor")
        send("comparison.load", path=str(bundle))
        ui = sample("loaded")
        assert ui["workspace"] == "Solver Lab" and ui["layout"] == "Editor"
        assert ui["editorControlsBounds"][2] == 0
        assert ui["markerHistoryVisible"] and ui["memoryWaterlineVisible"]
        assert latest["comparison"]["active"]
        capture("editor-lab")
        for key, diagnostic in ((0x74, 1), (0x75, 2)):
            send("input.set_key", key=key, down=True)
            ui = sample(f"diagnostic-shortcut-{diagnostic}")
            assert ui["focusedDiagnostic"] == diagnostic
            assert ui["markerHistoryVisible"] and ui["memoryWaterlineVisible"]
            send("input.set_key", key=key, down=False)
            ui = sample(f"diagnostic-shortcut-release-{diagnostic}")


        send("comparison.seek", tick=1)
        send("comparison.select", sceneObjectId=1)
        send("comparison.focus")
        ui = sample("pick-focus")
        for stacked in (False, True):
            send("comparison.setting", name="stackedViews", value=1 if stacked else 0)
            ui = sample("pick-stacked" if stacked else "pick-side-by-side")
            vx, vy, vw, vh = latest["comparison"]["viewport"]
            for side in (0, 1):
                px = vx + vw / 2 if stacked else vx + vw * (0.25 + side * 0.5)
                py = vy + vh * (0.25 + side * 0.5) if stacked else vy + vh / 2
                click(px, py)
                ui = sample(f"pick-{stacked}-{side}")
                assert latest["comparison"]["selected"] == 1, latest["comparison"]
        send("comparison.setting", name="stackedViews", value=0)
        ui = sample("paired-picking-complete")
        dx, dy, dw, _ = ui["causeControlsBounds"]
        click(dx + 24, dy + 10 + 166 + 13)
        ui = sample("selected-recorded-event")
        assert latest["comparison"]["selectedEvent"] >= 0 and latest["comparison"]["selected"] > 0
        selected = latest["comparison"]["selected"]
        tick = latest["comparison"]["tick"]
        capture("selected-event-plots")
        click(width - 38, 20)
        ui = sample("tools-in-lab")
        assert ui["toolsVisible"] and ui["workspace"] == "Solver Lab"
        assert latest["comparison"]["selected"] == selected and latest["comparison"]["tick"] == tick
        capture("tools-in-lab")
        click(width - 38, 20)
        ui = sample("tools-closed")
        assert latest["comparison"]["selected"] == selected

        x, y, _, _ = ui["replayControlsBounds"]
        for mode in range(5):
            click(x + 20 + (mode % 2) * 133, y + 10 + 152 + (mode // 2) * 34 + 14)
            ui = sample(f"mode-{mode}")
            assert latest["comparison"]["mode"] == mode, latest["comparison"]
        click(x + 20, y + 10 + 152 + 14)
        ui = sample("split-restored")
        before = latest["comparison"]["stackedViews"]
        click(x + 20, y + 10 + 254 + 14)
        ui = sample("stacked")
        assert latest["comparison"]["stackedViews"] != before
        capture("stacked-lab")

        x, y, control_width, _ = ui["replayControlsBounds"]
        half = (control_width - 26) / 2
        for row, key in ((220, "showA"), (254, "xray"), (288, "followA")):
            before = latest["comparison"][key]
            click(x + 10 + half + 6 + 20, y + 10 + row + 14)
            ui = sample(key + "-on")
            assert latest["comparison"][key] != before
            click(x + 10 + half + 6 + 20, y + 10 + row + 14)
            ui = sample(key + "-restored")
            assert latest["comparison"][key] == before
        before = latest["comparison"]["speed"]
        click(x + 10 + half + 6 + 20, y + 10 + 322 + 14)
        ui = sample("speed")
        assert latest["comparison"]["speed"] != before
        before = latest["comparison"]["loopEnabled"]
        click(x + 30, y + 10 + 356 + 14)
        ui = sample("loop")
        assert latest["comparison"]["loopEnabled"] != before
        dx, dy, dw, _ = ui["causeControlsBounds"]
        click(dx + dw - 30, dy + 24)
        ui = sample("all-differences")
        assert not latest["comparison"]["selectedOnly"]
        click(dx + 30, dy + 24)
        ui = sample("selected-differences")
        assert latest["comparison"]["selectedOnly"]
        before = latest["comparison"]["differencesOnly"]
        click(dx + 30, dy + 58)
        ui = sample("differences-filter")
        assert latest["comparison"]["differencesOnly"] != before
        before = latest["comparison"]["positionThreshold"]
        click(dx + 30, dy + 92)
        ui = sample("threshold")
        assert latest["comparison"]["positionThreshold"] != before
        send("comparison.seek", tick=200)
        tx, ty, _, _ = ui["transportBounds"]
        click(tx + 14, ty + 14)
        ui = sample("single-step-back")
        assert latest["comparison"]["tick"] == 199
        click(tx + 94, ty + 14)
        ui = sample("single-step-forward")
        assert latest["comparison"]["tick"] == 200
        sx, sy, sw, sh = latest["comparison"]["timeline"]
        start = int(sx + sw * 0.25)
        send("input.pointer_drag", button="left", x=start, y=int(sy + sh / 2), deltaX=120,
             deltaY=-80, moveClient=True)
        ui = sample("scrubber-capture-outside")
        expected = int((start + 120 - sx) / sw * latest["comparison"]["lastTick"])
        assert abs(latest["comparison"]["tick"] - expected) <= 1
        assert not latest["comparison"]["timelineDragging"]
        send("input.pointer_position", x=int(sx + sw / 2), y=int(sy + sh / 2), enabled=True)
        send("state.subscribe", topics=["ui.presentation"], detail="normal")
        until = time.monotonic() + 0.6
        while time.monotonic() < until:
            connection.read_event()
        capture("transport-tooltip")
        send("state.subscribe", topics=[], detail="normal")
        send("input.pointer_position", x=0, y=0, enabled=False)
        send("comparison.seek", tick=500)
        send("comparison.play", direction=1)
        click(width - 403, 20)
        ui = sample("scene-retained-comparison")
        assert ui["workspace"] == "Scene"
        retained = dict(latest["comparison"])
        assert retained["active"] and retained["direction"] == 0
        send("run.step_frames", count=12)
        ui = sample("background-still-paused")
        assert latest["comparison"]["tick"] == retained["tick"]
        send("scene.load_demo")
        ui = sample("other-scene-loaded")
        assert ui["workspace"] == "Scene" and latest["comparison"]["active"]
        before_scene_frame = latest["frame.clocks"]["sceneFrame"]
        send("run.step", count=3)
        ui = sample("scene-advances-with-retained-comparison")
        assert latest["frame.clocks"]["sceneFrame"] > before_scene_frame
        assert latest["comparison"]["tick"] == retained["tick"]
        click(width - 403, 20)
        ui = sample("lab-restored-after-scene-load")
        assert ui["workspace"] == "Solver Lab"
        restored = latest["comparison"]
        assert restored["tick"] == retained["tick"] and restored["direction"] == 0
        assert restored["cameraEye"] == retained["cameraEye"] and restored["cameraView"] == retained["cameraView"]
        assert restored["mode"] == retained["mode"] and restored["stackedViews"] == retained["stackedViews"]
        capture("retained-lab-after-scene-load")
        click(width - 110, 20)
        ui = sample("canvas-lab")
        assert ui["layout"] == "Canvas" and ui["workspace"] == "Solver Lab"
        assert latest["comparison"]["tick"] == retained["tick"]
        capture("canvas-lab")
        rx, ry, rw, rh = ui["replayDetailsBounds"]
        click(rx + rw / 2, ry + rh / 2)
        ui = sample("canvas-controls")
        cx, cy, cw, ch = ui["replayControlsBounds"]
        assert cw > 0 and ch > 0
        before_mode = latest["comparison"]["mode"]
        click(cx + 30, cy + 10 + 152 + 14)
        ui = sample("canvas-split-control")
        assert latest["comparison"]["mode"] == 0
        click(cx + 30, cy + 10 + 22 + 12)
        ui = sample("library-popup")
        assert latest["comparison"]["libraryPopupOpen"]
        capture("library-popup")
        click(width - 110, 20)
        ui = sample("popup-dismiss-no-layout-click-through")
        assert ui["layout"] == "Canvas" and not latest["comparison"]["libraryPopupOpen"]
        click(cx + 30, cy + 10 + 22 + 12)
        ui = sample("library-popup-refocus")
        assert latest["comparison"]["libraryPopupOpen"]
        send("input.set_focus", focused=False)
        send("run.step_frames", count=3)
        send("input.set_focus", focused=True)
        ui = sample("focus-loss-closes-popup")
        assert not latest["comparison"]["libraryPopupOpen"]
        assert latest["comparison"]["tick"] == retained["tick"]
        click(cx + 30, cy + 10 + 22 + 12)
        ui = sample("library-same-selection")
        px, py, pw, ph = latest["comparison"]["libraryPopup"]
        click(px + pw / 2, py + ph * 0.75)
        ui = sample("library-same-retained")
        assert not latest["comparison"]["libraryPopupOpen"]
        assert latest["comparison"]["tick"] == retained["tick"]
        dx, dy, dw, dh = ui["detailsCausesTabBounds"]
        click(dx + dw / 2, dy + dh / 2)
        ui = sample("canvas-differences")
        assert ui["causeControlsBounds"][2] > 0 and ui["replayControlsBounds"][2] == 0
        capture("canvas-differences")
        click(cx + 30, cy + 10 + 152 + 14)
        ui = sample("hidden-controls-do-not-change-mode")
        assert latest["comparison"]["mode"] == 0

        before_resize_tick = latest["comparison"]["tick"]
        send("window.resize", width=900, height=640)
        ui = sample("narrow-canvas")
        assert ui["window"] == [900, 640] and ui["layout"] == "Canvas"
        assert latest["comparison"]["tick"] == before_resize_tick
        click(900 - 110, 20)
        ui = sample("narrow-editor")
        assert ui["layout"] == "Editor"
        capture("narrow-editor")
        click(900 - 38, 20)
        ui = sample("narrow-editor-tools")
        capture("narrow-editor-tools")
        lx, ly, lw, lh = ui["replayControlsBounds"]
        before_speed = latest["comparison"]["speed"]
        send("input.pointer_wheel", x=int(lx + lw / 2), y=int(ly + lh / 2), wheelDelta=-1200)
        ui = sample("narrow-controls-scrolled")
        capture("narrow-controls-scrolled")
        send("input.pointer_wheel", x=int(lx + lw / 2), y=int(ly + lh / 2), wheelDelta=120)
        ui = sample("narrow-speed-row-visible")
        scroll = max(0, 460 - lh - 36)
        half = (lw - 26) / 2
        click(lx + 10 + half + 26, ly + 10 + 322 + 14 - scroll)
        ui = sample("narrow-speed-control")
        assert latest["comparison"]["speed"] != before_speed
        send("window.resize", width=width, height=961)
        ui = sample("window-restored")
        send("comparison.close")
        ui = sample("closed-comparison")
        assert not latest["comparison"]["active"]
        capture("empty-lab")
        print("PASS: shared Solver Lab modes/orientation, paused background retention, scene reload, camera restoration and close")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-solver-lab-ui")
    run(parser.parse_args().session)
