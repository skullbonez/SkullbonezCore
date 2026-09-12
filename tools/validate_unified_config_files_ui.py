"""Click native render save actions against an isolated engine.cfg copy."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path
from skarness import SkarnessConnection, launch
from validate_unified_render_catalog_ui import catalogs

REPO = Path(__file__).resolve().parents[1]


def run(session: Path) -> None:
    session = session.resolve()
    session.mkdir(parents=True, exist_ok=True)
    original = REPO / "SkullbonezData/engine.cfg"
    original_hash = hashlib.sha256(original.read_bytes()).hexdigest()
    destination = session / "engine.cfg"
    marker = "# Native UI save test: preserve this comment."
    destination.write_bytes(original.read_bytes() + ("\n" + marker + "\n").encode())
    assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe",
                  REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json",
                  hidden=True, render_defaults_file=destination) == 0
    connection = SkarnessConnection(session)
    rows = catalogs()
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
        send("input.pointer_drag", button="left", x=int(x), y=int(y), deltaX=0, deltaY=0, holdMilliseconds=70)

    def scroll(ui: dict, value: float) -> dict:
        x, y, _, h = ui["toolsContentBounds"]
        delta = int(round((ui["toolsScroll"] - value) * 120 / 42))
        if delta:
            send("input.pointer_wheel", x=int(x + 80), y=int(y + h / 2), wheelDelta=delta)
        return sample("scrolled")

    try:
        assert "input.pointer_drag" in send("capabilities.get")["commands"]
        send("state.subscribe", topics=[], detail="normal")
        ui = sample("initial")
        for layout in ("Canvas", "Editor"):
            if ui["layout"] != layout:
                click(ui["window"][0] - 110, 20)
                ui = sample(layout)
            if not ui["toolsVisible"]:
                click(ui["window"][0] - 38, 20)
                ui = sample("tools")
            for action, tab, param, key in (
                ("Save CFG", "Render", rows["Render"][0]["param"], "ordinary_sun_intensity"),
                ("Save Paths", "Render", "TrajectoryFutureWidth", "replay_trajectory_future_width"),
                ("Save Sky", "Sky", "SunAzimuth", "cinematic_sun_screen_x"),
            ):
                index = 5 if tab == "Render" else 8
                top = ui["viewport"][1] + ui["viewport"][3] + (28 if layout == "Editor" else 0)
                click(14 + (ui["window"][0] - 28) * (index + .5) / 11, top + 66)
                ui = sample("tab")
                assert ui["activeTool"] == index
                row = next(row for row in rows[tab] if row["param"] == param)
                ui = scroll(ui, max(0, row["rowY"] - ui["toolsContentBounds"][3] / 2))
                x, y, w, _ = ui["toolsContentBounds"]
                fraction = .31 if layout == "Canvas" else .68
                click(x + 118 + (w - 190) * fraction, y + row["rowY"] - ui["toolsScroll"] + 17)
                ui = sample("edited")
                field = "ordinaryRenderParameters" if tab == "Render" else "cinematicParameters"
                expected = ui[field][row["index"]]
                ui = scroll(ui, max(0, row["rowY"] - 100) if action == "Save Paths" else 0)
                x, y, w, _ = ui["toolsContentBounds"]
                before = destination.read_bytes()
                if action == "Save Paths":
                    click(x + w - 52, y + row["rowY"] - 28 + 11 - ui["toolsScroll"])
                elif action == "Save CFG":
                    click(x + w - 63, y + 60 - ui["toolsScroll"])
                else:
                    click(x + w - 46, y + 24 - ui["toolsScroll"])
                ui = sample(layout + "-" + action.replace(" ", "-"))
                saved = destination.read_text(encoding="utf-8")
                assert destination.read_bytes() != before, (layout, action)
                assert marker in saved
                match = re.search(r"^" + key + r"\s*=\s*([^\s#]+)", saved, re.MULTILINE)
                assert match and math.isclose(float(match[1]), expected, abs_tol=.00051), (action, key, expected, match)
                (session / (layout + "-" + action.replace(" ", "-") + ".cfg")).write_text(saved, encoding="utf-8")
                assert hashlib.sha256(original.read_bytes()).hexdigest() == original_hash
        print("PASS: native Save CFG, Save Paths and Save Sky preserve comments and persist owner values in both layouts")
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/unified-config-files-ui")
    run(parser.parse_args().session)
