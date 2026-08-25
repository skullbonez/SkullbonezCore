"""File: tools/check_replay_visual_fidelity.py
Purpose:
  Turns the Automation interaction report into a bounded, immutable golden contract
  for every deterministic prediction reveal frame.

Summary:
  One hidden engine process records the approved prediction once. This checker
  compares that report with the golden and inspects the saved artifact bytes.
  It never launches a second engine or presents reconstructed visuals.

Glossary:
  Reveal tick: One future-frame packet exposed by the prediction cascade.
  Causal baseline: Approved topology and activation order for the wall cascade.
  Packet hash: Deterministic digest of the exact ordered body data rendered for
    a predicted frame.
  Determinism contract: Ordered report/artifact projection that excludes only
    measured wall-clock throughput and retains every simulation/visual input.

Invariants:
  - Ordinary validation never updates the baseline; only an explicit cold
    write command may replace it.
  - A Physics-plan replacement additionally requires the exact candidate hash
    and an append-only retained-runtime transition manifest.
  - Config provenance prefers the approved raw hash; line-ending drift passes
    only when normalized bytes equal the config in the recorded capture commit.
  - Reveal rows are contiguous ReplayFrameIndex values 0 through 2400.
  - All 200 authored wall bricks participate, every causal path reveals, and
    more than half the wall is grounded and sleeping throughout the final second.
  - High-detail evidence either retains the complete horizon below its 320 MiB
    bank ceiling or reports one internally consistent bounded prefix.
  - The first differing field is reported, not merely a whole-file hash.
  - This checker is read-only and cannot start a second prediction generation.

The explicit --approve-baseline lane remains the cold non-Physics owner action.
Physics plans add --physics-automated-override-sha256 and
--physics-artifact-manifest; the validation batch never supplies write flags.

Related:
  - tools/validate_replay_visual_fidelity.bat
  - tools/replay_query.py
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path
from typing import Any

from replay_query import ReplayQueryError, ReplayV2, VISUAL_PACKET_RECORD
from check_physics_baseline_guard import (
    GuardFailure,
    sha256_bytes,
    validate_physics_plan_transition,
)


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
EXPECTED_HORIZON_SECONDS = 20.0
EXPECTED_TICKS = 2401
EXPECTED_LAST_REVEAL = 2400
EXPECTED_WALL_BRICKS = 200
EXPECTED_MIN_TOPPLED_WALL_BRICKS = EXPECTED_WALL_BRICKS // 2 + 1

# The shape guard proves a majority settled during the final second. The exact
# approved outcome remains pinned separately by the golden's finalState.
EXPECTED_MIN_SETTLED_WALL_BRICKS = EXPECTED_WALL_BRICKS // 2
EXPECTED_START_FRAME = 900
EXPECTED_EVIDENCE_BANK_HARD_BYTES = 320 * 1024 * 1024
NEGATIVE_CONTROL_TICK = 1200
REPLAY_VISUAL_FNV_OFFSET = 1469598103934665603
REPLAY_VISUAL_FNV_PRIME = 1099511628211


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def serialized_baseline(payload: dict[str, Any]) -> bytes:
    return (json.dumps(payload, indent=2) + "\n").encode("utf-8")


def validate_physics_automated_override(
    args: argparse.Namespace,
    baseline_path: Path,
    payload: dict[str, Any],
) -> bytes:
    data = serialized_baseline(payload)
    supplied = args.physics_automated_override_sha256
    if supplied is None:
        return data
    actual = sha256_bytes(data)
    if supplied.lower() != actual:
        raise ValueError(
            "Physics automated-override SHA-256 does not match the exact serialized candidate: "
            f"expected={actual} supplied={supplied.lower()}"
        )
    resolved_baseline = baseline_path.resolve()
    try:
        relative_baseline = resolved_baseline.relative_to(ROOT.resolve()).as_posix()
    except ValueError as exc:
        raise ValueError("Physics automated override baseline must stay inside the repository") from exc
    if not resolved_baseline.is_file():
        raise ValueError("Physics automated override requires a tracked predecessor baseline")
    producing_executable = ROOT / args.configuration / "SKULLBONEZ_CORE.exe"
    try:
        validate_physics_plan_transition(
            ROOT,
            args.physics_artifact_manifest,
            relative_baseline,
            sha256(resolved_baseline),
            actual,
            producing_executable,
            f"{args.configuration}|x64",
        )
    except GuardFailure as exc:
        raise ValueError(str(exc)) from exc
    return data


def write_baseline_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


def normalized_text_bytes(data: bytes) -> bytes:
    """Canonicalize line endings without changing any other config byte."""
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def config_matches_approved_capture(baseline: dict[str, Any]) -> bool:
    if baseline.get("configSha256") == sha256(CONFIG):
        return True

    capture_commit = baseline.get("captureCommit")
    if not isinstance(capture_commit, str) or not capture_commit:
        return False

    try:
        approved = subprocess.run(
            ["git", "show", f"{capture_commit}:SkullbonezData/engine.cfg"],
            cwd=ROOT,
            check=True,
            capture_output=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return False

    # Why: Git tracks this file as LF, but historical Windows worktrees could
    # retain CRLF or mixed endings. The capture commit remains the independent
    # semantic oracle; whitespace and value changes still fail byte-for-byte.
    return normalized_text_bytes(CONFIG.read_bytes()) == normalized_text_bytes(approved)


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


def replay_visual_byte_hash(payload: bytes) -> int:
    value = REPLAY_VISUAL_FNV_OFFSET
    for byte in payload:
        value ^= byte
        value = (value * REPLAY_VISUAL_FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def canonical_archive_semantic_hash(
    visual_state_hash: int,
    exact_packet_hash: int,
    topology_version: int,
    replay_reserve_growth_events: int,
) -> int:
    """Mirror the RVIS writer's content-sensitive canonical semantic digest."""
    value = visual_state_hash
    payload = struct.pack(
        "<IQQ",
        topology_version,
        replay_reserve_growth_events,
        exact_packet_hash,
    )
    for byte in payload:
        value ^= byte
        value = (value * REPLAY_VISUAL_FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def validate_settled_wall_brick_count(final: dict[str, Any]) -> None:
    actual = final.get("predictionSettledWallBrickCount", 0)
    if actual < EXPECTED_MIN_SETTLED_WALL_BRICKS:
        raise ValueError(
            "too little of the wall was settled throughout the final prediction second: "
            f"expected_at_least={EXPECTED_MIN_SETTLED_WALL_BRICKS} actual={actual}"
        )


def validate_report_shape(report: dict[str, Any]) -> list[dict[str, Any]]:
    if not report.get("ok"):
        raise ValueError(f"interaction report failed: {report.get('failure', 'unknown failure')}")
    fidelity = report.get("replayVisualFidelity", {})
    if fidelity.get("schemaVersion") != 2:
        raise ValueError(f"packet schema mismatch: expected=2 actual={fidelity.get('schemaVersion')}")
    if fidelity.get("offlineProjectionComplete") is not True:
        raise ValueError("RVPD was not projected through the non-presenting production visual path")
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
        raise ValueError(
            f"prediction horizon ended before ReplayFrameIndex {EXPECTED_LAST_REVEAL}"
        )
    final = report.get("finalState", {})
    if final.get("predictionAuthoredWallBrickCount") != EXPECTED_WALL_BRICKS:
        raise ValueError(
            "authored wall changed: "
            f"expected={EXPECTED_WALL_BRICKS} actual={final.get('predictionAuthoredWallBrickCount')}"
        )
    if final.get("predictionMovedWallBrickCount") != EXPECTED_WALL_BRICKS:
        raise ValueError(
            "not every wall brick participated within the prediction horizon: "
            f"expected_moved={EXPECTED_WALL_BRICKS} actual={final.get('predictionMovedWallBrickCount')}"
        )
    if final.get("predictionToppledWallBrickCount", 0) < EXPECTED_MIN_TOPPLED_WALL_BRICKS:
        raise ValueError(
            "fewer than half the wall bricks finished grounded and sleeping: "
            f"expected_at_least={EXPECTED_MIN_TOPPLED_WALL_BRICKS} "
            f"actual={final.get('predictionToppledWallBrickCount')}"
        )
    if final.get("predictionSustainedToppledWallBrickCount", 0) < EXPECTED_MIN_TOPPLED_WALL_BRICKS:
        raise ValueError(
            "fewer than half the wall bricks stayed grounded and sleeping for the final second: "
            f"expected_at_least={EXPECTED_MIN_TOPPLED_WALL_BRICKS} "
            f"actual={final.get('predictionSustainedToppledWallBrickCount')}"
        )
    validate_settled_wall_brick_count(final)
    if final.get("predictionGenerationCount") != 1:
        raise ValueError(
            "visual gate must contain exactly one prediction generation: "
            f"actual={final.get('predictionGenerationCount')}"
        )
    evidence_truncated = final.get("predictionEvidenceCapacityTruncated") is True
    evidence_truncation_count = final.get("predictionEvidenceCapacityTruncationCount")
    evidence_first_truncated_frame = final.get("predictionEvidenceFirstTruncatedFrame")
    evidence_committed_frames = final.get("predictionEvidenceCommittedPublishedFrameCount")
    if evidence_truncated:
        evidence_shape_valid = (
            evidence_truncation_count == 1
            and isinstance(evidence_first_truncated_frame, int)
            and 0 < evidence_first_truncated_frame < EXPECTED_TICKS
            and evidence_committed_frames == evidence_first_truncated_frame
        )
    else:
        # Invariant: improved settling may keep the complete dense evidence bank
        # below its cap. Zero remains the sentinel for "no first truncated frame."
        evidence_shape_valid = (
            evidence_truncation_count == 0
            and evidence_first_truncated_frame == 0
            and evidence_committed_frames == EXPECTED_TICKS
        )
    if not evidence_shape_valid:
        raise ValueError(
            "bounded prediction evidence is neither a complete horizon nor one exact prefix: "
            f"truncated={evidence_truncated} count={evidence_truncation_count} "
            f"first_frame={evidence_first_truncated_frame} "
            f"committed_frames={evidence_committed_frames}"
        )
    if final.get("predictionEvidenceCurrentCapacityBytes", EXPECTED_EVIDENCE_BANK_HARD_BYTES + 1) > EXPECTED_EVIDENCE_BANK_HARD_BYTES:
        raise ValueError(
            "prediction evidence exceeded its 320 MiB bank ceiling: "
            f"actual={final.get('predictionEvidenceCurrentCapacityBytes')}"
        )
    if (
        final.get("predictionEvidenceConsumerActive") is not False
        or final.get("predictionEvidenceConsumerAcquireCount") != 1
        or final.get("predictionEvidenceConsumerReleaseCount") != 1
    ):
        raise ValueError(
            "prediction evidence did not release its Physics consumer once: "
            f"active={final.get('predictionEvidenceConsumerActive')} "
            f"acquires={final.get('predictionEvidenceConsumerAcquireCount')} "
            f"releases={final.get('predictionEvidenceConsumerReleaseCount')}"
        )
    for index, tick in enumerate(ticks):
        for field in (
            "sourceFrame",
            "semanticHash",
            "headerStateHash",
            "trajectoryStateHash",
            "topologyStateHash",
            "markerStateHash",
            "ghostStateHash",
            "visualStateHash",
            "exactPacketHash",
            "schemaVersion",
            "targetId",
            "branchId",
            "eventCursor",
            "topologyVersion",
            "publishedFrameCount",
            "predictionEnabled",
            "predictionBuilding",
            "predictionComplete",
            "cameraEye",
            "cameraUp",
            "hasGeometry",
            "combinedLineHash",
            "combinedLineBytes",
            "combinedLineVertexCount",
        ):
            if field not in tick:
                raise ValueError(f"ticks[{index}] omitted complete-packet field {field}")
        if int(tick["sourceFrame"]) <= 0:
            raise ValueError(f"ticks[{index}].sourceFrame is invalid: {tick['sourceFrame']!r}")
        if tick["schemaVersion"] != 1 or int(tick["targetId"]) <= 0:
            raise ValueError(
                f"ticks[{index}] has invalid packet identity: "
                f"schema={tick['schemaVersion']!r} target={tick['targetId']!r}"
            )
        # `publishedFrameCount` is the worker's immutable completed-frame
        # publication count, not the reveal cursor. Every row is deliberately
        # captured after one complete build; revealFrame carries the visible
        # inclusive prefix independently.
        if tick["publishedFrameCount"] != EXPECTED_TICKS:
            raise ValueError(
                f"ticks[{index}].publishedFrameCount is not the completed horizon: "
                f"expected={EXPECTED_TICKS} actual={tick['publishedFrameCount']!r}"
            )
        if not tick["predictionEnabled"] or tick["predictionBuilding"] or not tick["predictionComplete"]:
            raise ValueError(
                f"ticks[{index}] was not captured from one completed prediction: "
                f"enabled={tick['predictionEnabled']!r} "
                f"building={tick['predictionBuilding']!r} complete={tick['predictionComplete']!r}"
            )
        if (
            tick["semanticHash"] == "0x0000000000000000"
            or tick["visualStateHash"] == "0x0000000000000000"
            or tick["exactPacketHash"] == "0x0000000000000000"
        ):
            raise ValueError(f"ticks[{index}] has an empty complete-packet fingerprint")
    if not final.get("predictionTrajectoryFingerprintReady"):
        raise ValueError("trajectory fingerprint is empty")
    return ticks


def visual_ticks(ticks: list[dict[str, Any]]) -> list[dict[str, Any]]:
    # ReplayFrameIndex is the binding key. The fixed presentation start makes
    # sceneFrame redundant. Absolute topology-cache versions, reserve growth,
    # and the semantic diagnostic hash are process telemetry; exact topology
    # content, visual-state hash, and every raw/canonical buffer fact remain.
    # Reordering submitted geometry is a visual change even if a canonical
    # diagnostic hash still matches.
    excluded = {
        "sceneFrame",
        "semanticHash",
        "topologyVersion",
        "replayReserveGrowthEvents",
    }
    return [
        {key: value for key, value in tick.items() if key not in excluded}
        for tick in ticks
    ]


def validate_causal_shape(report: dict[str, Any]) -> dict[str, Any]:
    causal = report.get("replayCausalProof", {})
    if causal.get("schemaVersion") != 2:
        raise ValueError(
            f"causal schema mismatch: expected=2 actual={causal.get('schemaVersion')}"
        )
    if not causal.get("singleRevealGeneration"):
        raise ValueError("causal proof did not certify one reveal generation")
    if not causal.get("singlePresentedCascade"):
        raise ValueError("causal proof did not certify one presented cascade")

    ticks = causal.get("ticks", [])
    topology = causal.get("topology", [])
    if len(ticks) != EXPECTED_TICKS or causal.get("tickCount") != EXPECTED_TICKS:
        raise ValueError(
            f"causal horizon incomplete: expected_ticks={EXPECTED_TICKS} actual_ticks={len(ticks)}"
        )
    if not topology or causal.get("topologyCount") != len(topology):
        raise ValueError("causal topology is empty or count-mismatched")

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

    visual_rows = report.get("replayVisualFidelity", {}).get("ticks", [])
    source_frame = visual_rows[0].get("sourceFrame") if visual_rows else None
    if not isinstance(source_frame, int) or source_frame <= 0:
        raise ValueError(f"visual source frame is invalid: {source_frame!r}")
    for index, visual_row in enumerate(visual_rows):
        if visual_row.get("sourceFrame") != source_frame or visual_row.get("revealFrame") != index:
            raise ValueError(
                f"visual source/reveal identity drifted at index={index}: "
                f"source={visual_row.get('sourceFrame')} reveal={visual_row.get('revealFrame')}"
            )
    return causal


def validate_artifact_roundtrip(report: dict[str, Any]) -> dict[str, Any]:
    artifact_report = report.get("replayArtifact", {})
    if artifact_report.get("schemaVersion") != 4 or not artifact_report.get("saved"):
        raise ValueError("replay artifact report schema v4 was not saved by the fidelity probe")
    path = Path(str(artifact_report.get("path", "")))
    if not path.is_absolute():
        path = ROOT / path
    if not path.is_file():
        raise ValueError(f"current durable replay artifact is missing: {path}")

    try:
        artifact = ReplayV2(path)
        packet_hashes = artifact.presentation_packet_hashes()
    except ReplayQueryError as error:
        raise ValueError(f"current durable replay artifact is invalid: {error}") from error
    if artifact.version != 5 or artifact.manifest.get("version") != 5:
        raise ValueError(
            "durable replay artifact version mismatch: "
            f"header={artifact.version} manifest={artifact.manifest.get('version')}"
        )
    if artifact.manifest.get("bodyPoseBytes") != 76:
        raise ValueError("v5 artifact did not retain the full 76-byte visual body state")
    if len(packet_hashes) != artifact_report.get("sampleCount"):
        raise ValueError(
            "v5 artifact sample count mismatch: "
            f"report={artifact_report.get('sampleCount')} loaded={len(packet_hashes)}"
        )

    if not packet_hashes:
        raise ValueError("v5 artifact omitted its retained presentation samples")
    visual_ticks_report = report.get("replayVisualFidelity", {}).get("ticks", [])
    if len(artifact.visual_packets) != EXPECTED_TICKS or artifact_report.get("visualPacketCount") != EXPECTED_TICKS:
        raise ValueError(
            "v5 artifact visual-packet horizon mismatch: "
            f"expected={EXPECTED_TICKS} loaded={len(artifact.visual_packets)} "
            f"reported={artifact_report.get('visualPacketCount')}"
        )
    prediction_chunk = artifact.chunks.get("RVPD")
    if not prediction_chunk or prediction_chunk.record_count != 1 or prediction_chunk.size <= 8:
        raise ValueError("v5 artifact omitted its durable typed prediction-state chunk")
    if artifact.manifest.get("visualPredictionBytes") != prediction_chunk.size:
        raise ValueError(
            "v5 artifact prediction-state manifest mismatch: "
            f"manifest={artifact.manifest.get('visualPredictionBytes')} chunk={prediction_chunk.size}"
        )
    prediction_hash = replay_visual_byte_hash(artifact._chunk_bytes("RVPD"))
    manifest_prediction_hash = artifact.manifest.get("visualPredictionHash")
    report_prediction_hash = artifact_report.get("visualPredictionHash")
    if manifest_prediction_hash != prediction_hash or report_prediction_hash != f"0x{prediction_hash:016X}":
        raise ValueError(
            "v5 artifact prediction-state hash mismatch: "
            f"manifest={manifest_prediction_hash} report={report_prediction_hash} "
            f"actual=0x{prediction_hash:016X}"
        )
    if artifact.manifest.get("visualPacketEntryBytes") != VISUAL_PACKET_RECORD.size:
        raise ValueError(
            "v5 artifact visual-packet row size mismatch: "
            f"manifest={artifact.manifest.get('visualPacketEntryBytes')} "
            f"reader={VISUAL_PACKET_RECORD.size}"
        )
    visual_fields = (
        ("source_frame", "sourceFrame"),
        ("reveal_frame", "revealFrame"),
        ("visual_state_hash", "visualStateHash"),
        ("exact_packet_hash", "exactPacketHash"),
        ("schema_version", "schemaVersion"),
        ("target_id", "targetId"),
        ("branch_id", "branchId"),
        ("event_cursor", "eventCursor"),
        ("published_frame_count", "publishedFrameCount"),
        ("prediction_enabled", "predictionEnabled"),
        ("prediction_building", "predictionBuilding"),
        ("prediction_complete", "predictionComplete"),
        ("combined_line_hash", "combinedLineHash"),
        ("ordinary_line_hash", "ordinaryLineHash"),
        ("priority_line_hash", "priorityLineHash"),
        ("priority_line_canonical_hash", "priorityLineCanonicalHash"),
        ("ordinary_ribbon_hash", "ordinaryRibbonHash"),
        ("priority_ribbon_hash", "priorityRibbonHash"),
        ("priority_ribbon_canonical_hash", "priorityRibbonCanonicalHash"),
        ("expanded_vertex_hash", "vertexHash"),
        ("ordinary_expanded_vertex_hash", "ordinaryVertexHash"),
        ("dropped_segment_count", "droppedSegmentCount"),
        ("combined_line_bytes", "combinedLineBytes"),
        ("ordinary_line_bytes", "ordinaryLineBytes"),
        ("priority_line_bytes", "priorityLineBytes"),
        ("ordinary_ribbon_bytes", "ordinaryRibbonBytes"),
        ("priority_ribbon_bytes", "priorityRibbonBytes"),
        ("expanded_vertex_bytes", "vertexBytes"),
        ("ordinary_expanded_vertex_bytes", "ordinaryVertexBytes"),
        ("has_geometry", "hasGeometry"),
        ("trajectory_record_count", "trajectoryRecordCount"),
        ("future_node_count", "futureNodeCount"),
        ("retained_marker_count", "retainedMarkerCount"),
        ("ghost_request_count", "ghostRequestCount"),
        ("combined_line_vertex_count", "combinedLineVertexCount"),
        ("ordinary_line_vertex_count", "ordinaryLineVertexCount"),
        ("priority_line_vertex_count", "priorityLineVertexCount"),
        ("ordinary_ribbon_segment_count", "ordinaryRibbonSegmentCount"),
        ("priority_ribbon_segment_count", "priorityRibbonSegmentCount"),
        ("expanded_vertex_count", "vertexCount"),
        ("ordinary_expanded_vertex_count", "ordinaryVertexCount"),
        ("segment_count", "segmentCount"),
    )
    hash_report_fields = {
        "semanticHash",
        "visualStateHash",
        "exactPacketHash",
        "combinedLineHash",
        "ordinaryLineHash",
        "priorityLineHash",
        "priorityLineCanonicalHash",
        "ordinaryRibbonHash",
        "priorityRibbonHash",
        "priorityRibbonCanonicalHash",
        "vertexHash",
        "ordinaryVertexHash",
    }
    published_topology_versions: list[int] = []
    for index, (saved_packet, report_tick) in enumerate(zip(artifact.visual_packets, visual_ticks_report)):
        live_topology_version = int(report_tick.get("topologyVersion", 0))
        if live_topology_version == 0:
            canonical_topology_version = 0
        elif live_topology_version in published_topology_versions:
            canonical_topology_version = published_topology_versions.index(live_topology_version) + 1
        else:
            published_topology_versions.append(live_topology_version)
            canonical_topology_version = len(published_topology_versions)
        # R4b keeps live report telemetry raw while the durable row carries an
        # explicit first-publication topology token, zero reserve telemetry,
        # and a content-sensitive semantic digest of canonical row values.
        # Verify them directly; omitting these fields from the raw report
        # equality loop must not make them unchecked.
        expected_semantic_hash = canonical_archive_semantic_hash(
            int(report_tick["visualStateHash"], 16),
            int(report_tick["exactPacketHash"], 16),
            canonical_topology_version,
            0,
        )
        canonical_bookkeeping = {
            "semanticHash": (saved_packet.semantic_hash, expected_semantic_hash),
            "topologyVersion": (saved_packet.topology_version, canonical_topology_version),
            "replayReserveGrowthEvents": (saved_packet.replay_reserve_growth_events, 0),
        }
        for field, (saved_value, expected_value) in canonical_bookkeeping.items():
            if saved_value != expected_value:
                raise ValueError(
                    f"saved/load canonical bookkeeping divergence at ticks[{index}].{field}: "
                    f"expected={expected_value!r} actual={saved_value!r}"
                )
        archived_camera = (
            saved_packet.camera_eye_x,
            saved_packet.camera_eye_y,
            saved_packet.camera_eye_z,
            saved_packet.camera_up_x,
            saved_packet.camera_up_y,
            saved_packet.camera_up_z,
        )
        report_camera = tuple(report_tick.get("cameraEye", [])) + tuple(report_tick.get("cameraUp", []))
        if len(report_camera) != 6 or any(
            struct.pack("<f", float(saved)) != struct.pack("<f", float(reported))
            for saved, reported in zip(archived_camera, report_camera)
        ):
            raise ValueError(
                f"saved/load visual packet divergence at ticks[{index}].camera: "
                f"expected={report_camera!r} actual={archived_camera!r}"
            )
        for saved_field, report_field in visual_fields:
            saved_value = getattr(saved_packet, saved_field)
            report_value = report_tick.get(report_field)
            if report_field in hash_report_fields:
                report_value = int(str(report_value), 16)
            if saved_value != report_value:
                raise ValueError(
                    f"saved/load visual packet divergence at ticks[{index}].{report_field}: "
                    f"expected={report_value!r} actual={saved_value!r}"
                )
    return {
        "path": str(path),
        "sampleCount": len(packet_hashes),
        "firstFrame": packet_hashes[0]["frameIndex"],
        "lastFrame": packet_hashes[-1]["frameIndex"],
        "visualPacketCount": len(artifact.visual_packets),
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
        "visualPackets": [
            {
                "sourceFrame": row.source_frame,
                "revealFrame": row.reveal_frame,
                "semanticHash": f"0x{row.semantic_hash:016x}",
                "visualStateHash": f"0x{row.visual_state_hash:016x}",
                "exactPacketHash": f"0x{row.exact_packet_hash:016x}",
                "schemaVersion": row.schema_version,
                "targetId": row.target_id,
                "branchId": row.branch_id,
                "eventCursor": row.event_cursor,
                "topologyVersion": row.topology_version,
                "publishedFrameCount": row.published_frame_count,
                "predictionEnabled": bool(row.prediction_enabled),
                "predictionBuilding": bool(row.prediction_building),
                "predictionComplete": bool(row.prediction_complete),
                "cameraEye": [row.camera_eye_x, row.camera_eye_y, row.camera_eye_z],
                "cameraUp": [row.camera_up_x, row.camera_up_y, row.camera_up_z],
                "combinedLineHash": f"0x{row.combined_line_hash:016x}",
                "ordinaryLineHash": f"0x{row.ordinary_line_hash:016x}",
                "priorityLineHash": f"0x{row.priority_line_hash:016x}",
                "priorityLineCanonicalHash": f"0x{row.priority_line_canonical_hash:016x}",
                "ordinaryRibbonHash": f"0x{row.ordinary_ribbon_hash:016x}",
                "priorityRibbonHash": f"0x{row.priority_ribbon_hash:016x}",
                "priorityRibbonCanonicalHash": f"0x{row.priority_ribbon_canonical_hash:016x}",
                "vertexHash": f"0x{row.expanded_vertex_hash:016x}",
                "ordinaryVertexHash": f"0x{row.ordinary_expanded_vertex_hash:016x}",
                "droppedSegmentCount": row.dropped_segment_count,
                "replayReserveGrowthEvents": row.replay_reserve_growth_events,
                "hasGeometry": bool(row.has_geometry),
                "combinedLineBytes": row.combined_line_bytes,
                "ordinaryLineBytes": row.ordinary_line_bytes,
                "priorityLineBytes": row.priority_line_bytes,
                "ordinaryRibbonBytes": row.ordinary_ribbon_bytes,
                "priorityRibbonBytes": row.priority_ribbon_bytes,
                "vertexBytes": row.expanded_vertex_bytes,
                "ordinaryVertexBytes": row.ordinary_expanded_vertex_bytes,
                "trajectoryRecordCount": row.trajectory_record_count,
                "futureNodeCount": row.future_node_count,
                "retainedMarkerCount": row.retained_marker_count,
                "ghostRequestCount": row.ghost_request_count,
                "combinedLineVertexCount": row.combined_line_vertex_count,
                "ordinaryLineVertexCount": row.ordinary_line_vertex_count,
                "priorityLineVertexCount": row.priority_line_vertex_count,
                "ordinaryRibbonSegmentCount": row.ordinary_ribbon_segment_count,
                "priorityRibbonSegmentCount": row.priority_ribbon_segment_count,
                "vertexCount": row.expanded_vertex_count,
                "ordinaryVertexCount": row.ordinary_expanded_vertex_count,
                "segmentCount": row.segment_count,
            }
            for row in artifact.visual_packets
        ],
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
        final["replayPredictionEnabled"]
        and not final["predictionPendingLatestRestart"]
        and final["predictionSupersededRestartCount"] == 0
        and final["predictionLatestRestartBeginCount"] == 0
        and final["predictionGenerationCount"] == 1
        and final["predictionBuildFrameCount"] == 0
        and final["predictionFrameCount"] == EXPECTED_TICKS
        and causal["singleRevealGeneration"]
        and causal["singlePresentedCascade"]
    )
    if not worker_complete:
        raise ValueError("prediction worker/restart state was not quiescent after one generation")
    if not final["predictionTrajectorySteadyStateNoReserveGrowth"] or reserve_end != reserve_start:
        raise ValueError(
            f"trajectory reserve grew during proof: start={reserve_start} end={reserve_end}"
        )
    if final.get("predictionFutureTreeReadinessDropCount") != 0:
        raise ValueError(
            "live prediction child trajectories became unready after publication: "
            f"drops={final.get('predictionFutureTreeReadinessDropCount')}"
        )
    if final["predictionHorizonSeconds"] != EXPECTED_HORIZON_SECONDS:
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
        "predictionToppledWallBrickCount",
        "predictionSustainedToppledWallBrickCount",
        "predictionSettledWallBrickCount",
        "predictionGenerationCount",
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
        ("missing-tick", f"revealMapping[{NEGATIVE_CONTROL_TICK}].revealFrame"),
        ("event-mutation", "artifact.events[0].kind"),
        ("non-fixed-step", "artifact.frameHeaders[0].fixedStep"),
        ("truncated-horizon", "horizon.tickCount"),
        ("record-reordering", "visualTicks[100].sceneFrame"),
        ("vertex-byte-change", f"visualTicks[{NEGATIVE_CONTROL_TICK}].ordinaryVertexBytes"),
        ("dropped-geometry", f"visualTicks[{NEGATIVE_CONTROL_TICK}].segmentCount"),
        ("reserve-growth", "workerAndReserve.reserveGrowthDelta"),
        ("duplicate-generation", "runtime.predictionGenerationCount"),
    )
    for name, expected_path in controls:
        mutated = copy.deepcopy(actual)
        if name == "seed-mismatch":
            mutated["inputs"]["sceneSeed"] += 1
        elif name == "missing-tick":
            mutated["revealMapping"][NEGATIVE_CONTROL_TICK]["revealFrame"] += 1
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
            mutated["visualTicks"][NEGATIVE_CONTROL_TICK]["ordinaryVertexBytes"] += 4
        elif name == "dropped-geometry":
            mutated["visualTicks"][NEGATIVE_CONTROL_TICK]["segmentCount"] -= 1
        elif name == "reserve-growth":
            mutated["workerAndReserve"]["reserveGrowthDelta"] += 1
        elif name == "duplicate-generation":
            mutated["runtime"]["predictionGenerationCount"] += 1

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
    report: dict[str, Any],
    working_base_commit: str,
    configuration: str,
    visual_baseline: Path,
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
        "horizonSeconds": EXPECTED_HORIZON_SECONDS,
        "visualBaselineSha256": sha256(visual_baseline),
        **comparable,
    }


