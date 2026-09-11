#!/usr/bin/env python3
"""Verify lazy velocity comparison, synchronized ghosts, and both native choices."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from PIL import Image
from skarness import SkarnessConnection, launch
from validate_skarness_prediction_matrix import ReplayStateReader, vector_distance

REPO = Path(__file__).resolve().parents[1]


def run(session: Path, executable: Path) -> None:
    scene = REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json"
    assert launch(session, executable, scene, hidden=True, allocation_guard="gameplay",
                  layout_file=session / "layout.preferences") == 0
    connection = SkarnessConnection(session)
    reader = ReplayStateReader(session / "runtime.skarness.ndjson")

    def send(command: str, **arguments: object) -> dict:
        result = connection.wait(connection.send(command, arguments))
        assert result.get("status") == "applied", (command, result)
        return result

    def state() -> dict:
        send("run.step_frames", count=2)
        return reader.latest()["payload"]

    def ready(predicate) -> dict:
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            observed = state()
            if predicate(observed):
                return observed
        raise AssertionError(observed)

    def topics() -> dict:
        latest = {}
        for line in (session / "runtime.skarness.ndjson").read_text(encoding="utf-8").splitlines():
            event = json.loads(line)
            if "topic" in event:
                latest[event["topic"]] = event["payload"]
        return latest

    def snapshot(label: str, observed: dict) -> None:
        (session / f"{label}.json").write_text(json.dumps(observed, indent=2), encoding="utf-8")

    def capture(label: str) -> None:
        path = session / f"{label}.png"
        send("capture.screenshot", path=str(path))
        # The native capture may contain BMP bytes despite the requested suffix.
        with Image.open(path) as captured:
            captured.save(session / f"{label}-view.png")

    def frozen_frames() -> dict:
        send("state.subscribe", topics=[], detail="full")
        state()
        frames = topics()["replay.prediction.frames"]["frames"]
        send("state.subscribe", topics=[], detail="normal")
        return {row["frame"]: {body["id"]: body for body in row["bodies"]} for row in frames}

    def verify_scrub(normalized: float, original: dict, label: str) -> dict:
        send("replay.scrub", normalized=normalized)
        observed = state()
        comparison = observed["divergence"]
        assert comparison["active"] and comparison["allocatedOwnerBytes"] > 0
        assert comparison["redFrame"] == comparison["blueFrame"] > 0
        blue = {body["id"]: body for body in comparison["blueBodies"]}
        red = {body["id"]: body for body in comparison["redBodies"]}
        assert blue.keys() == red.keys() == original[comparison["blueFrame"]].keys()
        ghosts = {ghost["modelRow"]: ghost for ghost in comparison["ghosts"]}
        assert len(ghosts) == len(blue)
        for object_id, body in blue.items():
            assert body["position"] == original[comparison["blueFrame"]][object_id]["position"]
            ghost = ghosts[body["modelRow"]]
            assert ghost["position"] == body["position"]
            assert ghost["tint"][2] > ghost["tint"][0] and 0 < ghost["alpha"] < 0.5
        assert vector_distance(blue[6]["position"], red[6]["position"]) > 1
        snapshot(label, observed)
        return observed

    def accept(red: bool) -> dict:
        ui = topics()["ui.presentation"]
        x, _, width, _ = ui["viewport"]
        button_width = min(128, max(0, (width - 24) / 2))
        # Canvas reserves the strip above transport for Planning. Exercise the
        # physical pointer, not an acceptance-only automation implementation.
        button_x = x + 8 + button_width / 2 + (0 if red else button_width + 8)
        send("input.pointer_drag", button="left", x=int(button_x),
             y=int(ui["transportBounds"][1] - 25), deltaX=0, deltaY=0)
        observed = state()
        assert not observed["divergence"]["active"]
        assert observed["divergence"]["allocatedOwnerBytes"] == 0
        assert observed["divergence"]["blueFrameCount"] == 0
        assert not observed["divergence"]["ghosts"]
        return observed

    try:
        catalog = send("capabilities.get")["catalog"]
        assert {"replay.velocity_preview", "replay.velocity_commit", "input.pointer_drag"} <= {row["name"] for row in catalog}
        send("state.subscribe", topics=[], detail="normal")
        assert state()["divergence"]["allocatedOwnerBytes"] == 0
        send("replay.set_prediction_horizon", seconds=3)
        send("prediction.select_target", name="path_striker")
        send("replay.set_prediction_enabled", enabled=True)
        stock = ready(lambda row: row["predictionComplete"] and row["publishedPredictionFrames"] > 2)
        assert stock["divergence"]["allocatedOwnerBytes"] == 0
        snapshot("stock", stock)
        original = frozen_frames()

        for round_index, red_choice in enumerate((False, True)):
            send("replay.set_velocity_edit_enabled", enabled=True)
            assert not state()["divergence"]["active"]
            send("replay.velocity_preview", linear=[90, 12, 20], angular=[0, 0, 0])
            send("replay.velocity_commit")
            ready(lambda row: row["divergence"]["redReady"])
            for index, normalized in enumerate((0.7, 0.85, 1.0)):
                compared = verify_scrub(normalized, original, f"round-{round_index}-scrub-{index}")

            # Editor entry and Delete cannot invalidate the frozen stock topology.
            before_objects = send("scene.object.list")["result"]["objects"]
            send("input.set_key", key=192, down=True)
            state()
            send("input.set_key", key=192, down=False)
            selection = connection.wait(connection.send("scene.object.select", {"scope": "editor", "name": "path_striker"}))
            assert selection["status"] == "rejected" and "editor mode" in selection["reason"]
            send("input.set_key", key=46, down=True)
            state()
            send("input.set_key", key=46, down=False)
            assert send("scene.object.list")["result"]["objects"] == before_objects
            assert not topics()["ui.presentation"]["editorMode"]

            # Space and launcher mode normally force live stepping. Neither may
            # move the seed while a comparison is unresolved.
            live_before = send("scene.object.resolve", name="path_striker")["result"]
            send("input.set_key", key=32, down=True)
            send("run.step_frames", count=30)
            send("input.set_key", key=32, down=False)
            send("input.set_key", key=78, down=True)
            state()
            send("input.set_key", key=78, down=False)
            send("run.step_frames", count=30)
            live_after = send("scene.object.resolve", name="path_striker")["result"]
            assert live_after == live_before, (live_before, live_after)
            send("input.set_key", key=78, down=True)
            state()
            send("input.set_key", key=78, down=False)

            # Transport actions cannot silently commit or erase either branch.
            blocked_save = session / f"unaccepted-{round_index}.skreplay"
            send("replay.set_prediction_enabled", enabled=False)
            send("replay.set_prediction_horizon", seconds=6)
            send("replay.restore_branch")
            send("replay.save", path=str(blocked_save))
            assert not blocked_save.exists()
            retained = verify_scrub(1.0, original, f"round-{round_index}-guarded-controls")
            assert retained["divergence"]["redBodies"] == compared["divergence"]["redBodies"]

            # A subsequent vector change replaces red, never the stored blue.
            send("replay.velocity_preview", linear=[85, 15, 18], angular=[0, 0, 0])
            send("replay.velocity_commit")
            ready(lambda row: row["divergence"]["redReady"])
            compared = verify_scrub(1.0, original, f"round-{round_index}-second-edit")
            expected = compared["divergence"]["redBodies" if red_choice else "blueBodies"]
            capture(f"round-{round_index}-divergence")

            verify_scrub(0.7, original, f"round-{round_index}-before-play")
            send("replay.set_playback_paused", paused=False)
            playing = ready(lambda row: row["divergence"]["redFrame"] > 260)
            assert playing["divergence"]["blueFrame"] == playing["divergence"]["redFrame"]
            send("replay.set_playback_paused", paused=True)
            paused = state()
            send("run.step_frames", count=20)
            assert state()["divergence"]["redFrame"] == paused["divergence"]["redFrame"]
            accepted = accept(red_choice)
            snapshot(f"round-{round_index}-accepted", accepted)
            send("replay.scrub", normalized=1.0)
            committed = state()
            assert committed["divergence"]["redBodies"] == expected
            capture(f"round-{round_index}-accepted")

        # Scene replacement releases an unresolved comparison as well.
        send("replay.set_velocity_edit_enabled", enabled=True)
        assert not state()["divergence"]["active"]
        send("replay.velocity_preview", linear=[110, 12, 20], angular=[0, 0, 0])
        send("replay.velocity_commit")
        ready(lambda row: row["divergence"]["active"])
        send("scene.load", name=scene.name)
        cleared = state()
        assert cleared["divergence"]["allocatedOwnerBytes"] == 0
        snapshot("scene-reset", cleared)
        send("replay.set_prediction_horizon", seconds=3)
        send("prediction.select_target", name="path_striker")
        send("replay.set_prediction_enabled", enabled=True)
        resumed = ready(lambda row: row["predictionComplete"])
        assert resumed["predictionGenerationPermitted"]
        assert resumed["publishedPredictionTargetId"] == 6
        snapshot("prediction-after-unedited-reset", resumed)
    finally:
        try:
            send("session.stop")
        finally:
            connection.close()

    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        shutdown_log = (session / "process.stdout.log").read_text(encoding="utf-8", errors="replace")
        if "[allocation-guard] PASS:" in shutdown_log or "[allocation-guard] FAIL:" in shutdown_log:
            break
        time.sleep(0.05)
    assert "[allocation-guard] PASS:" in shutdown_log, "native shutdown did not pass the allocation guard"
    assert "gameplay_violations=0" in shutdown_log and "policy_violations=0" in shutdown_log
    (session / "result.json").write_text(json.dumps({"passed": True, "choices": ["Blue", "Red"],
        "bodyCount": len(original[0]), "framesPerBranch": len(original), "allocationGuard": "pass",
        "lazyAllocation": True, "releasedAfterBothChoices": True}), encoding="utf-8")
    print(f"PASS: lazy divergence, every ghost, repeated edits, playback, both choices, and reset ({session})")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/velocity-divergence")
    parser.add_argument("--exe", type=Path, default=REPO / "Automation/SKULLBONEZ_CORE.exe")
    args = parser.parse_args()
    run(args.session.resolve(), args.exe.resolve())
