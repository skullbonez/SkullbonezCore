"""Drive scene browser creation, loading and defaults through native shared UI."""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]

def run(session: Path) -> None:
    name = "unifieduitest" + str(time.time_ns())
    authored = (REPO / "SkullbonezData/scenes" / (name + ".scene.json")).resolve()
    assert not authored.exists()
    assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe",
                  REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json", hidden=True) == 0
    connection = SkarnessConnection(session)
    latest: dict = {}
    offset = 0
    def send(command: str, **args: object) -> dict:
        result = connection.wait(connection.send(command, args))
        assert result.get("status") == "applied", result
        return result
    def sample(label: str) -> dict:
        nonlocal offset
        send("run.step_frames", count=3)
        with (session / "runtime.skarness.ndjson").open() as trace:
            trace.seek(offset)
            for line in trace:
                row = json.loads(line)
                if "topic" in row:
                    latest[row["topic"]] = row["payload"]
            offset = trace.tell()
        (session / f"{label}.json").write_text(json.dumps(latest, indent=2))
        return latest["ui.presentation"]
    def click(x: float, y: float) -> None:
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0)
    def key(code: int) -> None:
        send("input.set_key", key=code, down=True)
        send("run.step_frames", count=1)
        send("input.set_key", key=code, down=False)
        send("run.step_frames", count=1)
    def text(value: str) -> None:
        for char in value:
            key(ord(char.upper()) if char.isalnum() else 0xBD if char == "-" else 0x20)
    def drawer_y(ui: dict) -> float:
        return ui["viewport"][1] + ui["viewport"][3] + (28 if ui["layout"] == "Editor" else 0)
    def open_scenes(ui: dict) -> float:
        click(ui["window"][0] - 189, 20)
        ui = sample("scenes-open")
        assert ui["toolsVisible"] and ui["activeTool"] == 1
        return drawer_y(ui)
    def select_name(ui: dict, value: str, label: str, existing: bool = False) -> dict:
        top = open_scenes(ui)
        click(200, top + 154)
        sample(label + "-popup")
        send("capture.screenshot", path=str((session / (label + "-popup.png")).resolve()))
        text(value)
        sample(label + "-filtered")
        send("capture.screenshot", path=str((session / (label + "-filtered.png")).resolve()))
        if existing:
            click(200, top + 198)
        else:
            key(0x0D)
        return sample(label)
    try:
        assert {"input.set_key", "input.pointer_drag"} <= set(send("capabilities.get")["commands"])
        send("state.subscribe", topics=[], detail="normal")
        ui = sample("initial")
        width, _ = ui["window"]
        initial_generation = latest["session.state"]["sceneGeneration"]
        ui = select_name(ui, "  ", "invalid-create")
        assert latest["session.state"]["sceneGeneration"] == initial_generation
        ui = select_name(ui, name, "created-scene")
        assert authored.exists(), latest["scene.state"]
        assert Path(latest["scene.state"]["scenePath"]).name == authored.name
        assert latest["scene.state"]["sceneMode"] and latest["scene.state"]["physicsEnabled"]
        (session / "created-starter.json").write_bytes(authored.read_bytes())
        send("capture.screenshot", path=str((session / "created-scene.png").resolve()))
        for mode in ("Canvas", "Editor"):
            if ui["layout"] != mode:
                click(width - 110, 20)
                ui = sample("layout-" + mode)
            top = open_scenes(ui)
            ui = sample(mode + "-before-scale")
            send("input.pointer_wheel", x=300, y=int(top + 230), wheelDelta=-240)
            ui = sample(mode + "-scrolled")
            click(650, top + 100 + 228 - 84 + 17)
            ui = sample(mode + "-scale-edited")
            scale = latest["scene.state"]["timeScale"]
            assert scale != 1, latest["scene.state"]
            send("input.pointer_wheel", x=300, y=int(top + 230), wheelDelta=1200)
            ui = sample(mode + "-scroll-top")
            generation = latest["session.state"]["sceneGeneration"]
            click(580, top + 154)
            ui = sample(mode + "-reset-live-controls")
            assert latest["session.state"]["sceneGeneration"] > generation
            assert latest["scene.state"]["timeScale"] == scale
            top = drawer_y(ui)
            click(690, top + 154)
            ui = sample(mode + "-reset-authored-defaults")
            assert latest["scene.state"]["timeScale"] == 1, latest["scene.state"]
            top = open_scenes(ui)
            # Save must persist a changed authored value, not merely leave an
            # already existing file behind. The fluid control uses the existing
            # Keys presenter and the scene-owned environment save operation.
            click(14 + (width - 28) * 7.5 / 11, top + 66)
            ui = sample(mode + "-save-value-keys")
            assert ui["activeTool"] == 7
            send("input.pointer_wheel", x=300, y=int(top + 220), wheelDelta=-1200)
            sample(mode + "-save-value-scroll")
            expected_height = 200 if mode == "Canvas" else -100
            click(width - 30 if mode == "Canvas" else 25, top + 100 + 252 - (338 - 176) + 17)
            ui = sample(mode + "-save-value-edited")
            top = open_scenes(ui)
            click(825, top + 154)
            ui = sample(mode + "-save-authored-defaults")
            saved = json.loads(authored.read_text())
            assert Path(latest["scene.state"]["scenePath"]).name == authored.name
            assert saved["format"] == "skullbonez.scene.json"
            assert saved["simulation"]["world"]["fluidHeight"] == expected_height, saved["simulation"]
            (session / (mode + "-saved-defaults.json")).write_bytes(authored.read_bytes())
            ui = select_name(ui, "demo", mode + "-demo-loaded", existing=True)
            assert not latest["scene.state"]["sceneMode"] and latest["scene.state"]["scenePath"] == ""
            ui = select_name(ui, name, mode + "-filtered-scene-loaded", existing=True)
            assert Path(latest["scene.state"]["scenePath"]).name == authored.name
        print("PASS: native invalid name, scene creation, filtered selection, Demo, Reset, Reset defaults and Save defaults in both layouts")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()
        # This test owns exactly this previously absent authored scene.
        assert authored.parent == (REPO / "SkullbonezData/scenes").resolve()
        if authored.exists():
            (session / "final-authored-scene.json").write_bytes(authored.read_bytes())
            authored.unlink()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-scenes")
    run(parser.parse_args().session)
