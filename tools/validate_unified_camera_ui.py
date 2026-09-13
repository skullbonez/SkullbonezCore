"""Verify the native header camera selector in both layouts."""
from __future__ import annotations

import argparse
import json
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
        (session / f"{label}.json").write_text(json.dumps(latest, indent=2), encoding="utf-8")
        return latest["ui.presentation"]

    def click(x: float, y: float) -> None:
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0)

    try:
        capabilities = send("capabilities.get")
        assert "input.pointer_drag" in capabilities["commands"]
        send("state.subscribe", topics=[], detail="normal")
        ui = sample("initial")
        scene_identity = dict(latest["scene.objects"])
        for layout in ("Canvas", "Editor"):
            if ui["layout"] != layout:
                click(ui["window"][0] - 110, 20)
                ui = sample("editor-layout")
            assert ui["layout"] == layout
            for mode in range(7):
                px, _, _, _ = ui["cameraPopupBounds"]
                click(px + 20, 20)
                ui = sample(f"{layout}-camera-{mode}-open")
                assert ui["cameraPopupOpen"]
                if mode == 0:
                    send("capture.screenshot", path=str((session / f"{layout}-camera-popup.png").resolve()))
                px, py, _, ph = ui["cameraPopupBounds"]
                before = ui["cameraMode"]
                enabled = bool(ui["cameraModeEnabledMask"] & (1 << mode))
                click(px + 20, py + (mode + 0.5) * ph / 7)
                ui = sample(f"{layout}-camera-{mode}-selected")
                assert ui["cameraMode"] == (mode if enabled else before), (layout, mode, ui)
                assert ui["cameraPopupOpen"] != enabled
                if not enabled:
                    click(px + 20, 20)
                    ui = sample(f"{layout}-disabled-dismissed")
                    assert not ui["cameraPopupOpen"]
                assert latest["scene.objects"] == scene_identity
            assert ui["layout"] == layout
        print("PASS: all seven header camera options in Canvas and Editor, including disabled availability and scene identity")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-camera-ui")
    run(parser.parse_args().session)
