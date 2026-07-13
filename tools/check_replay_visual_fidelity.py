"""Compare the frame-exact 200-box replay visual manifest.

Purpose:
  Turns the Profile interaction report into a bounded, immutable golden contract
  for every deterministic prediction reveal frame.

Invariants:
  - Validation never updates the baseline.
  - Reveal rows are contiguous ReplayFrameIndex values 0 through 2400.
  - All 200 authored wall bricks move before the approved horizon ends.
  - The first differing field is reported, not merely a whole-file hash.

The explicit --approve-baseline lane is a cold owner action used only while
freezing a user-approved working base. The validation batch never supplies it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REPORT = ROOT / "TestOutput/validation/replay_visual_fidelity/full_reveal_probe_debug.json"
DEFAULT_BASELINE = ROOT / "TestOutput/baselines/replay_visual_fidelity_200_box.json"
SCENE = ROOT / "SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json"
SCRIPT = ROOT / "SkullbonezData/interaction/prediction_ragdoll_wall_200_full_reveal.json"
CONFIG = ROOT / "SkullbonezData/engine.cfg"
SHADER_ROOT = ROOT / "SkullbonezData/shaders"
EXPECTED_TICKS = 2401
EXPECTED_LAST_REVEAL = 2400
EXPECTED_WALL_BRICKS = 200
EXPECTED_START_FRAME = 900


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def shader_tree_sha256() -> str:
    digest = hashlib.sha256()
    for path in sorted(item for item in SHADER_ROOT.rglob("*") if item.is_file()):
        digest.update(path.relative_to(ROOT).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def git_head() -> str:
    return subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, encoding="utf-8"
    ).strip()


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def validate_report_shape(report: dict[str, Any]) -> list[dict[str, Any]]:
    if not report.get("ok"):
        raise ValueError(f"interaction report failed: {report.get('failure', 'unknown failure')}")
    fidelity = report.get("replayVisualFidelity", {})
    if fidelity.get("schemaVersion") != 1:
        raise ValueError(f"packet schema mismatch: expected=1 actual={fidelity.get('schemaVersion')}")
    if fidelity.get("startFrame") != EXPECTED_START_FRAME:
        raise ValueError(
            "reveal did not start at its fixed presentation frame: "
            f"expected={EXPECTED_START_FRAME} actual={fidelity.get('startFrame')}"
        )
    ticks = fidelity.get("ticks", [])
    if len(ticks) != EXPECTED_TICKS or fidelity.get("tickCount") != EXPECTED_TICKS:
        raise ValueError(
            f"incomplete horizon: expected_ticks={EXPECTED_TICKS} actual_ticks={len(ticks)}"
        )
    for index, tick in enumerate(ticks):
        if tick.get("revealFrame") != index:
            raise ValueError(
                f"missing/reordered tick at row={index}: expected_reveal={index} "
                f"actual_reveal={tick.get('revealFrame')}"
            )
    if ticks[-1].get("revealFrame") != EXPECTED_LAST_REVEAL:
        raise ValueError("prediction horizon ended before ReplayFrameIndex 2400")
    final = report.get("finalState", {})
    if final.get("predictionAuthoredWallBrickCount") != EXPECTED_WALL_BRICKS:
        raise ValueError(
            "authored wall changed: "
            f"expected={EXPECTED_WALL_BRICKS} actual={final.get('predictionAuthoredWallBrickCount')}"
        )
    if final.get("predictionMovedWallBrickCount") != EXPECTED_WALL_BRICKS:
        raise ValueError(
            "whole wall did not topple within the prediction horizon: "
            f"expected_moved={EXPECTED_WALL_BRICKS} actual={final.get('predictionMovedWallBrickCount')}"
        )
    if not final.get("predictionTrajectoryFingerprintReady"):
        raise ValueError("trajectory fingerprint is empty")
    return ticks


def visual_ticks(ticks: list[dict[str, Any]]) -> list[dict[str, Any]]:
    # ReplayFrameIndex is the binding key. The fixed presentation start makes
    # sceneFrame redundant, but every typed count and exact raw/canonical buffer
    # hash remains part of the golden contract. Reordering submitted geometry is
    # a visual change even if a canonical diagnostic hash still matches.
    return [
        {key: value for key, value in tick.items() if key != "sceneFrame"}
        for tick in ticks
    ]


def baseline_payload(
    report: dict[str, Any], working_base_commit: str, configuration: str
) -> dict[str, Any]:
    ticks = validate_report_shape(report)
    final = report["finalState"]
    return {
        "format": "skullbonez.replay-visual-fidelity.json",
        "schemaVersion": 1,
        "workingBaseCommit": working_base_commit,
        "captureCommit": git_head(),
        "configuration": configuration,
        "fixedStep": True,
        "target": "prediction_striker_ball",
        "horizonSeconds": 20.0,
        "sceneSha256": sha256(SCENE),
        "scriptSha256": sha256(SCRIPT),
        "configSha256": sha256(CONFIG),
        "shadersSha256": shader_tree_sha256(),
        "tickCount": len(ticks),
        "finalState": {
            "predictionAuthoredWallBrickCount": final["predictionAuthoredWallBrickCount"],
            "predictionAffectedWallBrickCount": final["predictionAffectedWallBrickCount"],
            "predictionMovedWallBrickCount": final["predictionMovedWallBrickCount"],
            "predictionFutureNodeCount": final["predictionFutureNodeCount"],
            "predictionTrajectoryRecordCount": final["predictionTrajectoryRecordCount"],
            "predictionTrajectoryPointCount": final["predictionTrajectoryPointCount"],
        },
        "ticks": visual_ticks(ticks),
    }


def first_difference(expected: Any, actual: Any, path: str = "root") -> str | None:
    if type(expected) is not type(actual):
        return f"{path}: expected_type={type(expected).__name__} actual_type={type(actual).__name__}"
    if isinstance(expected, dict):
        for key in expected:
            if key not in actual:
                return f"{path}.{key}: missing actual field"
            difference = first_difference(expected[key], actual[key], f"{path}.{key}")
            if difference:
                return difference
        for key in actual:
            if key not in expected:
                return f"{path}.{key}: unexpected actual field"
        return None
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return f"{path}: expected_count={len(expected)} actual_count={len(actual)}"
        for index, (expected_item, actual_item) in enumerate(zip(expected, actual)):
            difference = first_difference(expected_item, actual_item, f"{path}[{index}]")
            if difference:
                return difference
        return None
    if expected != actual:
        return f"{path}: expected={expected!r} actual={actual!r}"
    return None


def comparable_report(report: dict[str, Any]) -> dict[str, Any]:
    validate_report_shape(report)
    final = report["finalState"]
    return {
        "tickCount": report["replayVisualFidelity"]["tickCount"],
        "finalState": {
            "predictionAuthoredWallBrickCount": final["predictionAuthoredWallBrickCount"],
            "predictionAffectedWallBrickCount": final["predictionAffectedWallBrickCount"],
            "predictionMovedWallBrickCount": final["predictionMovedWallBrickCount"],
            "predictionFutureNodeCount": final["predictionFutureNodeCount"],
            "predictionTrajectoryRecordCount": final["predictionTrajectoryRecordCount"],
            "predictionTrajectoryPointCount": final["predictionTrajectoryPointCount"],
        },
        "ticks": visual_ticks(report["replayVisualFidelity"]["ticks"]),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--approve-baseline", action="store_true")
    parser.add_argument("--working-base-commit", default="6a6ab4c65")
    parser.add_argument("--configuration", choices=("Debug", "Profile"), default="Debug")
    parser.add_argument("--negative-control", action="store_true")
    parser.add_argument("--incomplete-control", action="store_true")
    args = parser.parse_args()

    report = load_json(args.report)
    if args.approve_baseline:
        try:
            payload = baseline_payload(report, args.working_base_commit, args.configuration)
        except ValueError as error:
            print(f"FAIL replay visual fidelity report: {error}")
            return 1
        args.baseline.parent.mkdir(parents=True, exist_ok=True)
        with args.baseline.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(payload, stream, indent=2)
            stream.write("\n")
        print(
            f"APPROVED replay visual baseline: ticks={payload['tickCount']} "
            f"moved_wall_bricks={payload['finalState']['predictionMovedWallBrickCount']} "
            f"path={args.baseline}"
        )
        return 0

    baseline = load_json(args.baseline)
    if args.incomplete_control:
        report["replayVisualFidelity"]["ticks"].pop()
        try:
            validate_report_shape(report)
        except ValueError as error:
            if "incomplete horizon" in str(error):
                print(f"PASS incomplete-horizon control rejected: {error}")
                return 0
            print(f"FAIL incomplete-horizon control reported the wrong failure: {error}")
            return 1
        print("FAIL incomplete-horizon control was accepted")
        return 1

    current_provenance = {
        "sceneSha256": sha256(SCENE),
        "scriptSha256": sha256(SCRIPT),
        "configSha256": sha256(CONFIG),
        "shadersSha256": shader_tree_sha256(),
    }
    for field, actual_hash in current_provenance.items():
        if baseline.get(field) != actual_hash:
            print(
                f"FAIL replay visual fidelity provenance: {field} "
                f"expected={baseline.get(field)} actual={actual_hash}"
            )
            return 1
    try:
        actual = comparable_report(report)
    except ValueError as error:
        print(f"FAIL replay visual fidelity report: {error}")
        return 1
    expected = {
        "tickCount": baseline["tickCount"],
        "finalState": baseline["finalState"],
        "ticks": baseline["ticks"],
    }
    if args.negative_control:
        # Negative-control lane: alter one submitted float-stream fingerprint in
        # memory and require the ordinary comparator to name that exact field.
        actual["ticks"][1200]["ordinaryVertexHash"] = "0x0000000000000001"

    difference = first_difference(expected, actual)
    if args.negative_control:
        if difference and "ticks[1200].ordinaryVertexHash" in difference:
            print(f"PASS negative control detected first divergence: {difference}")
            return 0
        print(f"FAIL negative control was not detected at the injected field: {difference}")
        return 1
    if difference:
        print(f"FAIL replay visual fidelity first divergence: {difference}")
        return 1
    print(
        f"PASS replay visual fidelity: ticks={actual['tickCount']} "
        f"moved_wall_bricks={actual['finalState']['predictionMovedWallBrickCount']} "
        f"first_reveal={actual['ticks'][0]['revealFrame']} "
        f"last_reveal={actual['ticks'][-1]['revealFrame']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
