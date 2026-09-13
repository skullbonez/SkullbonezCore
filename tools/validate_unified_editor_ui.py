"""Drive the existing editor commands and entire catalog through the shared dock."""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]
# GameUILayout places quick objects below the two velocity-authoring rows.
PALETTE_TOP = 492


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
        deadline = time.monotonic() + 0.25
        while time.monotonic() < deadline:
            send("run.step_frames", count=3)
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
        send("input.pointer_position", enabled=True, x=int(x), y=int(y))
        send("run.step_frames", count=2)
        time.sleep(0.22)
        send("run.step_frames", count=2)
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0)

    def middle(bounds: list[float]) -> None:
        x, y, width, height = bounds
        assert width > 0 and height > 0
        click(x + width / 2, y + height / 2)

    def capture(label: str) -> None:
        send("capture.screenshot", path=str((session / f"{label}.png").resolve()))

    try:
        capabilities = send("capabilities.get")
        assert {"input.pointer_drag", "input.pointer_wheel"} <= set(capabilities["commands"])
        send("state.subscribe", topics=[], detail="normal")
        ui = sample("canvas")
        scene_identity = dict(latest["scene.objects"])
        camera = latest["camera.state"]["selectedCameraHash"]
        width, _ = ui["window"]
        middle(ui["headerLayoutBounds"])
        ui = sample("editor-layout")
        assert ui["layout"] == "Editor" and not ui["editorMode"]
        middle(ui["editorTabBounds"])
        ui = sample("editor-panel-open")
        middle(ui["causeTabBounds"])
        ui = sample("cause-panel-open")
        assert ui["editorControlsBounds"][2] > 0 and ui["replayControlsBounds"][2] == 0
        assert latest["camera.state"]["selectedCameraHash"] == camera
        capture("editor-layout")

        for edge, delta in (("leftResizeBounds", 70), ("rightResizeBounds", -70)):
            x, y, _, _ = ui[edge]
            before = list(ui["viewport"])
            send("input.pointer_drag", button="left", x=int(x + 2), y=int(y + 100), deltaX=delta, deltaY=0, moveClient=True)
            ui = sample(edge)
            assert ui["viewport"][2] < before[2]
            assert latest["camera.state"]["selectedCameraHash"] == camera
        before = list(ui["viewport"])
        middle(ui["rightFoldBounds"])
        ui = sample("right-folded")
        assert ui["causeControlsBounds"][2] == 0 and ui["viewport"][2] > before[2]
        middle(ui["rightFoldBounds"])
        ui = sample("right-unfolded")
        assert ui["viewport"] == before

        for y_offset, key in ((54, "editorMode"), (208, "editorPlacement"), (242, "editorStaticObject"), (382, "editorTerrainAlign")):
            x, y, _, _ = ui["editorControlsBounds"]
            if key == "editorPlacement":
                click(x + 25, y + 54)
                ui = sample("editor-enabled-for-placement")
                assert ui["editorMode"]
            before_value = ui[key]
            click(x + 25, y + y_offset)
            ui = sample(key + "-toggle")
            assert ui[key] != before_value, (key, ui)
            click(x + 25, y + y_offset)
            ui = sample(key + "-restore")
            assert ui[key] == before_value

        # Selecting a catalog item intentionally enters placement through the
        # same editor command as Tools. Merely opening a popup does not place it.
        total = ui["editorObjectOptions"]
        for index in range(total):
            x, y, _, _ = ui["editorControlsBounds"]
            click(x + 110, y + 286)
            ui = sample(f"object-{index}-popup")
            assert ui["editorPopupOpen"]
            while not ui["editorPopupFirstOption"] <= index < ui["editorPopupFirstOption"] + ui["editorPopupVisibleOptions"]:
                px, py, _, _ = ui["editorPopupBounds"]
                direction = -120 if index >= ui["editorPopupFirstOption"] else 120
                send("input.pointer_wheel", x=int(px + 10), y=int(py + 10), wheelDelta=direction)
                ui = sample(f"object-{index}-scroll")
            px, py, _, ph = ui["editorPopupBounds"]
            row_height = ph / ui["editorPopupVisibleOptions"]
            click(px + 20, py + (index - ui["editorPopupFirstOption"] + 0.5) * row_height)
            ui = sample(f"object-{index}-selected")
            assert not ui["editorPopupOpen"]
            assert ui["editorObjectType"] == index, (index, ui)
            assert ui["editorPlacement"]
            assert latest["scene.objects"] == scene_identity
        capture("catalog-final-selection")
        # Quick-object taps and hold variants use the existing editor commands.
        x, y, pane_width, _ = ui["editorControlsBounds"]
        columns = max(1, int((pane_width + 4) / 36))
        entries = list(range(13)) + list(range(30, 37)) + [16, 22, 25, 28]
        for entry, object_type in enumerate(entries):
            bx, by = x + (entry % columns) * 36 + 16, y + PALETTE_TOP + (entry // columns) * 36 + 16
            click(bx, by)
            ui = sample(f"quick-object-{entry}")
            assert ui["editorObjectType"] == object_type, (entry, object_type, ui["editorObjectType"])
            assert ui["editorPlacement"]
            assert latest["scene.objects"] == scene_identity
        for entry, variants in ((20, [15, 16, 17]), (21, [21, 22, 23]), (22, [24, 25, 26]), (23, [28, 29, 37, 38])):
            bx, by = x + (entry % columns) * 36 + 16, y + PALETTE_TOP + (entry // columns) * 36 + 16
            for option, object_type in enumerate(variants):
                send("input.pointer_drag", button="left", x=int(bx), y=int(by), deltaX=44 + option * 35,
                     deltaY=0, moveClient=True, holdMilliseconds=500)
                ui = sample(f"hold-{entry}-{option}")
                assert ui["editorObjectType"] == object_type, (entry, option, object_type, ui["editorObjectType"])
                assert ui["editorStaticObject"] == (entry in (20, 22))
                assert latest["scene.objects"] == scene_identity
        capture("quick-object-grid")
        editing = (ui["editorMode"], ui["editorPlacement"], ui["editorObjectType"])
        middle(ui["headerLayoutBounds"])
        ui = sample("canvas-retained-editor")
        assert (ui["editorMode"], ui["editorPlacement"], ui["editorObjectType"]) == editing
        # Canvas uses the same catalog and palette inside the Tools drawer.
        middle(ui["replayDetailsBounds"])
        ui = sample("canvas-tools")
        top = ui["drawerBounds"][1]
        click(14 + (width - 28) * 2.5 / 11, top + 66)
        ui = sample("canvas-tools-editor")
        assert ui["activeTool"] == 2
        # The brush controls put the catalog below the initial drawer viewport.
        x, y, _, height = ui["toolsContentBounds"]
        desired = max(0, 286 - height / 2)
        send("input.pointer_wheel", x=int(x + 60), y=int(y + height / 2),
             wheelDelta=-round(desired * 120 / 42))
        ui = sample("drawer-catalog-revealed")
        for index in range(total):
            x, y, _, _ = ui["toolsContentBounds"]
            click(x + 110, y + 286 - ui["toolsScroll"])
            ui = sample(f"canvas-object-{index}-popup")
            assert ui["editorPopupOpen"]
            while not ui["editorPopupFirstOption"] <= index < ui["editorPopupFirstOption"] + ui["editorPopupVisibleOptions"]:
                px, py, _, _ = ui["editorPopupBounds"]
                direction = -120 if index >= ui["editorPopupFirstOption"] else 120
                send("input.pointer_wheel", x=int(px + 10), y=int(py + 10), wheelDelta=direction)
                ui = sample(f"canvas-object-{index}-scroll")
            px, py, _, ph = ui["editorPopupBounds"]
            row_height = ph / ui["editorPopupVisibleOptions"]
            click(px + 20, py + (index - ui["editorPopupFirstOption"] + .5) * row_height)
            ui = sample(f"canvas-object-{index}-selected")
            assert ui["editorObjectType"] == index and ui["editorPlacement"]
            assert latest["scene.objects"] == scene_identity
        x, y, pane_width, pane_height = ui["toolsContentBounds"]
        send("input.pointer_wheel", x=int(x + 60), y=int(y + pane_height / 2),
             wheelDelta=-round((PALETTE_TOP - pane_height / 2) * 120 / 42))
        ui = sample("canvas-palette-scrolled")
        columns = max(1, int((pane_width + 4) / 36))
        palette_y = y + PALETTE_TOP - ui["toolsScroll"]
        for entry, object_type in enumerate(entries):
            bx, by = x + (entry % columns) * 36 + 16, palette_y + (entry // columns) * 36 + 16
            click(bx, by)
            ui = sample(f"canvas-quick-object-{entry}")
            assert ui["editorObjectType"] == object_type
            assert latest["scene.objects"] == scene_identity
        for entry, variants in ((20, [15, 16, 17]), (21, [21, 22, 23]), (22, [24, 25, 26]), (23, [28, 29, 37, 38])):
            bx, by = x + (entry % columns) * 36 + 16, palette_y + (entry // columns) * 36 + 16
            for option, object_type in enumerate(variants):
                send("input.pointer_drag", button="left", x=int(bx), y=int(by), deltaX=44 + option * 35,
                     deltaY=0, moveClient=True, holdMilliseconds=500)
                ui = sample(f"canvas-hold-{entry}-{option}")
                assert ui["editorObjectType"] == object_type
                assert ui["editorStaticObject"] == (entry in (20, 22))
                assert latest["scene.objects"] == scene_identity
        capture("canvas-quick-objects")
        editing = (ui["editorMode"], ui["editorPlacement"], ui["editorObjectType"])
        middle(ui["replayDetailsBounds"])
        ui = sample("editor-retained-editor")
        assert ui["layout"] == "Editor" and not ui["toolsVisible"]
        assert (ui["editorMode"], ui["editorPlacement"], ui["editorObjectType"]) == editing
        capture("editor-retained-editor")
        print(f"PASS: native Editor controls, all {total} catalog choices, 24 quick-object buttons and 13 hold variants in the dock and Tools drawer, dock resizing/folding and retained editing state")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-editor-ui")
    run(parser.parse_args().session)
