"""Exercise the native shared header and scene viewport through Skarness.

This focused check covers the shell foundation. The complete control inventory
and workspace migration have additional acceptance checks in the owning plan.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import time

from PIL import Image

from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def assert_shell_image(path: Path, editor: bool = False) -> None:
    with Image.open(path) as image:
        # Neutral opaque chrome is a rendering assertion, independent of the
        # presentation state report. Hidden legacy tools must not hide the shell.
        header = image.convert("RGB").getpixel((image.width // 2, 2))
        assert all(abs(actual - expected) <= 2 for actual, expected in zip(header, (19, 20, 23))), header
        if editor:
            dock = image.convert("RGB").getpixel((2, 100))
            assert all(abs(actual - expected) <= 2 for actual, expected in zip(dock, (19, 20, 23))), dock


def run(session: Path, executable: Path, scene: Path) -> None:
    if launch(session, executable, scene, hidden=True) != 0:
        raise RuntimeError("unified UI fixture could not launch")
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
        # Preserve actual owner snapshots, including the camera and selection,
        # so an acknowledgement alone cannot satisfy a layout assertion.
        (session / f"{label}.json").write_text(json.dumps(latest, indent=2), encoding="utf-8")
        return latest["ui.presentation"]

    def click(x: int, y: int) -> None:
        send("input.pointer_drag", button="left", x=x, y=y, deltaX=0, deltaY=0)

    def inspect_ray(label: str, ui: dict) -> None:
        x, y, width, height = ui["viewport"]
        send("input.pointer_position", x=x + width // 2, y=y + height // 2, enabled=True)
        centered = sample(label + "-center")
        assert centered["pointerHasWorldRay"], centered
        camera = latest["camera.state"]
        forward = [b - a for a, b in zip(camera["primaryEye"], camera["primaryView"])]
        length = math.sqrt(sum(value * value for value in forward))
        dot = sum(a * b / length for a, b in zip(forward, centered["pointerRayDirection"]))
        assert dot > 0.999, f"viewport centre ray diverged from the camera: {dot}"
        scale_x, scale_y = centered["projectionScale"]
        assert math.isclose(scale_y / scale_x, width / height, rel_tol=1e-5), centered

    try:
        capabilities = send("capabilities.get")
        assert "input.pointer_position" in capabilities["commands"]
        send("state.subscribe", topics=["*"], detail="normal")
        canvas = sample("canvas")
        assert canvas["layout"] == "Canvas" and not canvas["editorMode"], canvas
        width, height = canvas["window"]
        assert canvas["viewport"] == [0, 0, width, height], canvas
        scene_identity = dict(latest["scene.objects"])
        camera_identity = latest["camera.state"]["selectedCameraHash"]
        selection = dict(latest["selection.state"])
        inspect_ray("canvas", canvas)
        send("capture.screenshot", path=str((session / "canvas.png").resolve()))
        assert_shell_image(session / "canvas.png")
        send("input.pointer_position", x=width - 110, y=20, enabled=True)
        hover_until = time.monotonic() + 0.6
        # Keep consuming the bounded state stream during a stationary hover;
        # an idle subscriber is disconnected instead of growing host buffers.
        while time.monotonic() < hover_until:
            connection.read_event()
        send("capture.screenshot", path=str((session / "header-tooltip.png").resolve()))

        click(width - 110, 20)
        editor = sample("editor")
        assert editor["layout"] == "Editor" and not editor["editorMode"], editor
        assert editor["viewport"][0] > 0 and editor["viewport"][1] > 0, editor
        assert editor["viewport"][2] < width and editor["viewport"][3] < height, editor
        assert editor["markerHistoryVisible"] and editor["memoryWaterlineVisible"], editor
        assert latest["scene.objects"] == scene_identity, "layout replaced or changed the scene"
        assert latest["camera.state"]["selectedCameraHash"] == camera_identity, "layout selected another camera"
        assert latest["selection.state"] == selection, "layout replaced selection"
        inspect_ray("editor", editor)
        send("input.pointer_position", x=1, y=100, enabled=True)
        outside = sample("outside-viewport")
        assert not outside["pointerHasWorldRay"], "dock pointer produced a world ray"
        assert outside["pointerRayDirection"] == [0.0, 0.0, 0.0], outside
        send("capture.screenshot", path=str((session / "editor.png").resolve()))
        assert_shell_image(session / "editor.png", editor=True)

        click(width - 190, 20)
        tools = sample("editor-scenes")
        assert tools["toolsVisible"] and tools["activeTool"] == 1, tools
        assert tools["viewport"][3] < editor["viewport"][3], "Tools did not shrink the viewport"
        inspect_ray("editor-tools", tools)
        send("capture.screenshot", path=str((session / "editor-scenes.png").resolve()))
        # Select every existing tab through its actual visible tab bar. This
        # checks access; each tab's commands have separate parity checks.
        drawer_y = tools["viewport"][1] + tools["viewport"][3] + 28
        click(200, drawer_y + 154)
        sample("scene-browser-popup")
        send("capture.screenshot", path=str((session / "scene-browser-popup.png").resolve()))
        with Image.open(session / "scene-browser-popup.png") as popup:
            # The menu crosses the shared transport here. Its opaque fill must
            # cover that strip, not expose a track drawn by a later presenter.
            pixel = popup.convert("RGB").getpixel((400, int(drawer_y - 14)))
            assert all(abs(a - b) <= 2 for a, b in zip(pixel, (33, 36, 41))), pixel
        click(200, drawer_y + 154)
        sample("scene-browser-popup-closed")
        assert latest["scene.objects"] == scene_identity, "cancelling the browser changed the scene"
        for index, name in enumerate(("profiler", "scene", "editor", "physics", "options", "render",
                                      "targets", "keys", "sky", "cinematic", "memory")):
            click(int(14 + (width - 28) * (index + 0.5) / 11), drawer_y + 66)
            tab = sample("editor-tools-" + name)
            assert tab["activeTool"] == index, (name, tab)
            send("capture.screenshot", path=str((session / ("tools-" + name + ".png")).resolve()))
        send("input.pointer_drag", button="left", x=width // 2, y=drawer_y + 2, deltaX=0, deltaY=-60, moveClient=True)
        resized = sample("tools-resized")
        assert resized["viewport"][3] < tools["viewport"][3], resized
        assert resized["activeTool"] == 10 and not resized["editorMode"], resized
        click(width - 30, 20)
        closed = sample("editor-tools-closed")
        assert not closed["toolsVisible"] and closed["viewport"] == editor["viewport"], closed
        assert closed["markerSamples"] >= tools["markerSamples"] and closed["memorySamples"] >= tools["memorySamples"], closed
        assert closed["markerSelectionHash"] == tools["markerSelectionHash"], closed
        # Pinned diagnostic headers expose the two detailed Tools tabs.
        click(width // 2 - 44, height - 128)
        profiler_details = sample("profiler-details-route")
        assert profiler_details["activeTool"] == 0 and profiler_details["toolsVisible"], profiler_details
        click(width - 44, height - 128)
        memory_details = sample("memory-details-route")
        assert memory_details["activeTool"] == 10 and memory_details["toolsVisible"], memory_details
        click(width - 30, 20)
        click(width - 30, 20)
        reopened = sample("editor-tools-reopened")
        assert reopened["activeTool"] == 10 and reopened["viewport"] == resized["viewport"], reopened
        click(width - 110, 20)
        retained = sample("canvas-tools-retained")
        assert retained["layout"] == "Canvas" and retained["activeTool"] == 10, retained
        assert retained["toolsVisible"] and retained["viewport"][3] < height, retained
        click(width - 30, 20)
        restored = sample("canvas-restored")
        assert restored["layout"] == "Canvas" and restored["viewport"] == canvas["viewport"], restored
        assert restored["markerHistoryVisible"] == canvas["markerHistoryVisible"], restored
        assert restored["memoryWaterlineVisible"] == canvas["memoryWaterlineVisible"], restored
        assert latest["scene.objects"] == scene_identity
        click(width - 190, 20)
        assert sample("canvas-scenes")["activeTool"] == 1
        print("PASS: native layouts, Scenes and all Tools tabs, bounded drawer resize/retention, camera/selection identity, projection and offset world rays")
    finally:
        try:
            send("input.pointer_position", x=0, y=0, enabled=False)
            send("session.stop")
        except (OSError, RuntimeError):
            # A disconnected subscriber does not own the app's lifetime.
            # Reconnect to this exact session to finish orderly teardown.
            connection.close()
            connection = SkarnessConnection(session)
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-ui")
    parser.add_argument("--exe", type=Path, default=REPO / "Automation/SKULLBONEZ_CORE.exe")
    parser.add_argument(
        "--scene", type=Path,
        default=REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json",
    )
    args = parser.parse_args()
    run(args.session, args.exe, args.scene.resolve())
