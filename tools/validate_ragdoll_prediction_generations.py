#!/usr/bin/env python3
"""Verify articulated futures survive cancellation and repeat at one live source."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from physics_ab_capture import CaptureSide, digest, write_json
from validate_skarness_prediction_matrix import PredictionCase, validate_prediction

REPO = Path(__file__).resolve().parents[1]
STATE_KINDS = {"snapshot", "append", "change", "evict", "reset", "state"}


def latest_topics(path: Path) -> dict:
    latest = {}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            if not line.endswith("\n"):
                break
            row = json.loads(line)
            if row.get("kind") in STATE_KINDS and "topic" in row:
                latest[row["topic"]] = row
    return latest


def snapshot(side: CaptureSide, label: str, *, full: bool = False) -> dict:
    side.command("state.subscribe", {"topics": [], "detail": "full" if full else "summary"})
    # Advance presentation only: the live Physics source must stay paused.
    side.command("run.step_frames", {"count": 1})
    topics = latest_topics(side.directory / "runtime.skarness.ndjson")
    write_json(side.directory / f"{label}.json", topics)
    if not topics["replay.state"]["paused"]:
        raise ValueError("Prediction lifecycle capture advanced the live simulation")
    return topics


def projected_future(topics: dict) -> bytes:
    frames = topics["replay.prediction.frames"]["payload"]
    evidence = topics["replay.prediction.evidence"]["payload"]
    # Generation/epoch/publication identify a new bank, so they must differ.
    # Keep every published frame, body and solver row in its original order.
    # Native JSON emits round-trip float values; compare serialized values,
    # including signed zero, without epsilon or decimal rounding.
    evidence_frames = []
    for row in evidence["frames"]:
        if not row["complete"]:
            raise ValueError("Prediction evidence is incomplete")
        evidence_frames.append({key: value for key, value in row.items()
                                if key not in {"generation", "bankEpoch", "topologyVersion", "publicationVersion"}})
    if frames["count"] < 2 or not evidence_frames:
        raise ValueError("Prediction capture lacks frame or solver evidence")
    return json.dumps({"frames": frames, "evidence": evidence_frames}, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def complete(side: CaptureSide, label: str, target: int) -> tuple[dict, bytes]:
    side.command("prediction.select_target", {"sceneObjectId": target})
    side.command("replay.set_prediction_horizon", {"seconds": 2.0})
    side.command("replay.set_prediction_enabled", {"enabled": True})
    side.command("run.until", {"condition": "prediction.complete", "maxFrames": 3000})
    topics = snapshot(side, label, full=True)
    errors = validate_prediction(PredictionCase(label, None, target_id=target), topics["replay.state"])
    state = topics["replay.state"]["payload"]
    if state["publishedPredictionTargetId"] != target:
        errors.append("Wrong published target")
    if errors:
        raise ValueError(f"{label}: {errors}")
    payload = projected_future(topics)
    (side.directory / f"{label}.future.json").write_bytes(payload)
    side.command("state.subscribe", {"topics": [], "detail": "summary"})
    return {"label": label, "target": target, "generation": state["predictionGeneration"],
            "sourceFrame": state["predictionSourceFrame"], "sha256": hashlib.sha256(payload).hexdigest(),
            "bytes": len(payload)}, payload


def run(output: Path, executable: Path) -> None:
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    scene = REPO / "SkullbonezData/scenes/ragdoll_sleep_island.scene.json"
    side = CaptureSide(executable.resolve(), output, scene)
    producer_hash = digest(executable)
    try:
        capabilities = side.start(worker_threads=4)
        required = {"prediction.select_target", "replay.set_prediction_enabled", "replay.set_prediction_horizon",
                    "replay.set_prediction_detail", "run.until", "run.step_frames", "scene.object.resolve",
                    "physics.speculative_validation"}
        if required - capabilities:
            raise ValueError(f"Missing commands: {sorted(required - capabilities)}")
        side.command("state.subscribe", {"topics": [], "detail": "summary"})
        side.command("replay.set_prediction_enabled", {"enabled": False})
        side.command("physics.speculative_validation", {"enabled": True})
        side.command("replay.set_prediction_detail", {"highDetail": True})
        side.command("run.step", {"count": 1})
        targets = []
        for name in ("fall_stack_0_torso", "fall_stack_1_torso"):
            body = side.command("scene.object.resolve", {"name": name})["result"]["objects"][0]
            if body["fixed"]:
                raise ValueError("Prediction target must be dynamic")
            targets.append(body["sceneObjectId"])
        first, expected = complete(side, "first", targets[0])
        # A long replacement exposes an actual in-flight generation. Disabling
        # while already complete would exercise reset, not cancellation.
        side.command("replay.set_prediction_horizon", {"seconds": 120.0})
        side.command("prediction.select_target", {"sceneObjectId": targets[1]})
        building = snapshot(side, "before-cancel")["replay.state"]["payload"]
        if not building["predictionBuilding"]:
            raise ValueError("Cancellation fixture did not expose in-flight work")
        side.command("replay.set_prediction_enabled", {"enabled": False})
        cancelled = snapshot(side, "after-cancel")["replay.state"]["payload"]
        if cancelled["predictionEnabled"] or cancelled["predictionBuilding"]:
            raise ValueError("Cancelled prediction remains active")
        repeated, actual = complete(side, "after-cancellation", targets[0])
        if actual != expected or repeated["generation"] <= first["generation"]:
            raise ValueError("Cancelled work changed the repeated articulated future")
        other, _ = complete(side, "other-target", targets[1])
        again, actual = complete(side, "reselected", targets[0])
        if actual != expected or again["generation"] <= repeated["generation"]:
            raise ValueError("Retargeting changed the repeated articulated future")
        if any(row["sourceFrame"] != first["sourceFrame"] for row in (repeated, other, again)):
            raise ValueError("Comparisons do not share one live Physics source")
        side.command("capture.screenshot", {"path": str(output / "reselected.png")})
        report = {"passed": True, "executableSha256": producer_hash,
                   "scope": "Exact published body frames and solver evidence; full solver state has a separate worker oracle",
                   "generations": [first, repeated, other, again], "cancelledGeneration": building["predictionGeneration"]}
    finally:
        side.stop()
    if digest(executable) != producer_hash:
        raise ValueError("Producer changed during capture")
    exit_result = json.loads((output / "process-exit.json").read_text(encoding="utf-8"))
    if exit_result["exitCode"] != 0:
        raise ValueError(f"Prediction capture did not shut down cleanly: {exit_result}")
    # A passing artifact is published only after the owned producer has stopped
    # and its identity is checked; a shutdown failure must not publish a passing report.
    report["processExit"] = exit_result
    write_json(output / "report.json", report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exe", type=Path, default=REPO / "Automation/SKULLBONEZ_CORE.exe")
    args = parser.parse_args()
    run(args.output, args.exe)
