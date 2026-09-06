#!/usr/bin/env python3
"""Verify held causal arrows, camera adjustments, retained futures, and exit time."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from PIL import Image

from skarness import SkarnessConnection, launch
from validate_skarness_prediction_matrix import ReplayStateReader, vector_distance


REPO = Path(__file__).resolve().parents[1]


def verify_position_gate(path: Path) -> None:
    """The followed primary object's projected center must carry the cyan diamond."""
    with Image.open(path) as image:
        center_x, center_y = image.width // 2, image.height // 2
        pixels = image.convert("RGB").crop((center_x - 24, center_y - 24, center_x + 24, center_y + 24))
        samples = (pixels.getpixel((x, y)) for y in range(pixels.height) for x in range(pixels.width))
        cyan_count = sum(1 for red, green, blue in samples
                         if red < 110 and green > 200 and blue > 230)
        assert cyan_count >= 35, f"selected-object position gate missing from {path}: {cyan_count} cyan pixels"


def verify_retained_geometry(session: Path) -> None:
    start = json.loads((session / "selected.json").read_text(encoding="utf-8"))["runtimeTurn"]
    end = json.loads((session / "stopped.json").read_text(encoding="utf-8"))["runtimeTurn"]
    retained = None
    initial = None
    with (session / "runtime.skarness.ndjson").open(encoding="utf-8") as trace:
        for line in trace:
            event = json.loads(line)
            if event.get("runtimeTurn", 0) > end:
                break
            if event.get("topic") == "replay.visual_packet":
                payload = event["payload"]
                retained = (payload["retainedStreamId"], payload["retainedRevision"])
            if event.get("runtimeTurn", 0) >= start and retained:
                initial = initial or retained
                assert retained == initial, "causal playback rebuilt retained future geometry"
    assert initial and initial[0] != 0, "no retained prediction geometry was observed"


def run(session: Path, executable: Path) -> None:
    scene = REPO / "SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json"
    if launch(session, executable, scene, hidden=True) != 0:
        raise RuntimeError("causal playback fixture could not launch")
    connection = SkarnessConnection(session)
    reader = ReplayStateReader(session / "runtime.skarness.ndjson")

    def send(command: str, **arguments: object) -> dict:
        result = connection.wait(connection.send(command, arguments))
        if result.get("status") != "applied":
            raise RuntimeError(f"{command}: {result}")
        return result

    def state(label: str) -> dict:
        send("run.step_frames", count=2)
        latest = reader.latest()
        if latest is None:
            raise RuntimeError("replay state was not published")
        payload = latest["payload"]
        (session / f"{label}.json").write_text(json.dumps(latest, indent=2), encoding="utf-8")
        return payload

    try:
        capabilities = send("capabilities.get")
        assert any(row["name"] == "input.set_arrows" for row in capabilities["catalog"])
        # All topics remain durable on disk; live subscriptions are unnecessary
        # for this synchronous command client and can fill the pipe during QA.
        send("state.subscribe", topics=[], detail="normal")
        send("input.set_arrows", left=False, right=True)
        outside = state("outside")
        send("input.set_arrows", left=False, right=False)
        assert outside["causeInspectionMode"] == 0
        objects = send("scene.object.list")["result"]["objects"]
        assert any(row["name"] == "path_striker" and row["sceneObjectId"] == 6 for row in objects)
        send("replay.set_prediction_horizon", seconds=7.5)
        send("prediction.select_target", name="path_striker")
        send("replay.set_prediction_enabled", enabled=True)
        send("run.until", condition="prediction.complete", maxFrames=3000)
        send("replay.select_cause_row", row=2)
        send("run.until", condition="camera.inspection_settled", maxFrames=1000)
        before = state("selected")
        assert before["inspectionCameraFocusKind"] == 2
        assert before["selectedCausePrimaryId"] == 1
        send("capture.screenshot", path=str((session / "selected.png").resolve()))
        verify_position_gate(session / "selected.png")
        send("camera.orbit_inspection", yawRadians=0.25, pitchRadians=0.1, wheelDelta=120)
        orbit = state("orbit")
        radius = vector_distance(orbit["cameraPrimaryEye"], orbit["inspectionPivot"])
        assert abs(radius - vector_distance(before["cameraPrimaryEye"], before["inspectionPivot"])) > 0.01
        send("input.set_arrows", left=False, right=True)
        send("run.step_frames", count=60)
        forward = state("forward")
        send("input.set_arrows", left=False, right=False)
        send("capture.screenshot", path=str((session / "forward.png").resolve()))
        verify_position_gate(session / "forward.png")
        paused = state("paused")
        send("run.step_frames", count=60)
        held = state("held")
        send("input.set_arrows", left=True, right=True)
        both = state("both")
        send("input.set_arrows", left=True, right=False)
        send("run.step_frames", count=30)
        reverse = state("reverse")
        send("input.set_arrows", left=False, right=False)
        stopped = state("stopped")
        send("capture.screenshot", path=str((session / "playback.png").resolve()))
        verify_position_gate(session / "playback.png")
        verify_retained_geometry(session)
        send("replay.return_from_cause")
        exited = state("exited")
        send("input.set_arrows", left=True, right=False)
        after_exit = state("after-exit")
        assert forward["causePresentedFrame"] > before["causePresentedFrame"]
        assert held["causePresentedFrame"] == paused["causePresentedFrame"] == both["causePresentedFrame"]
        assert reverse["causePresentedFrame"] < held["causePresentedFrame"]
        assert exited["presentedReplayFrame"] == stopped["presentedReplayFrame"] == after_exit["presentedReplayFrame"]
        assert after_exit["causeInspectionMode"] == 0
        for sample in (forward, paused, held, reverse, stopped):
            assert sample["selectedCausePrimaryId"] == before["selectedCausePrimaryId"]
            assert sample["publishedPredictionTargetId"] == sample["submittedPredictionTargetId"] == sample["pathTargetId"]
            assert sample["predictionGeneration"] == before["predictionGeneration"]
            assert sample["submittedPredictionSourceFrame"] == before["predictionSourceFrame"]
            assert sample["predictionEnabled"] and sample["trajectorySubmitted"]
            assert sample["futureNodeCount"] == before["futureNodeCount"]
            assert vector_distance(sample["cameraPrimaryView"], sample["inspectionPivot"]) < 0.001
            assert abs(vector_distance(sample["cameraPrimaryEye"], sample["inspectionPivot"]) - radius) < 0.01
            offset = [a - b for a, b in zip(sample["cameraPrimaryEye"], sample["inspectionPivot"])]
            orbit_offset = [a - b for a, b in zip(orbit["cameraPrimaryEye"], orbit["inspectionPivot"])]
            assert vector_distance(offset, orbit_offset) < 0.01
        print(f"PASS: causal arrows, release, both keys, zoom/orbit, retained futures, and exit time ({session})")
    finally:
        try:
            send("input.set_arrows", left=False, right=False)
            send("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput/skarness/causal-playback")
    parser.add_argument("--exe", type=Path, default=REPO / "Automation/SKULLBONEZ_CORE.exe")
    args = parser.parse_args()
    started = time.monotonic()
    run(args.session, args.exe)
    print(f"Elapsed: {time.monotonic() - started:.3f}s")
