"""Drive native recording Save and Load, observing the actual loaded artifact."""
from __future__ import annotations

import argparse
import copy
import json
import shutil
import time
from pathlib import Path
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    session = session.resolve()
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
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0,
             holdMilliseconds=70)

    def row(ui: dict, index: int) -> None:
        x, y, w, h = ui["replayControlsBounds"]
        assert w > 0
        scroll = ui["replayScroll"] * max(0, 370 - h)
        click(x + w / 2, y + 19 + index * 32 - scroll)

    def load(ui: dict, path: Path | None, label: str) -> dict:
        before = ui["fileDialogResponsesConsumed"]
        send("input.file_dialog_response", purpose="replay.load", accepted=path is not None,
             **({"path": str(path)} if path is not None else {}))
        row(ui, 8)
        ui = sample(label)
        assert ui["fileDialogResponsesConsumed"] == before + 1
        return ui

    try:
        capabilities = send("capabilities.get")
        assert "input.file_dialog_response" in capabilities["commands"]
        send("state.subscribe", topics=[], detail="normal")
        send("run.resume")
        time.sleep(0.5)
        send("run.pause")
        ui = sample("history")
        width = ui["window"][0]
        for layout in ("Canvas", "Editor"):
            send("replay.return_to_live")
            ui = sample(layout + "-live")
            if ui["layout"] != layout:
                click(width - 110, 20)
                ui = sample(layout)
                x, y, w, h = ui["editorReplayTabBounds"]
                click(x + w / 2, y + h / 2)
            else:
                x, y, w, h = ui["replayDetailsBounds"]
                click(x + w / 2, y + h / 2)
            ui = sample(layout + "-details")
            x, y, w, h = ui["replayControlsBounds"]
            send("input.pointer_wheel", x=int(x + w / 2), y=int(y + h / 2), wheelDelta=-1200)
            ui = sample(layout + "-recording-controls")
            history = copy.deepcopy(latest["replay.timeline"]["presentation"])
            previous = set((REPO / "replays").glob("replay_v2_*.skreplay"))
            row(ui, 7)
            ui = sample(layout + "-saved")
            created = set((REPO / "replays").glob("replay_v2_*.skreplay")) - previous
            assert len(created) == 1, (created, latest["replay.timeline"])
            saved = created.pop()
            assert saved.stat().st_size > 0
            assert latest["replay.timeline"]["scrubber"]["feedback"] == "SAVED " + saved.name
            artifact = session / f"{layout}.skreplay"
            shutil.copyfile(saved, artifact)
            # The numbered writer selected a previously absent path for this
            # click. Keep its bytes in the session before removing that file.
            assert saved.parent.resolve() == (REPO / "replays").resolve()
            saved.unlink()
            ui = load(ui, artifact, layout + "-loaded")
            loaded = copy.deepcopy(latest["replay.timeline"]["loaded"])
            assert loaded["path"] == str(artifact)
            assert loaded["sampleCount"] == history["sampleCount"]
            assert loaded["lastFrame"] == history["nextFrame"] - 1
            assert latest["input.state"]["scrubPaused"]
            selected_camera = latest["camera.state"]["selectedCameraHash"]
            ui = load(ui, None, layout + "-cancelled")
            assert latest["replay.timeline"]["loaded"] == loaded
            assert latest["camera.state"]["selectedCameraHash"] == selected_camera
            ui = load(ui, session / "missing.skreplay", layout + "-missing")
            assert "FAILED" in latest["replay.timeline"]["scrubber"]["feedback"]
            send("capture.screenshot", path=str(session / f"{layout}-load-error.png"))
            ui = load(ui, artifact, layout + "-recovered")
            assert latest["replay.timeline"]["loaded"] == loaded
        print("PASS: native Save recording, Load recording, cancellation, missing file and recovery in both layouts")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-replay-files-ui")
    run(parser.parse_args().session)
