"""Verify restored panels and scene projection agree before any layout input."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import time

from PIL import Image
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]


def run(root: Path) -> None:
    for label, left, right, folded in (("expanded", 264, 450, 0), ("folded", 350, 400, 1)):
        session = root / label
        session.mkdir(parents=True, exist_ok=False)
        preferences = session / "layout.preferences"
        preferences.write_text(
            f"version 5\nlayout 1\nleft {left}\nright {right}\ndrawer 368\n"
            f"diagnostics 140\nfolded 7\ntool 1\nleftFolded {folded}\n"
            f"rightFolded {folded}\ntheme 0\nreplayFolded {folded}\n")
        assert launch(session, REPO / "Automation/SKULLBONEZ_CORE.exe",
                      REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json",
                      hidden=True, layout_file=preferences) == 0
        connection = SkarnessConnection(session)

        def send(command: str, **arguments: object) -> dict:
            result = connection.wait(connection.send(command, arguments))
            assert result.get("status") == "applied", result
            return result

        try:
            assert "capture.screenshot" in send("capabilities.get")["commands"]
            send("state.subscribe", topics=[], detail="normal")
            send("run.step_frames", count=3)
            # Read from the start: a correct rectangle after a later click or
            # window resize cannot satisfy this boot assertion.
            with (session / "runtime.skarness.ndjson").open() as stream:
                first = next(row["payload"] for line in stream
                             if (row := json.loads(line)).get("topic") == "ui.presentation")
            width, height = first["window"]
            x, y, w, h = first["viewport"]
            assert first["layout"] == "Editor"
            assert [x, y, w, h] == [24 if folded else left, 42,
                                   width - (48 if folded else left + right), height - 70], first
            sx, sy = first["projectionScale"]
            assert math.isclose(sy / sx, w / h, rel_tol=1e-5), first
            (session / "first-frame.json").write_text(json.dumps(first, indent=2))
            time.sleep(.25)
            send("run.step_frames", count=3)
            image_path = session / "boot.png"
            send("capture.screenshot", path=str(image_path.resolve()))
            with Image.open(image_path) as picture:
                picture.save(session / "boot-view.png")
        finally:
            send("session.stop")
            connection.close()
    print("PASS: expanded and folded saved panels reserve the scene viewport and projection on the first frame")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/viewport-startup")
    run(parser.parse_args().session)
