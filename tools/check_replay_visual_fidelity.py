"""File: tools/check_replay_visual_fidelity.py
Purpose:
  Turns the Profile interaction report into a bounded, immutable golden contract
  for every deterministic prediction reveal frame.

Mental model:
  Each hidden engine process records the approved prediction once. This checker
  compares one report with the golden and can compare two sequential clean-run
  reports without launching the engine or generating another prediction.

Glossary:
  Reveal tick: One future-frame packet exposed by the prediction cascade.
  Causal baseline: Approved topology and activation order for the wall cascade.
  Packet hash: Deterministic digest of the exact ordered body data rendered for
    a predicted or subsequently live frame.
  Determinism contract: Ordered report/artifact projection that excludes only
    measured wall-clock throughput and retains every simulation/visual input.

Invariants:
  - Validation never updates the baseline.
  - Reveal rows are contiguous ReplayFrameIndex values 0 through 2400.
  - All 200 authored wall bricks move before the approved horizon ends.
  - The first differing field is reported, not merely a whole-file hash.
  - This checker is read-only and cannot start a second prediction generation.

The explicit --approve-baseline lane is a cold owner action used only while
freezing a user-approved working base. The validation batch never supplies it.

Related:
  - tools/validate_replay_visual_fidelity.bat
  - tools/replay_query.py
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any

from replay_query import ReplayQueryError, ReplayV2


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REPORT = ROOT / "TestOutput/validation/replay_visual_fidelity/full_reveal_probe_debug.json"
DEFAULT_BASELINE = ROOT / "TestOutput/baselines/replay_visual_fidelity_200_box.json"
DEFAULT_CAUSAL_BASELINE = (
    ROOT / "TestOutput/baselines/replay_visual_fidelity_200_box_causal.json"
)
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


def validate_causal_shape(report: dict[str, Any]) -> dict[str, Any]:
    causal = report.get("replayCausalProof", {})
    if causal.get("schemaVersion") != 1:
        raise ValueError(
            f"causal schema mismatch: expected=1 actual={causal.get('schemaVersion')}"
        )
    if not causal.get("singleRevealGeneration"):
        raise ValueError("causal proof did not certify one reveal generation")
    if not causal.get("liveComparisonComplete"):
        raise ValueError("predicted-versus-live comparison did not complete")

    ticks = causal.get("ticks", [])
    topology = causal.get("topology", [])
    live_ticks = causal.get("predictedLiveTicks", [])
    if len(ticks) != EXPECTED_TICKS or causal.get("tickCount") != EXPECTED_TICKS:
        raise ValueError(
            f"causal horizon incomplete: expected_ticks={EXPECTED_TICKS} actual_ticks={len(ticks)}"
        )
    if not topology or causal.get("topologyCount") != len(topology):
        raise ValueError("causal topology is empty or count-mismatched")
    if len(live_ticks) != EXPECTED_TICKS or causal.get("predictedLiveTickCount") != EXPECTED_TICKS:
        raise ValueError(
            "predicted-versus-live horizon incomplete: "
            f"expected_ticks={EXPECTED_TICKS} actual_ticks={len(live_ticks)}"
        )

    target_id = causal.get("targetId", 0)
    if not isinstance(target_id, int) or target_id <= 0:
        raise ValueError(f"causal target identity is invalid: {target_id!r}")
    node_by_id: dict[int, dict[str, Any]] = {}
    for index, node in enumerate(topology):
        node_id = node.get("id", 0)
        if not isinstance(node_id, int) or node_id <= 0 or node_id in node_by_id:
            raise ValueError(f"topology[{index}].id is invalid or duplicated: {node_id!r}")
        node_by_id[node_id] = node
    for index, node in enumerate(topology):
        parent_id = node.get("parentId", 0)
        parent_depth = 0 if parent_id == target_id else node_by_id.get(parent_id, {}).get("depth")
        if parent_depth is None:
            raise ValueError(f"topology[{index}].parentId does not resolve: {parent_id!r}")
        if node.get("depth") != parent_depth + 1:
            raise ValueError(
                f"topology[{index}].depth breaks parent chain: "
                f"parent_depth={parent_depth} actual={node.get('depth')}"
            )
        first_frame = node.get("firstFrame")
        if not isinstance(first_frame, int) or first_frame < 0 or first_frame > EXPECTED_LAST_REVEAL:
            raise ValueError(f"topology[{index}].firstFrame is outside the horizon: {first_frame!r}")

    if ticks[0].get("revealFrame") != 0 or ticks[0].get("activeNodeCount") != 0:
        raise ValueError("downstream topology activated before the striker path")
    if ticks[0].get("revealedRecordCount", 0) < 1 or ticks[0].get("revealedSegmentCount", 0) < 1:
        raise ValueError("striker path was not present on reveal tick zero")

    monotonic_fields = (
        "activeNodeCount",
        "revealedRecordCount",
        "revealedPointCount",
        "revealedSegmentCount",
        "entryMarkerCount",
        "restMarkerCount",
        "horizonMarkerCount",
    )
    for index, tick in enumerate(ticks):
        if tick.get("revealFrame") != index:
            raise ValueError(
                f"causal tick reordered at row={index}: actual={tick.get('revealFrame')}"
            )
        if index:
            for field in monotonic_fields:
                if tick.get(field, -1) < ticks[index - 1].get(field, -1):
                    raise ValueError(
                        f"causal field regressed at ticks[{index}].{field}: "
                        f"previous={ticks[index - 1].get(field)} actual={tick.get(field)}"
                    )
    if ticks[-1].get("activeNodeCount") != len(topology):
        raise ValueError(
            "not every downstream causal node activated: "
            f"expected={len(topology)} actual={ticks[-1].get('activeNodeCount')}"
        )

    source_frame = causal.get("sourceFrame")
    for index, tick in enumerate(live_ticks):
        if tick.get("offset") != index:
            raise ValueError(
                f"predicted-versus-live row reordered at index={index}: actual={tick.get('offset')}"
            )
        if tick.get("liveFrame") != source_frame + index:
            raise ValueError(
                f"live solver frame skipped at index={index}: "
                f"expected={source_frame + index} actual={tick.get('liveFrame')}"
            )
        if tick.get("predictedHash") != tick.get("liveHash"):
            raise ValueError(
                f"predicted-versus-live hash diverged at index={index}: "
                f"predicted={tick.get('predictedHash')} live={tick.get('liveHash')}"
            )
        if tick.get("bodyCount", 0) <= 0:
            raise ValueError(f"predicted-versus-live body packet is empty at index={index}")
    return causal


def validate_artifact_roundtrip(report: dict[str, Any]) -> dict[str, Any]:
    artifact_report = report.get("replayArtifact", {})
    if artifact_report.get("schemaVersion") != 3 or not artifact_report.get("saved"):
        raise ValueError("v3 durable replay artifact was not saved by the fidelity probe")
    path = Path(str(artifact_report.get("path", "")))
    if not path.is_absolute():
        path = ROOT / path
    if not path.is_file():
        raise ValueError(f"v3 durable replay artifact is missing: {path}")

    try:
        artifact = ReplayV2(path)
        packet_hashes = artifact.presentation_packet_hashes()
    except ReplayQueryError as error:
        raise ValueError(f"v3 durable replay artifact is invalid: {error}") from error
    if artifact.version != 3 or artifact.manifest.get("version") != 3:
        raise ValueError(
            "durable replay artifact version mismatch: "
            f"header={artifact.version} manifest={artifact.manifest.get('version')}"
        )
    if artifact.manifest.get("bodyPoseBytes") != 76:
        raise ValueError("v3 artifact did not declare the full 76-byte visual body state")
    if len(packet_hashes) != artifact_report.get("sampleCount"):
        raise ValueError(
            "v3 artifact sample count mismatch: "
            f"report={artifact_report.get('sampleCount')} loaded={len(packet_hashes)}"
        )

    hashes_by_frame = {row["frameIndex"]: row for row in packet_hashes}
    live_ticks = report.get("replayCausalProof", {}).get("predictedLiveTicks", [])
    if len(live_ticks) != EXPECTED_TICKS:
        raise ValueError("artifact comparison requires the complete predicted/live horizon")
    for index, tick in enumerate(live_ticks):
        frame = tick.get("liveFrame")
        saved = hashes_by_frame.get(frame)
        if not saved:
            raise ValueError(f"saved artifact omitted live packet frame {frame} at row {index}")
        if saved["bodyCount"] != tick.get("bodyCount") or saved["hash"] != tick.get("liveHash"):
            raise ValueError(
                f"saved/load packet divergence at row={index} frame={frame}: "
                f"expected_count={tick.get('bodyCount')} actual_count={saved['bodyCount']} "
                f"expected_hash={tick.get('liveHash')} actual_hash={saved['hash']}"
            )
    return {
        "path": str(path),
        "sampleCount": len(packet_hashes),
        "firstFrame": packet_hashes[0]["frameIndex"],
        "lastFrame": packet_hashes[-1]["frameIndex"],
    }


def replay_artifact_path(report: dict[str, Any]) -> Path:
    path = Path(str(report.get("replayArtifact", {}).get("path", "")))
    return path if path.is_absolute() else ROOT / path


def replay_artifact_determinism_contract(report: dict[str, Any]) -> dict[str, Any]:
    path = replay_artifact_path(report)
    artifact = ReplayV2(path)
    return {
        "version": artifact.version,
        "manifest": artifact.manifest,
        "bodyDictionary": [
            {
                "dictionaryIndex": body.dictionary_index,
                "bodyId": body.body_id,
                "modelIndex": body.model_index,
                "shapeKind": body.shape_kind,
                "name": body.name,
                "massHex": float(body.mass).hex(),
                "fixed": body.fixed,
            }
            for body in artifact.bodies
        ],
        "frameHeaders": artifact.presentation_frame_headers(),
        "presentationPackets": artifact.presentation_packet_hashes(),
        "branches": [
            {
                "branchId": row.branch_id,
                "parentBranchId": row.parent_branch_id,
                "startFrame": row.start_frame,
                "firstRetainedFrame": row.first_retained_frame,
                "lastRetainedFrame": row.last_retained_frame,
                "sourceFrame": row.source_frame,
                "sourceSolverHash": row.source_solver_hash,
                "flags": row.flags,
            }
            for row in artifact.branches
        ],
        "events": [
            {
                "frameIndex": row.frame_index,
                "sequence": row.sequence,
                "branchId": row.branch_id,
                "parentBranchId": row.parent_branch_id,
                "kind": row.kind,
                "payloadVersion": row.payload_version,
                "flags": row.flags,
                "values": list(row.values),
                "data0": row.data0,
                "sourceFrame": row.source_frame,
                "sourceSolverHash": row.source_solver_hash,
                "text": row.text,
            }
            for row in artifact.events
        ],
        "eventCursors": [
            {
                "frameIndex": row.frame_index,
                "eventCursor": row.event_cursor,
                "flags": row.flags,
                "solverHash": row.solver_hash,
            }
            for row in artifact.event_cursors
        ],
        "solverHashes": [
            {
                "frameIndex": row.frame_index,
                "sceneFrame": row.scene_frame,
                "timeSecondsHex": float(row.time_seconds).hex(),
                "presentationHash": row.presentation_hash,
                "solverHash": row.solver_hash,
                "bodyCount": row.body_count,
                "contactCount": row.contact_count,
                "pipelineRecordCount": row.pipeline_record_count,
                "checkpointBoundary": row.checkpoint_boundary,
            }
            for row in artifact.solver_hashes
        ],
        # This comes last so a semantic row is named before the whole-file hash
        # if a process differs in any decoded presentation or timeline field.
        "artifactSha256": sha256(path),
    }


def determinism_contract(report: dict[str, Any]) -> dict[str, Any]:
    ticks = validate_report_shape(report)
    causal = validate_causal_shape(report)
    validate_artifact_roundtrip(report)
    artifact = replay_artifact_determinism_contract(report)
    scene_data = load_json(SCENE)
    final = report["finalState"]
    start_frame = report["replayVisualFidelity"]["startFrame"]
    reveal_mapping = [
        {"sceneFrame": tick["sceneFrame"], "revealFrame": tick["revealFrame"]}
        for tick in ticks
    ]
    for index, mapping in enumerate(reveal_mapping):
        if mapping["sceneFrame"] != start_frame + index:
            raise ValueError(
                f"reveal-frame mapping drifted at row {index}: "
                f"expected_scene={start_frame + index} actual_scene={mapping['sceneFrame']}"
            )

    frame_headers = artifact["frameHeaders"]
    if not frame_headers or not all(row["fixedStep"] for row in frame_headers):
        raise ValueError("saved presentation contains a non-fixed-step frame")
    event_cursors = artifact["eventCursors"]
    if not event_cursors:
        raise ValueError("saved presentation contains no event-cursor checkpoints")
    if any(
        event_cursors[index]["frameIndex"] <= event_cursors[index - 1]["frameIndex"]
        for index in range(1, len(event_cursors))
    ):
        raise ValueError("saved event-cursor checkpoints are not strictly ordered")

    reserve_start = int(final["predictionTrajectoryReserveGrowthEventsAtStart"])
    reserve_end = int(final["predictionTrajectoryReserveGrowthEventsAtEnd"])
    worker_complete = (
        not final["replayPredictionEnabled"]
        and not final["predictionPendingLatestRestart"]
        and final["predictionSupersededRestartCount"] == 0
        and final["predictionLatestRestartBeginCount"] == 0
        and final["predictionBuildFrameCount"] == 0
        and final["predictionFrameCount"] == EXPECTED_TICKS
        and causal["singleRevealGeneration"]
    )
    if not worker_complete:
        raise ValueError("prediction worker/restart state was not quiescent after one generation")
    if not final["predictionTrajectorySteadyStateNoReserveGrowth"] or reserve_end != reserve_start:
        raise ValueError(
            f"trajectory reserve grew during proof: start={reserve_start} end={reserve_end}"
        )
    if final["predictionHorizonSeconds"] != 20.0:
        raise ValueError(f"prediction horizon drifted: {final['predictionHorizonSeconds']}")
    if not scene_data.get("playback", {}).get("fixedStep"):
        raise ValueError("200-box scene no longer authors fixed-step playback")
    scene_seed = scene_data.get("simulation", {}).get("seed")
    if not isinstance(scene_seed, int) or scene_seed <= 0:
        raise ValueError(f"200-box scene seed is not pinned: {scene_seed!r}")
    if int(final["predictionSourceSolverHash"]) == 0:
        raise ValueError("prediction source solver seed hash is empty")

    # Worker completion may publish the internal trajectory records in a
    # different diagnostic order. V2 deliberately excluded that fingerprint:
    # renderer-facing record order is already covered by every raw submission
    # hash/count in visualTicks, where reordering is a real visual divergence.
    runtime_fields = (
        "cameraMode",
        "replayPredictionEnabled",
        "predictionHorizonSeconds",
        "predictionBuildMode",
        "predictionPendingLatestRestart",
        "predictionSupersededRestartCount",
        "predictionLatestRestartBeginCount",
        "replayPathTarget",
        "replayPathTargetCount",
        "predictionSourceSolverHash",
        "predictionActiveFrameCount",
        "predictionFrameCount",
        "predictionBuildFrameCount",
        "predictionTrajectoryFingerprintReady",
        "predictionTrajectoryRecordCount",
        "predictionTrajectoryPointCount",
        "predictionFutureNodeCount",
        "predictionAuthoredWallBrickCount",
        "predictionAffectedWallBrickCount",
        "predictionMovedWallBrickCount",
        "predictionFutureNodeBuildFrameCount",
        "predictionRetainedEntryMarkerCount",
        "predictionRetainedRestMarkerCount",
        "predictionRetainedHorizonMarkerCount",
    )
    return {
        "inputs": {
            "scene": str(report.get("scene", "")).replace("\\", "/"),
            "script": str(report.get("script", "")).replace("\\", "/"),
            "sceneSha256": sha256(SCENE),
            "scriptSha256": sha256(SCRIPT),
            "configSha256": sha256(CONFIG),
            "shadersSha256": shader_tree_sha256(),
            "sceneSeed": scene_seed,
        },
        "horizon": {
            "fixedStep": True,
            "startFrame": start_frame,
            "tickCount": len(ticks),
            "lastReveal": ticks[-1]["revealFrame"],
            "seconds": final["predictionHorizonSeconds"],
        },
        "workerAndReserve": {
            "workerComplete": worker_complete,
            "reserveGrowthDelta": reserve_end - reserve_start,
            "steadyStateNoReserveGrowth": final["predictionTrajectorySteadyStateNoReserveGrowth"],
        },
        "runtime": {field: final[field] for field in runtime_fields},
        "revealMapping": reveal_mapping,
        "visualTicks": ticks,
        "causal": causal,
        "artifact": artifact,
    }


def run_determinism_negative_controls(
    expected: dict[str, Any], actual: dict[str, Any]
) -> bool:
    controls = (
        ("seed-mismatch", "inputs.sceneSeed"),
        ("missing-tick", "revealMapping[1200].revealFrame"),
        ("event-mutation", "artifact.events[0].kind"),
        ("non-fixed-step", "artifact.frameHeaders[0].fixedStep"),
        ("truncated-horizon", "horizon.tickCount"),
        ("record-reordering", "visualTicks[100].sceneFrame"),
        ("vertex-byte-change", "visualTicks[1200].ordinaryVertexBytes"),
        ("dropped-geometry", "visualTicks[1200].segmentCount"),
        ("reserve-growth", "workerAndReserve.reserveGrowthDelta"),
    )
    for name, expected_path in controls:
        mutated = copy.deepcopy(actual)
        if name == "seed-mismatch":
            mutated["inputs"]["sceneSeed"] += 1
        elif name == "missing-tick":
            mutated["revealMapping"][1200]["revealFrame"] += 1
        elif name == "event-mutation":
            if not mutated["artifact"]["events"]:
                print("FAIL determinism event control has no event to mutate")
                return False
            mutated["artifact"]["events"][0]["kind"] += 1
        elif name == "non-fixed-step":
            mutated["artifact"]["frameHeaders"][0]["fixedStep"] = False
        elif name == "truncated-horizon":
            mutated["horizon"]["tickCount"] -= 1
            mutated["visualTicks"].pop()
        elif name == "record-reordering":
            mutated["visualTicks"][100], mutated["visualTicks"][101] = (
                mutated["visualTicks"][101],
                mutated["visualTicks"][100],
            )
        elif name == "vertex-byte-change":
            mutated["visualTicks"][1200]["ordinaryVertexBytes"] += 4
        elif name == "dropped-geometry":
            mutated["visualTicks"][1200]["segmentCount"] -= 1
        elif name == "reserve-growth":
            mutated["workerAndReserve"]["reserveGrowthDelta"] += 1

        difference = first_difference(expected, mutated, "determinism")
        if not difference or expected_path not in difference:
            print(
                f"FAIL determinism control {name} missed injected field: "
                f"expected_path={expected_path} difference={difference}"
            )
            return False
        print(f"PASS determinism control {name}: {difference}")
    return True


def causal_comparable(report: dict[str, Any]) -> dict[str, Any]:
    causal = validate_causal_shape(report)
    tick_fields = (
        "revealFrame",
        "activeTopologyHash",
        "activeNodeCount",
        "revealedRecordCount",
        "revealedPointCount",
        "revealedSegmentCount",
        "entryMarkerCount",
        "restMarkerCount",
        "horizonMarkerCount",
        "ghostRequestCount",
    )
    return {
        "targetId": causal["targetId"],
        "topologyCount": causal["topologyCount"],
        "topology": causal["topology"],
        "tickCount": causal["tickCount"],
        # Production submission hashes remain in the V0 manifest. V2 keeps only
        # stable causal transitions here; internal trajectory record publication
        # order is not a renderer-facing contract.
        "ticks": [
            {field: tick[field] for field in tick_fields}
            for tick in causal["ticks"]
        ],
    }


def causal_baseline_payload(
    report: dict[str, Any], working_base_commit: str, configuration: str
) -> dict[str, Any]:
    comparable = causal_comparable(report)
    return {
        "format": "skullbonez.replay-visual-fidelity-causal.json",
        "schemaVersion": 1,
        "workingBaseCommit": working_base_commit,
        "captureCommit": git_head(),
        "configuration": configuration,
        "fixedStep": True,
        "target": "prediction_striker_ball",
        "horizonSeconds": 20.0,
        "visualBaselineSha256": sha256(DEFAULT_BASELINE),
        **comparable,
    }


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
    parser.add_argument("--causal-baseline", type=Path, default=DEFAULT_CAUSAL_BASELINE)
    parser.add_argument("--approve-baseline", action="store_true")
    parser.add_argument("--approve-causal-baseline", action="store_true")
    parser.add_argument("--working-base-commit", default="6a6ab4c65")
    parser.add_argument("--configuration", choices=("Debug", "Profile"), default="Debug")
    parser.add_argument("--negative-control", action="store_true")
    parser.add_argument("--incomplete-control", action="store_true")
    parser.add_argument("--causal-activation-control", action="store_true")
    parser.add_argument("--causal-topology-control", action="store_true")
    parser.add_argument("--causal-segment-control", action="store_true")
    parser.add_argument("--compare-report", type=Path)
    parser.add_argument("--run-determinism-controls", action="store_true")
    args = parser.parse_args()

    if args.run_determinism_controls and not args.compare_report:
        parser.error("--run-determinism-controls requires --compare-report")

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

    if args.approve_causal_baseline:
        # Bootstrap only: the diagnostic that proved V2 before the committed
        # schema field existed still contains the same target as the unique
        # topology parent outside the node set. Normal validation below requires
        # the explicit targetId emitted by the runtime report.
        causal = report.get("replayCausalProof", {})
        if not causal.get("targetId"):
            topology = causal.get("topology", [])
            node_ids = {node.get("id") for node in topology}
            root_parents = {
                node.get("parentId")
                for node in topology
                if node.get("parentId") not in node_ids
            }
            if len(root_parents) == 1:
                causal["targetId"] = root_parents.pop()
        try:
            payload = causal_baseline_payload(
                report, args.working_base_commit, args.configuration
            )
        except ValueError as error:
            print(f"FAIL replay causal proof report: {error}")
            return 1
        args.causal_baseline.parent.mkdir(parents=True, exist_ok=True)
        with args.causal_baseline.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(payload, stream, indent=2)
            stream.write("\n")
        print(
            f"APPROVED replay causal baseline: ticks={payload['tickCount']} "
            f"topology_nodes={payload['topologyCount']} path={args.causal_baseline}"
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


    causal_control_requested = (
        args.causal_activation_control
        or args.causal_topology_control
        or args.causal_segment_control
    )
    causal_baseline = load_json(args.causal_baseline)
    if causal_baseline.get("visualBaselineSha256") != sha256(args.baseline):
        print(
            "FAIL replay causal baseline is not bound to the current visual baseline: "
            f"expected={sha256(args.baseline)} "
            f"actual={causal_baseline.get('visualBaselineSha256')}"
        )
        return 1
    try:
        actual_causal = causal_comparable(report)
    except ValueError as error:
        print(f"FAIL replay causal proof report: {error}")
        return 1
    expected_causal = {
        "targetId": causal_baseline["targetId"],
        "topologyCount": causal_baseline["topologyCount"],
        "topology": causal_baseline["topology"],
        "tickCount": causal_baseline["tickCount"],
        "ticks": causal_baseline["ticks"],
    }
    actual_causal = copy.deepcopy(actual_causal)
    expected_control_path = ""
    if args.causal_activation_control:
        topology_index = min(
            range(len(actual_causal["topology"])),
            key=lambda index: actual_causal["topology"][index]["firstFrame"],
        )
        actual_causal["topology"][topology_index]["firstFrame"] += 1
        expected_control_path = f"topology[{topology_index}].firstFrame"
    elif args.causal_topology_control:
        actual_causal["topology"][0]["parentId"] += 1
        actual_causal["topology"][0]["depth"] += 1
        expected_control_path = "topology[0].parentId"
    elif args.causal_segment_control:
        first_activation = min(node["firstFrame"] for node in actual_causal["topology"])
        actual_causal["ticks"][first_activation]["revealedSegmentCount"] -= 1
        expected_control_path = f"ticks[{first_activation}].revealedSegmentCount"

    causal_difference = first_difference(expected_causal, actual_causal, "causal")
    if causal_control_requested:
        if causal_difference and expected_control_path in causal_difference:
            print(f"PASS causal negative control detected first divergence: {causal_difference}")
            return 0
        print(
            "FAIL causal negative control was not detected at the injected field: "
            f"expected_path={expected_control_path} difference={causal_difference}"
        )
        return 1
    if causal_difference:
        print(f"FAIL replay causal proof first divergence: {causal_difference}")
        return 1

    try:
        artifact = validate_artifact_roundtrip(report)
    except ValueError as error:
        print(f"FAIL replay artifact round-trip: {error}")
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
    if args.compare_report:
        try:
            expected_determinism = determinism_contract(report)
            actual_determinism = determinism_contract(load_json(args.compare_report))
        except (OSError, ReplayQueryError, ValueError) as error:
            print(f"FAIL replay cross-process determinism input: {error}")
            return 1
        if args.run_determinism_controls:
            return 0 if run_determinism_negative_controls(
                expected_determinism, actual_determinism
            ) else 1
        determinism_difference = first_difference(
            expected_determinism, actual_determinism, "determinism"
        )
        if determinism_difference:
            print(
                "FAIL replay cross-process determinism first divergence: "
                f"{determinism_difference}"
            )
            return 1
        print(
            "PASS replay cross-process determinism: "
            f"ticks={len(expected_determinism['visualTicks'])} "
            f"saved_frames={len(expected_determinism['artifact']['frameHeaders'])} "
            f"event_cursors={len(expected_determinism['artifact']['eventCursors'])}"
        )
        return 0
    print(
        f"PASS replay visual fidelity: ticks={actual['tickCount']} "
        f"moved_wall_bricks={actual['finalState']['predictionMovedWallBrickCount']} "
        f"causal_nodes={actual_causal['topologyCount']} "
        f"predicted_live_ticks={report['replayCausalProof']['predictedLiveTickCount']} "
        f"saved_loaded_ticks={artifact['sampleCount']} "
        f"first_reveal={actual['ticks'][0]['revealFrame']} "
        f"last_reveal={actual['ticks'][-1]['revealFrame']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
