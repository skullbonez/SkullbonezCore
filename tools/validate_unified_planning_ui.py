"""Exercise viewport-bounded trip planning and transfer cells through native input."""
from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path

from PIL import Image
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(session: Path, commit_fixture: bool = False) -> None:
    session = session.resolve()
    scene = REPO / "SkullbonezData/scenes/solar_system.scene.json"
    if commit_fixture:
        session.mkdir(parents=True, exist_ok=True)
        data = json.loads(scene.read_text())
        target = next(obj for obj in data["objects"] if obj["name"] == "mars")
        ship = next(obj for obj in data["objects"] if obj["name"] == "ship")
        ship["position"] = [target["position"][0] + 3.1, *target["position"][1:]]
        ship["velocity"] = target["velocity"][:]
        scene = session / "near-intercept.scene.json"
        scene.write_text(json.dumps(data, indent=2))
    assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe", scene, hidden=True) == 0
    connection = SkarnessConnection(session)
    latest: dict[str, dict] = {}
    offset = 0

    def send(command: str, **args: object) -> dict:
        result = connection.wait(connection.send(command, args))
        assert result.get("status") == "applied", result
        return result

    def sample(label: str, frames: int = 3) -> dict:
        nonlocal offset
        send("run.step_frames", count=frames)
        with (session / "runtime.skarness.ndjson").open(encoding="utf-8") as stream:
            stream.seek(offset)
            for line in stream:
                event = json.loads(line)
                if "topic" in event:
                    latest[event["topic"]] = event["payload"]
            offset = stream.tell()
        # Keep the evidence useful without duplicating full Physics/future arrays.
        selected = {key: value for key, value in latest.items()
                    if key in ("ui.presentation", "replay.planning", "camera", "selection", "replay.selection", "replay.prediction.controls", "replay.cause")}
        (session / (label + ".json")).write_text(json.dumps(selected, indent=2), encoding="utf-8")
        assert not latest["replay.planning"]["overlayOverflow"], latest["replay.planning"]
        return latest["ui.presentation"]

    def planning() -> dict:
        return latest["replay.planning"]

    def click(x: float, y: float, hold: int = 0) -> None:
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0,
             holdMilliseconds=hold)

    def key(code: int) -> None:
        send("input.set_key", key=code, down=True)
        send("run.step_frames", count=2)
        send("input.set_key", key=code, down=False)

    def content(ui: dict) -> list[float]:
        vx, vy, vw, vh = ui["viewport"]
        header = min(42, ui["window"][1] * .15)
        return [vx, header, vw, max(0, ui["transportBounds"][1] - header)]

    def rectangles(ui: dict) -> tuple[list[float], list[float], list[float], int]:
        vx, vy, vw, vh = content(ui)
        p = planning()
        y = vy + 8 - p["surfaceScroll"]
        if p["intercept"]["valid"]:
            y += 36
        tw = min(500, max(0, vw - 16))
        columns = max(1, min(5, int((tw - 16) / 86)))
        th = 64 + math.ceil(5 / columns) * 30
        trip = [vx + (vw - tw) / 2, y, tw, th]
        if p["trip"]["visible"] and p["trip"]["available"]:
            y += th + 8
        pw = min(720, max(0, vw - 16))
        pork = [vx + (vw - pw) / 2, y, pw, 420]
        margin = min(40, pw * .15)
        grid = [pork[0] + margin, y + 52, pw - 2 * margin, 288]
        return trip, pork, grid, columns

    def trip_button(ui: dict, index: int) -> tuple[float, float]:
        trip, _, _, columns = rectangles(ui)
        x, y, w, _ = trip
        column_width = (w - 16) / columns
        return x + 8 + index % columns * column_width + (column_width - 4) / 2, y + 65 + index // columns * 30

    def capture(label: str) -> None:
        path = session / (label + ".png")
        send("capture.screenshot", path=str(path))
        # Normalize the native PNG for image viewers that reject its encoding.
        with Image.open(path) as img:
            img.save(session / (label + "-view.png"))

    def scroll(ui: dict, delta: int) -> dict:
        vx, vy, vw, vh = content(ui)
        send("input.pointer_wheel", x=int(vx + vw / 2), y=int(vy + vh / 2), wheelDelta=delta)
        return sample("scroll")

    try:
        assert {"input.pointer_drag", "input.set_key", "window.resize"} <= set(send("capabilities.get")["commands"])
        send("state.subscribe", topics=[], detail="normal")
        send("prediction.select_target", name="ship")
        send("replay.set_intercept_target", name="mars")
        send("replay.set_prediction_horizon", seconds=20)
        send("replay.set_prediction_enabled", enabled=True)
        key(ord("J"))
        key(ord("I"))
        ui = sample("initial", 120)
        assert planning()["trip"]["visible"] and planning()["trip"]["available"], planning()
        assert planning()["porkchop"]["complete"], planning()
        for attempt in range(80):
            if latest["replay.prediction.controls"]["complete"]:
                break
            ui = sample("initial-prediction", 30)
        assert latest["replay.prediction.controls"]["complete"]
        ship_id, target_id = planning()["intercept"]["shipId"], planning()["intercept"]["targetId"]
        for layout in ("Canvas", "Editor"):
            if ui["layout"] != layout:
                click(ui["window"][0] - 110, 20)
                ui = sample(layout)
            send("replay.set_trip_time_of_flight", seconds=15.9)
            ui = sample(layout + "-tof")
            before = planning()["trip"]["timeOfFlightSeconds"]
            click(*trip_button(ui, 0), hold=180)
            ui = sample(layout + "-decrease")
            assert abs(planning()["trip"]["timeOfFlightSeconds"] - (before - .5)) < .001
            click(*trip_button(ui, 1), hold=180)
            ui = sample(layout + "-increase")
            assert abs(planning()["trip"]["timeOfFlightSeconds"] - before) < .001
            click(*trip_button(ui, 3))
            ui = sample(layout + "-disabled-commit")
            assert planning()["trip"]["state"] == 0
            for attempt in range(80):
                if latest["replay.prediction.controls"]["complete"]:
                    break
                ui = sample(layout + "-baseline", 30)
            assert latest["replay.prediction.controls"]["complete"]
            capture(layout + "-before-plan")
            click(*trip_button(ui, 2))
            ui = sample(layout + "-plan-click")
            capture(layout + "-after-plan")
            assert planning()["trip"]["state"] != 0, planning()
            for attempt in range(40):
                ui = sample(layout + "-planning", 60)
                if planning()["trip"]["state"] in (4, 5):
                    break
            assert planning()["trip"]["state"] in (4, 5), planning()["trip"]
            assert planning()["trip"]["shipId"] == ship_id and planning()["trip"]["targetId"] == target_id
            capture(layout + "-result")
            if commit_fixture:
                assert planning()["trip"]["state"] == 4, planning()
                click(*trip_button(ui, 3), hold=180)
                ui = sample(layout + "-commit")
                assert planning()["trip"]["state"] == 0 and planning()["trip"]["ghostCount"] == 0
                continue
            click(*trip_button(ui, 4))
            ui = sample(layout + "-cancel")
            assert planning()["trip"]["state"] == 0
            # Native hover and selection must publish the precise row-major cell.
            _, _, grid, _ = rectangles(ui)
            gx, gy, gw, gh = grid
            column, row = 1, 36
            px, py = int(gx + (column + .5) * gw / 64), int(gy + (row + .5) * gh / 48)
            actual_cell = int((py - gy) / gh * 48) * 64 + int((px - gx) / gw * 64)
            send("input.pointer_position", x=px, y=py, enabled=True)
            ui = sample(layout + "-hover")
            assert planning()["porkchop"]["hoveredCell"] == actual_cell
            click(px, py)
            ui = sample(layout + "-cell")
            assert planning()["porkchop"]["selectedCell"] == actual_cell, planning()
            assert abs(planning()["trip"]["timeOfFlightSeconds"] - (2 + row / 47 * 18)) < .001
            send("input.pointer_position", x=0, y=0, enabled=False)
            for width, height in ((900, 640), (640, 480), (480, 360), (320, 240)):
                send("window.resize", width=width, height=height)
                ui = sample(f"{layout}-{width}")
                ui = scroll(ui, -12000)
                assert planning()["surfaceScroll"] >= 0, (layout, width, ui["viewport"], planning())
                _, pork, _, _ = rectangles(ui)
                vx, vy, vw, vh = content(ui)
                assert pork[0] >= vx and pork[0] + pork[2] <= vx + vw
                assert pork[1] + pork[3] <= vy + vh + .01
                capture(f"{layout}-{width}-scrolled")
                ui = scroll(ui, 12000)
                assert planning()["surfaceScroll"] == 0
                trip, _, _, _ = rectangles(ui)
                # Reach every control by scrolling that row into the clipped viewport.
                for index in range(5):
                    px, py = trip_button(ui, index)
                    desired = max(0, py - vy - vh / 2)
                    if desired > 0:
                        ui = scroll(ui, -int(math.ceil(desired / 36) * 120))
                    px, py = trip_button(ui, index)
                    assert vx <= px < vx + vw and vy <= py < vy + vh, (layout, width, index, px, py)
                    ui = scroll(ui, 12000)
                capture(f"{layout}-{width}-top")
                assert planning()["intercept"]["shipId"] == ship_id and planning()["intercept"]["targetId"] == target_id
                if width == 320 and layout == "Editor":
                    cx, cy, cw, ch = ui["causeControlsBounds"]
                    send("input.pointer_wheel", x=int(cx+2), y=int(cy+ch/2), wheelDelta=-12000)
                    ui = sample("short-causes-bottom")
                    assert latest["replay.cause"]["shellScroll"] > 0
                    before_blue = latest["replay.cause"]["blueOutlinesVisible"]
                    click(cx+20, cy+ch-66)
                    ui = sample("short-causes-blue")
                    assert latest["replay.cause"]["blueOutlinesVisible"] != before_blue
                    capture("short-causes-bottom")
                    send("input.pointer_wheel", x=int(cx+2), y=int(cy+ch/2), wheelDelta=12000)
                    ui = sample("short-causes-top")
                    assert latest["replay.cause"]["shellScroll"] == 0

            send("window.resize", width=1784, height=961)
            ui = sample(layout + "-restored")
        key(ord("I"))
        key(ord("J"))
        ui = sample("hidden")
        assert not planning()["porkchop"]["visible"] and not planning()["trip"]["visible"]
        if commit_fixture:
            print("PASS: native converged Plan and held Commit, identity and cleared candidates in both layouts")
        else:
            print("PASS: trip decrease/increase, disabled commit, terminal result and cancel, exact grid hover/selection, identity and four viewport sizes in both layouts")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-planning-ui")
    parser.add_argument("--commit-fixture", action="store_true")
    args = parser.parse_args()
    run(args.session, args.commit_fixture)
