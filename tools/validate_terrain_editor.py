#!/usr/bin/env python3
"""Exercise native sculpt input, UI exclusion, and unchanged height-map reuse."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import time

from PIL import Image
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path, executable: Path) -> None:
    session.mkdir(parents=True, exist_ok=True)
    scene = session / "flat.scene.json"
    scene.write_text(json.dumps({
        "format": "skullbonez.scene.json", "version": 1, "name": "Terrain brush validation",
        "simulation": {"physics": True, "text": True,
                       "world": {"gravity": -9.81, "fluidHeight": 0, "fluidDensity": 0}},
        "editor": {"editableScene": True}, "playback": {"frames": "unlimited", "fixedStep": True},
        "debug": {"waterHidden": True},
        "terrain": {"flatSlope": {"baseY": 30, "slopeX": 0, "slopeZ": 0}},
        "ui": {"visible": True, "minimized": False, "tab": "editor"},
        "cameras": [{"name": "main", "position": [500, 500, 850],
                     "view": [500, 30, 500], "up": [0, 1, 0]}], "objects": []
    }, indent=2), encoding="utf-8")
    assert launch(session, executable, scene, hidden=True, allocation_guard="gameplay",
                  layout_file=session / "layout.preferences") == 0
    connection = SkarnessConnection(session)

    def send(command: str, **arguments: object) -> dict:
        result = connection.wait(connection.send(command, arguments))
        assert result.get("status") == "applied", (command, result)
        return result

    def topics() -> dict:
        send("run.step_frames", count=3)
        latest = {}
        with (session / "runtime.skarness.ndjson").open(encoding="utf-8") as stream:
            for line in stream:
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    break  # The final row may still be flushing.
                if "topic" in event:
                    latest[event["topic"]] = event["payload"]
        return latest

    def observe(label: str) -> dict:
        result = topics()["ui.presentation"]
        (session / f"{label}.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        return result

    def capture(label: str) -> None:
        path = session / f"{label}.png"
        send("capture.screenshot", path=str(path))
        with Image.open(path) as source:
            source.save(session / f"{label}-view.png")

    def map_path() -> Path:
        reference = Path(json.loads(scene.read_text(encoding="utf-8"))["terrain"]["heightMap"])
        return reference if reference.is_absolute() else scene.parent / reference

    try:
        catalog = send("capabilities.get")
        (session / "capabilities.json").write_text(json.dumps(catalog, indent=2), encoding="utf-8")
        required = {"editor.set_terrain_brush", "scene.save", "input.pointer_drag", "input.pointer_wheel"}
        assert required <= set(catalog["commands"])
        send("state.subscribe", topics=[], detail="normal")
        baseline = observe("flat")
        assert baseline["terrainFlat"] and baseline["terrainMaximumHeight"] == 30
        send("scene.save")
        observe("flat-saved")
        assert "flatSlope" in json.loads(scene.read_text(encoding="utf-8"))["terrain"]
        assert not list(session.glob("*.heightmap"))
        send("editor.set_terrain_brush", enabled=True)
        ui = observe("brush-enabled")
        x, y, width, height = ui["viewport"]
        center = {"x": int(x + width / 2), "y": int(y + height / 2)}
        send("input.pointer_position", enabled=True, **center)
        send("input.pointer_wheel", wheelDelta=240, **center)
        sized = observe("resized")
        assert sized["terrainBrushRadius"] > ui["terrainBrushRadius"]
        assert sized["terrainBrushVisible"]
        send("input.pointer_drag", button="left", deltaX=0, deltaY=0, holdMilliseconds=450, **center)
        raised = observe("raised")
        assert raised["terrainRevision"] > 0 and raised["terrainMaximumHeight"] > 35
        assert raised["terrainMinimumHeight"] == 30
        capture("raised")
        send("input.pointer_drag", button="right", deltaX=0, deltaY=0, holdMilliseconds=1000, **center)
        lowered = observe("lowered")
        assert lowered["terrainRevision"] > raised["terrainRevision"]
        assert lowered["terrainMinimumHeight"] < 30
        assert lowered["cameraMode"] == raised["cameraMode"]
        capture("lowered")
        # A right hold and wheel over Tools must never reach terrain or camera look.
        over_ui = {"x": 50, "y": int(y + height + 80)}
        send("input.pointer_position", enabled=True, **over_ui)
        send("input.pointer_drag", button="right", deltaX=20, deltaY=10, holdMilliseconds=200, **over_ui)
        send("input.pointer_wheel", wheelDelta=120, **over_ui)
        blocked = observe("ui-blocked")
        assert blocked["terrainRevision"] == lowered["terrainRevision"]
        assert blocked["terrainBrushRadius"] == lowered["terrainBrushRadius"]
        assert not blocked["terrainBrushVisible"]
        send("scene.save")
        observe("saved")
        saved_map = map_path()
        saved_bytes = saved_map.read_bytes()
        saved_time = saved_map.stat().st_mtime_ns
        send("scene.save")
        observe("saved-again")
        assert map_path() == saved_map
        assert saved_map.stat().st_mtime_ns == saved_time
        assert len(list(session.glob("*.heightmap"))) == 1
        send("scene.reset")
        restored = observe("reloaded")
        for key in ("terrainMinimumHeight", "terrainMaximumHeight", "terrainCenterHeight"):
            assert restored[key] == lowered[key], (key, restored[key], lowered[key])
        assert restored["terrainRevision"] == 0
        send("scene.save")
        observe("import-reused")
        assert map_path() == saved_map
        assert saved_map.read_bytes() == saved_bytes and saved_map.stat().st_mtime_ns == saved_time
        assert len(list(session.glob("*.heightmap"))) == 1
        capture("editor-controls")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        shutdown = (session / "process.stdout.log").read_text(encoding="utf-8", errors="replace")
        if "[allocation-guard] PASS:" in shutdown or "[allocation-guard] FAIL:" in shutdown:
            break
        time.sleep(0.05)
    assert "[allocation-guard] PASS:" in shutdown, "native allocation guard did not pass"
    assert "gameplay_violations=0" in shutdown and "policy_violations=0" in shutdown
    (session / "result.json").write_text(json.dumps({
        "passed": True, "allocationGuard": "pass", "mapCount": 1,
        "heightMapSha256": hashlib.sha256(saved_bytes).hexdigest(),
        "raiseMaximum": raised["terrainMaximumHeight"], "lowerMinimum": lowered["terrainMinimumHeight"],
        "brushRadius": sized["terrainBrushRadius"], "uiBlocked": True, "unchangedImportReused": True
    }, indent=2), encoding="utf-8")
    print(f"PASS: sculpt, brush size, UI exclusion, save/reload, reuse and allocations ({session})")


def validate_creation(root: Path, executable: Path) -> None:
    """Use the visible scene menu and the native import-dialog response seam."""
    source = next(root.glob("*.heightmap"))
    original = source.read_bytes()
    modified = source.stat().st_mtime_ns
    for mode in ("flat", "import", "cancel"):
        session = root / f"create-{mode}"
        session.mkdir()
        name = f"terrain_validation_{mode}_{time.time_ns()}"
        created = (REPO / "SkullbonezData/scenes" / f"{name}.scene.json").resolve()
        assert not created.exists()
        fixture = json.loads((root / "flat.scene.json").read_text(encoding="utf-8"))
        fixture["terrain"] = {"flatSlope": {"baseY": 30, "slopeX": 0, "slopeZ": 0}}
        fixture["ui"] = {"visible": True, "minimized": False, "tab": "scene",
                         "sceneCombo": True, "sceneFilter": name}
        path = session / "input.scene.json"
        path.write_text(json.dumps(fixture), encoding="utf-8")
        assert launch(session, executable, path, hidden=True, allocation_guard="gameplay",
                      layout_file=session / "layout.preferences") == 0
        connection = SkarnessConnection(session)

        def send(command: str, **arguments: object) -> dict:
            result = connection.wait(connection.send(command, arguments))
            assert result.get("status") == "applied", (command, result)
            return result

        try:
            send("capabilities.get")
            send("state.subscribe", topics=[], detail="normal")
            send("run.step_frames", count=3)
            send("capture.screenshot", path=str(session / "menu.png"))
            if mode != "flat":
                send("input.file_dialog_response", purpose="terrain.import",
                     accepted=mode == "import", path=str(source))
            # The fixture opens the native Scene combo with only its two creation rows.
            y = 779 if mode == "flat" else 799
            send("input.pointer_position", enabled=True, x=200, y=y)
            send("input.pointer_drag", button="left", x=200, y=y,
                 deltaX=0, deltaY=0, holdMilliseconds=60)
            send("run.step_frames", count=5)
            send("capture.screenshot", path=str(session / "result.png"))
            assert created.exists() == (mode != "cancel")
            if created.exists():
                saved = json.loads(created.read_text(encoding="utf-8"))
                if mode == "flat":
                    assert saved["terrain"]["flatSlope"] == {"baseY": 30, "slopeX": 0, "slopeZ": 0}
                else:
                    assert Path(saved["terrain"]["heightMap"]).resolve() == source
                (session / "created.scene.json").write_bytes(created.read_bytes())
            assert source.read_bytes() == original and source.stat().st_mtime_ns == modified
            assert not list(created.parent.glob(f"{name}*.heightmap"))
            events = [json.loads(line) for line in (session / "runtime.skarness.ndjson").read_text(encoding="utf-8").splitlines()]
            latest = next(event for event in reversed(events) if event.get("topic") == "ui.presentation")
            assert latest["sceneGeneration"] == (1 if mode == "cancel" else 2)
            if mode == "import":
                sculpted = json.loads((root / "lowered.json").read_text())
                assert latest["payload"]["terrainMinimumHeight"] == sculpted["terrainMinimumHeight"]
                assert latest["payload"]["terrainMaximumHeight"] == sculpted["terrainMaximumHeight"]
            else:
                assert latest["payload"]["terrainFlat"]
        finally:
            send("session.stop")
            connection.close()
            # Only the exact file created by this test is removed; evidence remains in session.
            assert created.parent == (REPO / "SkullbonezData/scenes").resolve()
            if created.exists():
                created.unlink()
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            output = (session / "process.stdout.log").read_text(encoding="utf-8", errors="replace")
            if "[allocation-guard] PASS:" in output or "[allocation-guard] FAIL:" in output:
                break
            time.sleep(0.05)
        assert "[allocation-guard] PASS:" in output, mode
        for label in ("menu", "result"):
            with Image.open(session / f"{label}.png") as captured:
                captured.save(session / f"{label}-view.png")
        (session / "validation.json").write_text(json.dumps({"passed": True, "mode": mode,
            "allocationGuard": "pass", "sourceUnchanged": True}), encoding="utf-8")
    print("PASS: flat creation, existing-map import and cancellation through native Scene menu")


def validate_largest_import(root: Path, executable: Path) -> None:
    """Exercise both cold and brush uploads at the admitted map size limit."""
    session = root / "largest-import"
    session.mkdir()
    height_map = session / "largest.heightmap"
    height_map.write_text("SKULLBONEZ_HEIGHTMAP 1 257 4 8\n" + "30 " * (257 * 257), encoding="ascii")
    fixture = json.loads((root / "flat.scene.json").read_text(encoding="utf-8"))
    fixture["terrain"] = {"heightMap": str(height_map)}
    scene = session / "input.scene.json"
    scene.write_text(json.dumps(fixture), encoding="utf-8")
    assert launch(session, executable, scene, hidden=True, allocation_guard="gameplay",
                  layout_file=session / "layout.preferences") == 0
    connection = SkarnessConnection(session)

    def send(command: str, **arguments: object) -> dict:
        reply = connection.wait(connection.send(command, arguments))
        assert reply.get("status") == "applied", reply
        return reply

    try:
        assert "editor.set_terrain_brush" in send("capabilities.get")["commands"]
        send("state.subscribe", topics=[], detail="normal")
        send("editor.set_terrain_brush", enabled=True)
        send("run.step_frames", count=3)
        events = [json.loads(line) for line in (session / "runtime.skarness.ndjson").read_text().splitlines()]
        ui = next(row["payload"] for row in reversed(events) if row.get("topic") == "ui.presentation")
        x, y, width, height = ui["viewport"]
        send("input.pointer_drag", button="left", x=int(x + width / 2), y=int(y + height / 2),
             deltaX=0, deltaY=0, holdMilliseconds=400)
        send("run.step_frames", count=3)
        events = [json.loads(line) for line in (session / "runtime.skarness.ndjson").read_text().splitlines()]
        ui = next(row["payload"] for row in reversed(events) if row.get("topic") == "ui.presentation")
        assert ui["terrainRevision"] > 0 and ui["terrainMaximumHeight"] > 30
        (session / "result.json").write_text(json.dumps(ui, indent=2))
        send("capture.screenshot", path=str(session / "sculpted.png"))
    finally:
        send("session.stop")
        connection.close()
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        output = (session / "process.stdout.log").read_text(encoding="utf-8", errors="replace")
        if "[allocation-guard] PASS:" in output:
            break
        time.sleep(0.05)
    assert "[allocation-guard] PASS:" in output
    with Image.open(session / "sculpted.png") as captured:
        captured.save(session / "sculpted-view.png")
    print("PASS: 257 by 257 imported map loads and sculpts within the fixed GPU arena")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/terrain-editor")
    parser.add_argument("--exe", type=Path, default=REPO / "Automation/SKULLBONEZ_CORE.exe")
    args = parser.parse_args()
    run(args.session.resolve(), args.exe.resolve())
    validate_creation(args.session.resolve(), args.exe.resolve())
    validate_largest_import(args.session.resolve(), args.exe.resolve())
