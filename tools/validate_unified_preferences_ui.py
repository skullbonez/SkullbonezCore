"""Verify native layout preferences across launches without touching user files."""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]

def run(root: Path) -> None:
    root.mkdir(parents=True, exist_ok=False)
    preferences = root / "ui-layout.preferences"
    scene = REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json"
    retained: dict = {}

    def session(label: str, mutate: bool = False, invalid: bool = False) -> None:
        directory = root / label
        assert launch(directory, REPO / "Automation/SKULLBONEZ_CORE.exe", scene,
                      hidden=True, layout_file=preferences) == 0
        connection = SkarnessConnection(directory)
        def send(command: str, **args: object) -> dict:
            result = connection.wait(connection.send(command, args))
            assert result.get("status") == "applied", result
            return result
        def sample(label: str) -> dict:
            send("run.step_frames", count=3)
            rows = [json.loads(line) for line in (directory / "runtime.skarness.ndjson").read_text().splitlines()]
            state = next(row["payload"] for row in reversed(rows) if row.get("topic") == "ui.presentation")
            cause = next(row["payload"] for row in reversed(rows) if row.get("topic") == "replay.cause")
            retained["observedSummarySection"] = cause["summaryExpandedSection"]
            (directory / f"{label}.json").write_text(json.dumps(state, indent=2))
            return state
        def click(x: float, y: float) -> None:
            send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0)
        try:
            assert "input.pointer_drag" in send("capabilities.get")["commands"]
            send("state.subscribe", topics=[], detail="normal")
            ui = sample("initial")
            width, height = ui["window"]
            assert not ui["toolsVisible"], ui
            if mutate:
                assert ui["layout"] == "Canvas" and ui["activeTool"] == 1
                click(width - 110, 20)
                ui = sample("editor")
                x, y, w, h = ui["leftResizeBounds"]
                send("input.pointer_drag", button="left", x=int(x + w / 2), y=int(y + h / 2),
                     deltaX=70, deltaY=0, moveClient=True)
                click(width - 38, 20)
                ui = sample("tools")
                drawer_y = ui["viewport"][1] + ui["viewport"][3] + 28
                click(14 + (width - 28) * 3.5 / 11, drawer_y + 66)
                send("input.pointer_drag", button="left", x=width // 2, y=int(drawer_y + 2),
                     deltaX=0, deltaY=-45, moveClient=True)
                ui = sample("resized-physics")
                assert ui["activeTool"] == 3
                retained["drawerViewport"] = ui["viewport"]
                click(width - 38, 20)
                ui = sample("tools-closed")
                retained["closedViewport"] = ui["viewport"]
                # Fold state is changed through the evidence presenter, then
                # restored without retaining any comparison or prediction data.
                send("replay.set_prediction_detail", highDetail=True)
                send("prediction.select_target", name="path_striker")
                send("replay.set_prediction_enabled", enabled=True)
                send("run.until", condition="prediction.complete", maxFrames=3000)
                ui = sample("prediction-ready")
                x, y, w, h = ui["causeControlsBounds"]
                click(x + 150, y + 108 + 2 * 38 + 12)
                send("run.until", condition="camera.inspection_settled", maxFrames=1000)
                sample("evidence-selected")
                click(x + w - 48, y + 19)
                sample("evidence-open")
                click(x + 35, y + 38 + 88 + 38 + 12 + 254 + 15)
                sample("summary-expanded")
                assert retained["observedSummarySection"] == 0
                x, y, w, h = ui["rightFoldBounds"]
                click(x + w / 2, y + h / 2)
                ui = sample("folded")
                retained["foldedViewport"] = ui["viewport"]
            elif invalid:
                assert ui["layout"] == "Canvas" and ui["activeTool"] == 1, ui
                assert retained["observedSummarySection"] == -1
            else:
                assert ui["layout"] == "Editor" and ui["activeTool"] == 3, ui
                assert retained["observedSummarySection"] == 0
                assert ui["viewport"] == retained["foldedViewport"], (ui, retained)
                x, y, w, h = ui["rightFoldBounds"]
                click(x + w / 2, y + h / 2)
                ui = sample("unfolded")
                assert ui["viewport"] == retained["closedViewport"]
                click(width - 38, 20)
                ui = sample("remembered-drawer")
                assert ui["viewport"] == retained["drawerViewport"] and ui["activeTool"] == 3
        finally:
            send("session.stop")
            connection.close()
        deadline = time.monotonic() + 10
        while not preferences.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        assert preferences.exists(), "Shutdown did not publish preferences"
        # A new launch must observe the flushed file after the old process exits.
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.windll.kernel32
        kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
        kernel.WaitForSingleObject.restype = wintypes.DWORD
        kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
        kernel.CloseHandle.restype = wintypes.BOOL
        handle = kernel.OpenProcess(0x00100000, False,
                    json.loads((directory / "session.json").read_text())["processId"])
        if handle:
            try:
                assert kernel.WaitForSingleObject(handle, 10000) == 0
            finally:
                kernel.CloseHandle(handle)

    session("first-launch", mutate=True)
    saved = preferences.read_text()
    assert "version 1" in saved and "layout 1" in saved and "tool 3" in saved and "folded 6" in saved, saved
    session("restored-launch")
    preferences.write_text("version 999\nlayout 1\n")
    session("invalid-version", invalid=True)
    preferences.write_text("version 1\nlayout broken\n")
    session("malformed-file", invalid=True)
    print("PASS: native preferences retain layout, dock width, folds, drawer height and last tool; Tools starts closed; malformed files fall back")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-preferences")
    run(parser.parse_args().session)