def baseline_payload(
    report: dict[str, Any], working_base_commit: str, configuration: str
) -> dict[str, Any]:
    ticks = validate_report_shape(report)
    final = report["finalState"]
    return {
        "format": "skullbonez.replay-visual-fidelity.json",
        "schemaVersion": 2,
        "workingBaseCommit": working_base_commit,
        "captureCommit": git_head(),
        "configuration": configuration,
        "fixedStep": True,
        "target": "prediction_striker_ball",
        "horizonSeconds": EXPECTED_HORIZON_SECONDS,
        "sceneSha256": sha256(SCENE),
        "scriptSha256": sha256(SCRIPT),
        "configSha256": sha256(CONFIG),
        "shadersSha256": shader_tree_sha256(),
        "tickCount": len(ticks),
        "finalState": {
            "predictionAuthoredWallBrickCount": final["predictionAuthoredWallBrickCount"],
            "predictionAffectedWallBrickCount": final["predictionAffectedWallBrickCount"],
            "predictionMovedWallBrickCount": final["predictionMovedWallBrickCount"],
            "predictionToppledWallBrickCount": final["predictionToppledWallBrickCount"],
            "predictionSustainedToppledWallBrickCount": final["predictionSustainedToppledWallBrickCount"],
            "predictionSettledWallBrickCount": final["predictionSettledWallBrickCount"],
            "predictionGenerationCount": final["predictionGenerationCount"],
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


def comparable_report(
    report: dict[str, Any], approved_tick_keys: set[str] | None = None
) -> dict[str, Any]:
    validate_report_shape(report)
    final = report["finalState"]
    ticks = visual_ticks(report["replayVisualFidelity"]["ticks"])
    if approved_tick_keys is not None:
        # The immutable V0 manifest cannot be silently refreshed. New V6 packet
        # evidence is validated above and through the artifact lane, while this
        # projection keeps the original renderer-byte oracle byte-for-byte.
        ticks = [
            {key: value for key, value in tick.items() if key in approved_tick_keys}
            for tick in ticks
        ]
    return {
        "tickCount": report["replayVisualFidelity"]["tickCount"],
        "finalState": {
            "predictionAuthoredWallBrickCount": final["predictionAuthoredWallBrickCount"],
            "predictionAffectedWallBrickCount": final["predictionAffectedWallBrickCount"],
            "predictionMovedWallBrickCount": final["predictionMovedWallBrickCount"],
            "predictionToppledWallBrickCount": final["predictionToppledWallBrickCount"],
            "predictionSustainedToppledWallBrickCount": final["predictionSustainedToppledWallBrickCount"],
            "predictionSettledWallBrickCount": final["predictionSettledWallBrickCount"],
            "predictionGenerationCount": final["predictionGenerationCount"],
            "predictionFutureNodeCount": final["predictionFutureNodeCount"],
            "predictionTrajectoryRecordCount": final["predictionTrajectoryRecordCount"],
            "predictionTrajectoryPointCount": final["predictionTrajectoryPointCount"],
        },
        "ticks": ticks,
    }


def validate_launcher_shape() -> bool:
    """Reject any mega launcher that can start a second visual engine pass."""
    launcher = ROOT / "tools/validate_replay_visual_fidelity.bat"
    executable_lines: list[tuple[int, str]] = []
    nested_scrub_lines: list[tuple[int, str]] = []
    for line_number, raw_line in enumerate(
        launcher.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        lowered = line.lower()
        if not line or lowered.startswith("rem ") or lowered.startswith("::"):
            continue
        if "skullbonez_core.exe" in lowered:
            executable_lines.append((line_number, line))
        if "validate_replay_scrub.bat" in lowered and "--prove-failure-propagation" not in lowered:
            nested_scrub_lines.append((line_number, line))

    if len(executable_lines) != 1:
        print(
            "FAIL replay launcher must contain exactly one engine command: "
            f"actual={len(executable_lines)} lines={executable_lines}"
        )
        return False
    line_number, command = executable_lines[0]
    lowered_command = command.lower()
    if "automation\\skullbonez_core.exe" not in lowered_command or "--replay-load-probe" in lowered_command:
        print(
            "FAIL replay launcher engine command is not the sole Automation generation: "
            f"line={line_number} command={command}"
        )
        return False
    if nested_scrub_lines:
        print(
            "FAIL replay launcher delegates to a normal scrub run: "
            f"lines={nested_scrub_lines}"
        )
        return False

    script = load_json(SCRIPT)
    actions = script.get("actions", [])
    capture_frames = [
        int(action["frame"])
        for action in actions
        if action.get("beginReplayVisualFidelityCapture") is True
    ]
    target_frames = [
        int(action["frame"])
        for action in actions
        if action.get("setReplayPathTarget") == "prediction_striker_ball"
    ]
    horizon_actions = [
        action for action in actions if "setReplayPredictionHorizonSeconds" in action
    ]
    prediction_frames = [
        int(action["frame"])
        for action in actions
        if action.get("clickReplayControl") == "predict"
    ]
    if not (
        len(capture_frames) == 1
        and len(target_frames) == 1
        and len(horizon_actions) == 1
        and len(prediction_frames) == 1
    ):
        print(
            "FAIL replay interaction script must arm, target, set horizon, and Predict once: "
            f"capture={capture_frames} target={target_frames} "
            f"horizon_count={len(horizon_actions)} predict={prediction_frames}"
        )
        return False
    horizon_frame = int(horizon_actions[0]["frame"])
    horizon_seconds = float(horizon_actions[0]["setReplayPredictionHorizonSeconds"])
    if horizon_seconds != EXPECTED_HORIZON_SECONDS:
        print(
            "FAIL replay interaction script horizon drifted: "
            f"expected={EXPECTED_HORIZON_SECONDS} actual={horizon_seconds}"
        )
        return False
    predict_frame = prediction_frames[0]
    if not capture_frames[0] < target_frames[0] < horizon_frame < predict_frame:
        print(
            "FAIL replay interaction setup must precede its sole prediction in order: "
            f"capture={capture_frames[0]} target={target_frames[0]} "
            f"horizon={horizon_frame} predict={predict_frame}"
        )
        return False
    dirty_after_predict = [
        action
        for action in actions
        if int(action.get("frame", -1)) > predict_frame
        and any(
            key in action
            for key in (
                "setReplayPathTarget",
                "setReplayPredictionHorizonSeconds",
                "clickReplayControl",
                "clickObject",
                "nudgeReplayPathTargetVelocity",
            )
        )
    ]
    if dirty_after_predict:
        print(
            "FAIL replay interaction script changes replay state after its sole Predict action: "
            f"actions={dirty_after_predict}"
        )
        return False
    print(
        "PASS replay launcher shape: engine_processes=1 "
        f"generation_line={line_number} prediction_starts=1 presented_cascades=1 nested_scrub_runs=0"
    )
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--causal-baseline", type=Path, default=DEFAULT_CAUSAL_BASELINE)
    parser.add_argument("--approve-baseline", action="store_true")
    parser.add_argument("--approve-causal-baseline", action="store_true")
    parser.add_argument(
        "--physics-automated-override-sha256",
        help="exact serialized candidate SHA-256 for a Physics-plan golden transition",
    )
    parser.add_argument(
        "--physics-artifact-manifest",
        type=Path,
        help="append-only Physics-plan golden-transition manifest.json",
    )
    parser.add_argument("--working-base-commit", default="6a6ab4c65")
    parser.add_argument("--configuration", choices=("Debug", "Profile", "Automation"), default="Debug")
    parser.add_argument("--negative-control", action="store_true")
    parser.add_argument("--trajectory-count-control", action="store_true")
    parser.add_argument("--incomplete-control", action="store_true")
    parser.add_argument("--causal-activation-control", action="store_true")
    parser.add_argument("--causal-topology-control", action="store_true")
    parser.add_argument("--causal-segment-control", action="store_true")
    parser.add_argument("--semantic-packet-control", action="store_true")
    parser.add_argument("--artifact-byte-control", action="store_true")
    parser.add_argument("--prediction-artifact-control", action="store_true")
    parser.add_argument("--compare-report", type=Path)
    parser.add_argument("--run-determinism-controls", action="store_true")
    parser.add_argument("--launcher-control", action="store_true")
    args = parser.parse_args()
    if (args.physics_automated_override_sha256 is None) != (
        args.physics_artifact_manifest is None
    ):
        parser.error(
            "--physics-automated-override-sha256 and --physics-artifact-manifest are required together"
        )
    if args.physics_automated_override_sha256 is not None and not (
        args.approve_baseline or args.approve_causal_baseline
    ):
        parser.error("Physics automated-override arguments require a baseline write action")

    if args.launcher_control:
        return 0 if validate_launcher_shape() else 1

    report = load_json(args.report)
    if args.approve_baseline:
        try:
            payload = baseline_payload(report, args.working_base_commit, args.configuration)
            if args.baseline.exists():
                previous = load_json(args.baseline)
                if previous.get("schemaVersion") == 1:
                    # Migration safety: schema 2 adds evidence but may not
                    # rewrite any field already approved from the working base.
                    previous_tick_keys = set(previous["ticks"][0]) if previous.get("ticks") else set()
                    legacy_actual = comparable_report(report, previous_tick_keys)
                    legacy_actual["finalState"] = {
                        field: legacy_actual["finalState"][field]
                        for field in previous["finalState"]
                    }
                    legacy_expected = {
                        "tickCount": previous["tickCount"],
                        "finalState": previous["finalState"],
                        "ticks": previous["ticks"],
                    }
                    difference = first_difference(
                        legacy_expected, legacy_actual, "schema-1-working-base"
                    )
                    if difference:
                        raise ValueError(
                            "schema-2 upgrade diverges from the approved schema-1 working base: "
                            + difference
                        )
        except ValueError as error:
            print(f"FAIL replay visual fidelity report: {error}")
            return 1
        try:
            candidate_data = validate_physics_automated_override(args, args.baseline, payload)
        except ValueError as error:
            print(f"FAIL replay visual fidelity report: {error}")
            return 1
        write_baseline_atomic(args.baseline, candidate_data)
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
                report, args.working_base_commit, args.configuration, args.baseline
            )
            if (
                args.causal_baseline.exists()
                and args.physics_automated_override_sha256 is None
            ):
                # Non-Physics owner approval remains a schema/bootstrap lane and
                # cannot rewrite behavior. Physics-plan transitions instead bind
                # an intentional causal change to exact old/new producers.
                previous = load_json(args.causal_baseline)
                legacy_expected = {
                    "targetId": previous["targetId"],
                    "topologyCount": previous["topologyCount"],
                    "topology": previous["topology"],
                    "tickCount": previous["tickCount"],
                    "ticks": previous["ticks"],
                }
                legacy_actual = {
                    "targetId": payload["targetId"],
                    "topologyCount": payload["topologyCount"],
                    "topology": payload["topology"],
                    "tickCount": payload["tickCount"],
                    "ticks": payload["ticks"],
                }
                difference = first_difference(
                    legacy_expected, legacy_actual, "causal-working-base"
                )
                if difference:
                    raise ValueError(
                        "causal approval diverges from the approved working base: "
                        + difference
                    )
        except ValueError as error:
            print(f"FAIL replay causal proof report: {error}")
            return 1
        try:
            candidate_data = validate_physics_automated_override(
                args, args.causal_baseline, payload
            )
        except ValueError as error:
            print(f"FAIL replay causal proof report: {error}")
            return 1
        write_baseline_atomic(args.causal_baseline, candidate_data)
        print(
            f"APPROVED replay causal baseline: ticks={payload['tickCount']} "
            f"topology_nodes={payload['topologyCount']} path={args.causal_baseline}"
        )
        return 0

    baseline = load_json(args.baseline)
    if args.trajectory_count_control:
        # This control exercises the immutable live-report oracle in isolation.
        # The offline archive may compact inactive scratch, but the adjacent raw
        # row count must still fail at the exact live-report field.
        expected = {
            "tickCount": baseline["tickCount"],
            "finalState": baseline["finalState"],
            "ticks": baseline["ticks"],
        }
        actual = copy.deepcopy(expected)
        approved_count = actual["ticks"][0].get("trajectoryRecordCount")
        if not isinstance(approved_count, int) or approved_count <= 0:
            print(
                "FAIL trajectory-count control requires a positive approved row count: "
                f"actual={approved_count}"
            )
            return 1
        actual["ticks"][0]["trajectoryRecordCount"] = approved_count - 1
        difference = first_difference(expected, actual)
        expected_path = "ticks[0].trajectoryRecordCount"
        if difference and expected_path in difference:
            print(f"PASS trajectory-count control detected first divergence: {difference}")
            return 0
        print(f"FAIL trajectory-count control was not detected at the injected field: {difference}")
        return 1

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

    if args.semantic_packet_control:
        original_path = replay_artifact_path(report)
        mutated_path = original_path.with_name(original_path.stem + "_semantic_control.skreplay")
        try:
            artifact_reader = ReplayV2(original_path)
            visual_chunk = artifact_reader.chunks.get("RVIS")
            if not visual_chunk:
                print("FAIL semantic packet control found no RVIS chunk")
                return 1
            mutated_bytes = bytearray(original_path.read_bytes())
            # targetId follows the five leading uint64 values and schemaVersion.
            mutation_offset = (
                visual_chunk.offset
                + 4
                + NEGATIVE_CONTROL_TICK * VISUAL_PACKET_RECORD.size
                + 44
            )
            mutated_bytes[mutation_offset] ^= 0x01
            mutated_path.write_bytes(mutated_bytes)
            mutated_report = copy.deepcopy(report)
            mutated_report["replayArtifact"]["path"] = str(mutated_path)
            try:
                validate_artifact_roundtrip(mutated_report)
            except ValueError as error:
                if f"ticks[{NEGATIVE_CONTROL_TICK}].targetId" in str(error):
                    print(f"PASS semantic packet control detected first divergence: {error}")
                    return 0
                print(f"FAIL semantic packet control reported the wrong failure: {error}")
                return 1
            print("FAIL semantic packet control was accepted")
            return 1
        finally:
            mutated_path.unlink(missing_ok=True)

    if args.artifact_byte_control:
        original_path = replay_artifact_path(report)
        mutated_path = original_path.with_name(original_path.stem + "_byte_control.skreplay")
        try:
            artifact_reader = ReplayV2(original_path)
            visual_chunk = artifact_reader.chunks.get("RVIS")
            if not visual_chunk:
                print("FAIL artifact byte control found no RVIS chunk")
                return 1
            mutated_bytes = bytearray(original_path.read_bytes())
            # ordinaryLineHash is the second digest after the camera values.
            mutation_offset = (
                visual_chunk.offset
                + 4
                + NEGATIVE_CONTROL_TICK * VISUAL_PACKET_RECORD.size
                + 108
            )
            mutated_bytes[mutation_offset] ^= 0x01
            mutated_path.write_bytes(mutated_bytes)
            mutated_report = copy.deepcopy(report)
            mutated_report["replayArtifact"]["path"] = str(mutated_path)
            try:
                validate_artifact_roundtrip(mutated_report)
            except ValueError as error:
                if f"ticks[{NEGATIVE_CONTROL_TICK}].ordinaryLineHash" in str(error):
                    print(f"PASS artifact byte control detected first divergence: {error}")
                    return 0
                print(f"FAIL artifact byte control reported the wrong failure: {error}")
                return 1
            print("FAIL artifact byte control was accepted")
            return 1
        finally:
            mutated_path.unlink(missing_ok=True)

    if args.prediction_artifact_control:
        original_path = replay_artifact_path(report)
        mutated_path = original_path.with_name(original_path.stem + "_prediction_control.skreplay")
        try:
            artifact_reader = ReplayV2(original_path)
            prediction_chunk = artifact_reader.chunks.get("RVPD")
            if not prediction_chunk or prediction_chunk.size <= 16:
                print("FAIL prediction artifact control found no usable RVPD chunk")
                return 1
            mutated_bytes = bytearray(original_path.read_bytes())
            # Offset 12 is inside the typed archive header/state payload, not
            # the artifact table. The immutable manifest hash must reject it.
            mutated_bytes[prediction_chunk.offset + 12] ^= 0x01
            mutated_path.write_bytes(mutated_bytes)
            mutated_report = copy.deepcopy(report)
            mutated_report["replayArtifact"]["path"] = str(mutated_path)
            try:
                validate_artifact_roundtrip(mutated_report)
            except ValueError as error:
                if "prediction-state hash mismatch" in str(error):
                    print(f"PASS prediction artifact control detected RVPD divergence: {error}")
                    return 0
                print(f"FAIL prediction artifact control reported the wrong failure: {error}")
                return 1
            print("FAIL prediction artifact control was accepted")
            return 1
        finally:
            mutated_path.unlink(missing_ok=True)

    try:
        artifact = validate_artifact_roundtrip(report)
    except ValueError as error:
        print(f"FAIL replay artifact round-trip: {error}")
        return 1

    if baseline.get("schemaVersion") != 2:
        print(
            "FAIL replay visual baseline schema: schema 2 is required before "
            f"validation; actual={baseline.get('schemaVersion')}"
        )
        return 1

    current_provenance = {
        "sceneSha256": sha256(SCENE),
        "scriptSha256": sha256(SCRIPT),
        "configSha256": sha256(CONFIG),
        "shadersSha256": shader_tree_sha256(),
    }
    for field, actual_hash in current_provenance.items():
        if field == "configSha256" and config_matches_approved_capture(baseline):
            continue
        if baseline.get(field) != actual_hash:
            print(
                f"FAIL replay visual fidelity provenance: {field} "
                f"expected={baseline.get(field)} actual={actual_hash}"
            )
            return 1
    try:
        approved_tick_keys = set(baseline["ticks"][0]) if baseline.get("ticks") else set()
        required_tick_keys = set(visual_ticks(validate_report_shape(report))[0])
        if approved_tick_keys != required_tick_keys:
            missing = sorted(required_tick_keys - approved_tick_keys)
            extra = sorted(approved_tick_keys - required_tick_keys)
            raise ValueError(
                f"schema-2 baseline fields mismatch: missing={missing} extra={extra}"
            )
        actual = comparable_report(report, approved_tick_keys)
    except ValueError as error:
        print(f"FAIL replay visual fidelity report: {error}")
        return 1
    expected = {
        "tickCount": baseline["tickCount"],
        "finalState": baseline["finalState"],
        "ticks": baseline["ticks"],
    }
    if args.negative_control:
        # Exercise the majority shape guard without launching another engine.
        # The authoritative report proves its current settled count still passes;
        # this adjacent value proves the first sub-majority count cannot slip by.
        below_majority = copy.deepcopy(report["finalState"])
        below_majority["predictionSettledWallBrickCount"] = (
            EXPECTED_MIN_SETTLED_WALL_BRICKS - 1
        )
        try:
            validate_settled_wall_brick_count(below_majority)
        except ValueError as error:
            expected_threshold = f"expected_at_least={EXPECTED_MIN_SETTLED_WALL_BRICKS}"
            if expected_threshold not in str(error):
                print(f"FAIL settled-majority control reported the wrong failure: {error}")
                return 1
        else:
            print("FAIL settled-majority control accepted 99 settled wall bricks")
            return 1
        try:
            validate_settled_wall_brick_count(report["finalState"])
        except ValueError as error:
            print(f"FAIL settled-majority control rejected the authoritative report: {error}")
            return 1
        print(
            "PASS settled-majority control: rejected=99 "
            f"accepted={report['finalState']['predictionSettledWallBrickCount']}"
        )
        # Negative-control lane: alter one submitted float-stream fingerprint in
        # memory and require the ordinary comparator to name that exact field.
        actual["ticks"][NEGATIVE_CONTROL_TICK]["ordinaryVertexHash"] = "0x0000000000000001"

    difference = first_difference(expected, actual)
    if args.negative_control:
        expected_path = f"ticks[{NEGATIVE_CONTROL_TICK}].ordinaryVertexHash"
        if difference and expected_path in difference:
            print(f"PASS negative control detected first divergence: {difference}")
            return 0
        print(f"FAIL negative control was not detected at the injected field: {difference}")
        return 1
    if difference:
        print(f"FAIL replay visual fidelity first divergence: {difference}")
        return 1
    if args.run_determinism_controls:
        try:
            expected_determinism = determinism_contract(report)
        except (OSError, ReplayQueryError, ValueError) as error:
            print(f"FAIL replay determinism-control input: {error}")
            return 1
        # Why: controls need an initially equal value, not a second prediction
        # run. The immutable working-base comparison above is the determinism
        # oracle; this copy only proves each injected bad value is detected.
        actual_determinism = copy.deepcopy(expected_determinism)
        return 0 if run_determinism_negative_controls(
            expected_determinism, actual_determinism
        ) else 1
    if args.compare_report:
        try:
            expected_determinism = determinism_contract(report)
            actual_determinism = determinism_contract(load_json(args.compare_report))
        except (OSError, ReplayQueryError, ValueError) as error:
            print(f"FAIL replay cross-process determinism input: {error}")
            return 1
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
        f"toppled_wall_bricks={actual['finalState']['predictionSustainedToppledWallBrickCount']} "
        f"causal_nodes={actual_causal['topologyCount']} "
        f"presented_cascades=1 "
        f"saved_loaded_ticks={artifact['sampleCount']} "
        f"first_reveal={actual['ticks'][0]['revealFrame']} "
        f"last_reveal={actual['ticks'][-1]['revealFrame']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
