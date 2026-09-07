#!/usr/bin/env python3
"""Prove one selected causal prediction reaches the production DX12 submission.

The gate drives only Skarness commands. It binds the selected object through
prediction topology, full trajectory samples, retained marker geometry, the
visual packet, and the renderer submission before checking that a connected
world-space feature appeared in the captured viewport.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from typing import Any

from PIL import Image, ImageDraw

from check_prediction_future_render import CheckFailure, validate_raster
from skarness import SkarnessConnection


REPO = Path(__file__).resolve().parents[1]
DEFAULT_EXE = REPO / "Automation" / "SKULLBONEZ_CORE.exe"
DEFAULT_SCENE = REPO / "SkullbonezData" / "scenes" / "interaction_replay_prediction_harness.scene.json"
DEFAULT_TARGET = "path_striker"
STATE_KINDS = {"snapshot", "append", "change", "evict", "reset", "state"}
FUTURE_ROOT = 1
FUTURE_CHILD_INCOMING = 2
FUTURE_CHILD_OUTGOING = 3
REQUIRED_TOPICS = {
    "selection.state",
    "replay.prediction.controls",
    "replay.prediction.frames",
    "replay.prediction.topology",
    "replay.prediction.trajectories",
    "replay.visual_packet",
    "replay.render_submission",
    "replay.state",
}
VISUAL_BUFFERS = (
    "combinedLines",
    "ordinaryLines",
    "priorityLines",
    "ordinaryRibbonSegments",
    "priorityRibbonSegments",
    "expandedRibbonVertices",
    "priorityExpandedRibbonVertices",
    "retainedOrdinaryLines",
    "retainedPriorityLines",
    "retainedOrdinaryRibbonSegments",
    "retainedPriorityRibbonSegments",
    "retainedRibbonVertices",
    "retainedPriorityRibbonVertices",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CheckFailure(message)


def payload(topics: dict[str, dict[str, Any]], topic: str) -> dict[str, Any]:
    row = topics.get(topic)
    require(isinstance(row, dict), f"missing state topic: {topic}")
    value = row.get("payload")
    require(isinstance(value, dict), f"state topic has no payload object: {topic}")
    return value


def latest_topics(rows: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    latest: dict[str, dict[str, Any]] = {}
    scene_generation: int | None = None
    for row in rows:
        topic = row.get("topic")
        if row.get("kind") not in STATE_KINDS or not isinstance(topic, str):
            continue
        row_generation = int(row.get("sceneGeneration", -1))
        if scene_generation is None or row_generation != scene_generation:
            latest.clear()
            scene_generation = row_generation
        if row.get("kind") == "reset":
            latest.pop(topic, None)
        else:
            latest[topic] = row
    return latest


def validate_identity(topics: dict[str, dict[str, Any]]) -> tuple[int, int, int, int]:
    missing = REQUIRED_TOPICS - topics.keys()
    require(not missing, f"missing required state topics: {sorted(missing)}")

    scene_generations = {int(topics[name].get("sceneGeneration", -1)) for name in REQUIRED_TOPICS}
    require(len(scene_generations) == 1 and next(iter(scene_generations)) > 0,
            f"state topics do not belong to one scene generation: {sorted(scene_generations)}")

    selection = payload(topics, "selection.state")
    controls = payload(topics, "replay.prediction.controls")
    topology = payload(topics, "replay.prediction.topology")
    trajectories = payload(topics, "replay.prediction.trajectories")
    visual = payload(topics, "replay.visual_packet")
    submission = payload(topics, "replay.render_submission")
    legacy = payload(topics, "replay.state")
    header = visual.get("header", {})

    target = int(selection.get("pathTargetId", 0))
    require(selection.get("hasPathTarget") is True and target > 0, "selection has no path target")
    target_values = {
        target,
        int(controls.get("sourceTargetId", 0)),
        int(topology.get("targetId", 0)),
        int(header.get("targetId", 0)),
        int(submission.get("targetId", 0)),
        int(legacy.get("pathTargetId", 0)),
        int(legacy.get("publishedPredictionTargetId", 0)),
        int(legacy.get("submittedPredictionTargetId", 0)),
    }
    require(target_values == {target}, f"selected target identity is stale: {sorted(target_values)}")

    source = int(controls.get("sourceFrame", -1))
    source_values = {
        source,
        int(header.get("sourceFrame", -2)),
        int(submission.get("sourceFrame", -3)),
        int(legacy.get("predictionSourceFrame", -4)),
        int(legacy.get("submittedPredictionSourceFrame", -5)),
    }
    require(source >= 0 and source_values == {source}, f"prediction source frame is stale: {sorted(source_values)}")

    generation = int(controls.get("generation", 0))
    generation_versions = {
        int(topics["replay.prediction.controls"].get("ownerVersion", -1)),
        int(topics["replay.prediction.frames"].get("ownerVersion", -1)),
    }
    require(generation > 0 and generation_versions == {generation},
            f"prediction controls and frames do not share generation {generation}: {sorted(generation_versions)}")

    version = int(topology.get("topologyVersion", 0))
    version_values = {
        version,
        int(trajectories.get("topologyVersion", -1)),
        int(header.get("topologyVersion", -2)),
        int(submission.get("topologyVersion", -3)),
        int(legacy.get("publishedPredictionTopologyVersion", -4)),
        int(legacy.get("submittedPredictionTopologyVersion", -5)),
    }
    require(version > 0 and version_values == {version}, f"prediction topology identity is stale: {sorted(version_values)}")
    require(int(topics["replay.prediction.topology"].get("ownerVersion", -1)) == version
            and int(topics["replay.render_submission"].get("ownerVersion", -1)) == version,
            "topology and render submission owner versions do not match their topology version")
    retained_revision = int(visual.get("retainedRevision", 0))
    require(retained_revision > 0 and int(trajectories.get("retainedRevision", -1)) == retained_revision
            and int(topics["replay.prediction.trajectories"].get("ownerVersion", -1)) == retained_revision
            and int(topics["replay.visual_packet"].get("ownerVersion", -1)) == retained_revision,
            "trajectory and visual owner versions do not match the retained revision")
    require(int(header.get("publishedFrameCount", -1)) == int(payload(topics, "replay.prediction.frames").get("count", -2)),
            "visual packet frame count is stale")
    require(int(header.get("futureNodeCount", -1)) == int(topology.get("futureNodeCount", -2)),
            "visual packet topology count is stale")
    require(int(visual.get("trajectoryRecordCount", -1)) == int(trajectories.get("recordCount", -2)),
            "visual packet trajectory count is stale")
    return target, source, version, next(iter(scene_generations))


def validate_trajectories(topics: dict[str, dict[str, Any]], target: int) -> tuple[set[int], int]:
    frames = payload(topics, "replay.prediction.frames")
    topology = payload(topics, "replay.prediction.topology")
    trajectories = payload(topics, "replay.prediction.trajectories")

    frame_rows = frames.get("frames", [])
    require(isinstance(frame_rows, list) and int(frames.get("count", 0)) == len(frame_rows) >= 2,
            "prediction frame bank is incomplete")
    require(all(not bool(row.get("contactsIncomplete")) for row in frame_rows if isinstance(row, dict)),
            "prediction frame bank contains incomplete contacts")

    nodes = topology.get("nodes", [])
    require(isinstance(nodes, list), "prediction topology omitted full nodes")
    contact_nodes = {
        int(node.get("id", 0)): node
        for node in nodes
        if isinstance(node, dict) and node.get("contactDerived") is True and int(node.get("id", 0)) > 0
    }
    require(int(topology.get("futureNodeCount", 0)) == len(nodes) and contact_nodes,
            "prediction topology has no contact-derived children")
    require(len(contact_nodes) == len(nodes), "prediction topology contains duplicate or non-contact child nodes")
    for body_id, node in contact_nodes.items():
        require(body_id != target, f"causal child {body_id} aliases the selected root")
        visited: set[int] = set()
        ancestor = body_id
        while ancestor != target:
            require(ancestor not in visited, f"causal child {body_id} has a parent cycle")
            visited.add(ancestor)
            ancestor_node = contact_nodes.get(ancestor)
            require(ancestor_node is not None, f"causal child {body_id} is disconnected from selected root {target}")
            ancestor = int(ancestor_node.get("parentId", 0))

    records = trajectories.get("records", [])
    require(isinstance(records, list) and int(trajectories.get("recordCount", 0)) == len(records),
            "trajectory record count does not match its payload")
    root_rows = [row for row in records if isinstance(row, dict) and int(row.get("lane", -1)) == FUTURE_ROOT]
    require(root_rows and all(int(row.get("bodyId", 0)) == target for row in root_rows),
            "FutureRoot is absent or belongs to an unselected object")
    require(any(int(row.get("publishedPointCount", 0)) >= 2
                and len(row.get("points", [])) == int(row.get("publishedPointCount", 0))
                for row in root_rows), "selected FutureRoot has no full point samples")

    incoming = {
        int(row.get("bodyId", 0)): row
        for row in records
        if isinstance(row, dict) and int(row.get("lane", -1)) == FUTURE_CHILD_INCOMING
        and row.get("contactDerived") is True and int(row.get("publishedPointCount", 0)) >= 2
        and len(row.get("points", [])) == int(row.get("publishedPointCount", 0))
    }
    outgoing = {
        int(row.get("bodyId", 0)): row
        for row in records
        if isinstance(row, dict) and int(row.get("lane", -1)) == FUTURE_CHILD_OUTGOING
        and row.get("contactDerived") is True and int(row.get("publishedPointCount", 0)) >= 2
        and len(row.get("points", [])) == int(row.get("publishedPointCount", 0))
    }
    paired = contact_nodes.keys() & incoming.keys() & outgoing.keys()
    require(paired == set(contact_nodes), "not every causal child has full incoming and outgoing paths")
    for body_id in paired:
        node = contact_nodes[body_id]
        entry = int(node.get("firstFrame", -1))
        parent = int(node.get("parentId", 0))
        depth = int(node.get("depth", -1))
        for lane in (incoming[body_id], outgoing[body_id]):
            require(int(lane.get("parentId", -1)) == parent and int(lane.get("firstFrame", -1)) == entry
                    and int(lane.get("depth", -1)) == depth,
                    f"child {body_id} trajectory identity differs from topology")
        incoming_frames = [int(point.get("frame", -1)) for point in incoming[body_id].get("points", [])]
        outgoing_frames = [int(point.get("frame", -1)) for point in outgoing[body_id].get("points", [])]
        require(incoming_frames and max(incoming_frames) == entry,
                f"child {body_id} incoming path does not end at its collision frame")
        require(outgoing_frames and min(outgoing_frames) == entry,
                f"child {body_id} outgoing path begins before or after its collision frame")
    return set(paired), max(int(row.get("publishedPointCount", 0)) for row in root_rows)


def validate_visual_and_submission(topics: dict[str, dict[str, Any]], paired: set[int]) -> tuple[int, int]:
    visual = payload(topics, "replay.visual_packet")
    submission = payload(topics, "replay.render_submission")
    legacy = payload(topics, "replay.state")
    header = visual.get("header", {})

    require(header.get("predictionEnabled") is True and header.get("predictionComplete") is True
            and header.get("predictionBuilding") is False, "visual packet is not a completed prediction")
    require(visual.get("hasGeometry") is True, "visual packet reports no geometry")
    nonempty_buffers = 0
    for name in VISUAL_BUFFERS:
        buffer = visual.get(name, {})
        count = int(buffer.get("count", 0))
        size = int(buffer.get("bytes", 0))
        require(size == count * 4, f"visual buffer byte count is inconsistent: {name}")
        if count > 0:
            require(int(buffer.get("hash", 0)) != 0, f"visual buffer has no hash: {name}")
            nonempty_buffers += 1
    require(nonempty_buffers > 0, "semantic prediction produced no visual buffers")

    render_geometry = visual.get("renderGeometry", {})
    line_bytes = int(render_geometry.get("lineBytes", 0))
    ribbon_bytes = int(render_geometry.get("ribbonBytes", 0))
    vertex_bytes = int(render_geometry.get("vertexBytes", 0))
    visual_geometry_bytes = int(render_geometry.get("geometryBytes", 0))
    require(render_geometry.get("spanTelemetryMatches") is True,
            f"visual packet spans disagree with renderer telemetry: {render_geometry.get('spanMismatch', '')}")
    require(visual_geometry_bytes > 0 and visual_geometry_bytes == line_bytes + ribbon_bytes + vertex_bytes,
            "visual packet has no coherent representation-aware geometry byte count")
    visual_submission_hash = int(render_geometry.get("submissionHash", 0))
    require(visual_submission_hash != 0, "visual packet has no independently derived submission hash")

    markers = visual.get("markers", [])
    require(isinstance(markers, list) and int(visual.get("markerCount", 0)) == len(markers),
            "retained marker count does not match full marker rows")
    entry_ids = {int(marker.get("id", 0)) for marker in markers
                 if isinstance(marker, dict) and marker.get("hasEntryPose") is True}
    ending_ids = {int(marker.get("id", 0)) for marker in markers
                  if isinstance(marker, dict)
                  and (marker.get("hasRestPose") is True or marker.get("hasHorizonPose") is True)}
    require(paired <= entry_ids, "causal child has no retained entry wireframe pose")
    require(paired <= ending_ids, "causal child has no retained ending wireframe pose")

    require(submission.get("hasSubmission") is True and submission.get("futureTreeReady") is True
            and submission.get("stableWindowReady") is True and submission.get("noReserveGrowth") is True,
            "DX12 submission did not accept the causal future tree")
    require(int(submission.get("stableFrameTarget", 0)) > 0
            and int(submission.get("stableFrameCount", 0)) >= int(submission.get("stableFrameTarget", 0))
            and int(submission.get("observedFrameCount", 0)) >= int(submission.get("stableFrameCount", 0)),
            "DX12 submission has no coherent stable observation window")
    submitted_bytes = int(submission.get("submittedGeometryBytes", 0))
    require(submitted_bytes > 0 and submitted_bytes == int(submission.get("geometryBytes", -1)),
            "DX12 submission has no coherent geometry byte count")
    require(submitted_bytes == visual_geometry_bytes,
            f"DX12 submission bytes differ from the production visual packet: {submitted_bytes} != {visual_geometry_bytes}")
    submitted_hash = int(submission.get("submittedGeometryHash", 0))
    require(submitted_hash == int(submission.get("stableHash", -1)) == visual_submission_hash,
            "DX12 submission hash differs from renderer-bound visual spans")

    entry_count = int(legacy.get("retainedEntryMarkerCount", -1))
    ending_count = int(legacy.get("retainedEndMarkerCount", -1))
    require(entry_count == len(entry_ids) == int(legacy.get("drawnCollisionWireframeCount", -2)),
            "collision wireframes disagree with retained entry markers")
    require(ending_count == len(ending_ids) == int(legacy.get("drawnEndingWireframeCount", -2)),
            "ending wireframes disagree with retained ending markers")
    require(int(legacy.get("collisionWireframePathMismatchCount", -1)) == 0
            and int(legacy.get("endingWireframePathMismatchCount", -1)) == 0,
            "retained wireframe pose does not lie on its trajectory")
    return submitted_bytes, len(markers)


def validate_evidence(topics: dict[str, dict[str, Any]]) -> dict[str, int]:
    target, source, version, scene_generation = validate_identity(topics)
    paired, root_points = validate_trajectories(topics, target)
    submitted_bytes, markers = validate_visual_and_submission(topics, paired)
    return {
        "sceneGeneration": scene_generation,
        "targetId": target,
        "sourceFrame": source,
        "topologyVersion": version,
        "causalChildren": len(paired),
        "rootPoints": root_points,
        "retainedMarkers": markers,
        "submittedGeometryBytes": submitted_bytes,
    }


def row(topic: str, value: dict[str, Any], owner_version: int = 1) -> dict[str, Any]:
    return {
        "sceneGeneration": 3,
        "topic": topic,
        "kind": "change",
        "ownerVersion": owner_version,
        "payload": value,
    }


def valid_fixture() -> dict[str, dict[str, Any]]:
    points = lambda values: [{"frame": frame, "position": [float(frame), 0.0, 0.0]} for frame in values]
    buffers = {name: {"count": 0, "bytes": 0, "hash": 1, "values": []} for name in VISUAL_BUFFERS}
    buffers["retainedPriorityLines"] = {"count": 24, "bytes": 96, "hash": 55, "values": [0.0] * 24}
    return {
        "selection.state": row("selection.state", {"hasPathTarget": True, "pathTargetId": 7}),
        "replay.prediction.controls": row("replay.prediction.controls", {
            "enabled": True, "building": False, "complete": True, "generation": 1,
            "sourceTargetId": 7, "sourceFrame": 4,
        }),
        "replay.prediction.frames": row("replay.prediction.frames", {
            "count": 4, "frames": [{"frame": frame, "contactsIncomplete": False} for frame in range(4)],
        }),
        "replay.prediction.topology": row("replay.prediction.topology", {
            "targetId": 7, "topologyVersion": 9, "futureNodeCount": 1,
            "nodes": [{"id": 8, "parentId": 7, "firstFrame": 2, "depth": 1, "contactDerived": True}],
        }, owner_version=9),
        "replay.prediction.trajectories": row("replay.prediction.trajectories", {
            "topologyVersion": 9, "retainedRevision": 2, "recordCount": 3, "records": [
                {"bodyId": 7, "lane": FUTURE_ROOT, "publishedPointCount": 4, "points": points(range(4))},
                {"bodyId": 8, "lane": FUTURE_CHILD_INCOMING, "publishedPointCount": 3,
                 "parentId": 7, "depth": 1, "firstFrame": 2,
                 "contactDerived": True, "points": points(range(3))},
                {"bodyId": 8, "lane": FUTURE_CHILD_OUTGOING, "publishedPointCount": 2,
                 "parentId": 7, "depth": 1, "firstFrame": 2,
                 "contactDerived": True, "points": points(range(2, 4))},
            ],
        }, owner_version=2),
        "replay.visual_packet": row("replay.visual_packet", {
            "header": {"targetId": 7, "sourceFrame": 4, "topologyVersion": 9,
                       "publishedFrameCount": 4, "futureNodeCount": 1,
                       "predictionEnabled": True, "predictionBuilding": False, "predictionComplete": True},
            "retainedStreamId": 2, "retainedRevision": 2, "trajectoryRecordCount": 3,
            "hasGeometry": True, "markerCount": 1,
            "markers": [{"id": 8, "hasEntryPose": True, "hasRestPose": False, "hasHorizonPose": True}],
            "renderGeometry": {
                "lineBytes": 96, "ribbonBytes": 0, "vertexBytes": 0, "geometryBytes": 96,
                "submissionHash": 77, "spanTelemetryMatches": True, "spanMismatch": "",
            },
            **buffers,
        }, owner_version=2),
        "replay.render_submission": row("replay.render_submission", {
            "hasSubmission": True, "futureTreeReady": True, "stableWindowReady": True,
            "noReserveGrowth": True, "observedFrameCount": 10, "stableFrameCount": 8, "stableFrameTarget": 8,
            "targetId": 7, "sourceFrame": 4,
            "topologyVersion": 9, "stableHash": 77, "geometryBytes": 96,
            "submittedGeometryHash": 77, "submittedGeometryBytes": 96,
        }, owner_version=9),
        "replay.state": row("replay.state", {
            "pathTargetId": 7, "publishedPredictionTargetId": 7, "submittedPredictionTargetId": 7,
            "predictionSourceFrame": 4, "submittedPredictionSourceFrame": 4,
            "publishedPredictionTopologyVersion": 9, "submittedPredictionTopologyVersion": 9,
            "retainedEntryMarkerCount": 1, "drawnCollisionWireframeCount": 1,
            "retainedEndMarkerCount": 1, "drawnEndingWireframeCount": 1,
            "collisionWireframePathMismatchCount": 0, "endingWireframePathMismatchCount": 0,
        }),
    }


def expect_failure(operation: Any, label: str) -> None:
    try:
        operation()
    except CheckFailure:
        return
    raise AssertionError(f"negative control unexpectedly passed: {label}")


def run_self_test() -> None:
    validate_evidence(valid_fixture())

    transient_lines = valid_fixture()
    transient_lines["replay.visual_packet"]["payload"]["combinedLines"] = {
        "count": 24, "bytes": 96, "hash": 41, "values": [0.0] * 24,
    }
    transient_lines["replay.visual_packet"]["payload"]["ordinaryLines"] = {
        "count": 24, "bytes": 96, "hash": 42, "values": [0.0] * 24,
    }
    transient_lines["replay.visual_packet"]["payload"]["retainedPriorityLines"] = {
        "count": 0, "bytes": 0, "hash": 1, "values": [],
    }
    validate_evidence(transient_lines)

    ribbon_geometry = valid_fixture()
    ribbon_visual = ribbon_geometry["replay.visual_packet"]["payload"]
    ribbon_visual["retainedPriorityLines"] = {"count": 0, "bytes": 0, "hash": 1, "values": []}
    ribbon_visual["ordinaryRibbonSegments"] = {
        "count": 13, "bytes": 52, "hash": 51, "values": [0.0] * 13,
    }
    ribbon_visual["expandedRibbonVertices"] = {
        "count": 114, "bytes": 456, "hash": 52, "values": [0.0] * 114,
    }
    ribbon_visual["renderGeometry"].update({
        "lineBytes": 0, "ribbonBytes": 52, "vertexBytes": 456, "geometryBytes": 508,
    })
    ribbon_geometry["replay.render_submission"]["payload"].update({
        "geometryBytes": 508, "submittedGeometryBytes": 508,
    })
    validate_evidence(ribbon_geometry)

    root_only = valid_fixture()
    root_only["replay.prediction.topology"]["payload"].update({"futureNodeCount": 0, "nodes": []})
    root_only["replay.prediction.trajectories"]["payload"].update({
        "recordCount": 1,
        "records": root_only["replay.prediction.trajectories"]["payload"]["records"][:1],
    })
    expect_failure(lambda: validate_evidence(root_only), "root-only prediction")

    semantic_only = valid_fixture()
    for name in VISUAL_BUFFERS:
        semantic_only["replay.visual_packet"]["payload"][name].update({"count": 0, "bytes": 0, "values": []})
    expect_failure(lambda: validate_evidence(semantic_only), "semantic-only prediction")

    stale_identity = valid_fixture()
    stale_identity["replay.render_submission"]["payload"]["targetId"] = 6
    expect_failure(lambda: validate_evidence(stale_identity), "stale render identity")

    stale_topology = valid_fixture()
    stale_topology["replay.visual_packet"]["payload"]["header"]["topologyVersion"] = 8
    expect_failure(lambda: validate_evidence(stale_topology), "stale topology identity")

    mismatched_owner = valid_fixture()
    mismatched_owner["replay.prediction.frames"]["ownerVersion"] = 8
    expect_failure(lambda: validate_evidence(mismatched_owner), "mixed prediction owner versions")

    disconnected_child = valid_fixture()
    disconnected_child["replay.prediction.topology"]["payload"]["nodes"][0]["parentId"] = 999
    expect_failure(lambda: validate_evidence(disconnected_child), "disconnected causal ancestry")

    truncated_child = valid_fixture()
    truncated_child["replay.prediction.trajectories"]["payload"]["records"][1]["points"] = [
        {"frame": 2, "position": [2.0, 0.0, 0.0]},
    ]
    expect_failure(lambda: validate_evidence(truncated_child), "truncated child samples")

    zero_submission = valid_fixture()
    zero_submission["replay.render_submission"]["payload"].update({
        "hasSubmission": False, "geometryBytes": 0, "submittedGeometryBytes": 0,
    })
    expect_failure(lambda: validate_evidence(zero_submission), "zero DX12 submission")

    unstable_submission = valid_fixture()
    unstable_submission["replay.render_submission"]["payload"]["stableWindowReady"] = False
    expect_failure(lambda: validate_evidence(unstable_submission), "unstable DX12 submission")

    mismatched_hash = valid_fixture()
    mismatched_hash["replay.visual_packet"]["payload"]["renderGeometry"]["submissionHash"] = 76
    expect_failure(lambda: validate_evidence(mismatched_hash), "visual/submission hash mismatch")

    reset_without_snapshot = list(valid_fixture().values())
    reset_without_snapshot.append({
        "sceneGeneration": 3, "topic": "replay.visual_packet", "kind": "reset", "ownerVersion": 0, "payload": {},
    })
    expect_failure(lambda: validate_evidence(latest_topics(reset_without_snapshot)), "reset without replacement snapshot")

    before = Image.new("RGB", (320, 180), (55, 12, 65))
    after = before.copy()
    ImageDraw.Draw(after).line((30, 120, 210, 65), fill=(82, 178, 163), width=2)
    validate_raster(before, after)
    ui_only = before.copy()
    ImageDraw.Draw(ui_only).line((275, 20, 275, 155), fill=(82, 178, 163), width=2)
    expect_failure(lambda: validate_raster(before, ui_only), "UI-only pixels")
    disconnected = before.copy()
    draw = ImageDraw.Draw(disconnected)
    draw.rectangle((30, 90, 39, 99), fill=(210, 80, 20))
    draw.rectangle((180, 90, 189, 99), fill=(210, 80, 20))
    expect_failure(lambda: validate_raster(before, disconnected), "disconnected pixels")


def launch_session(session: Path, executable: Path, scene: Path) -> None:
    launched = subprocess.run(
        [sys.executable, str(REPO / "tools" / "skarness.py"), "launch", "--session", str(session),
         "--exe", str(executable), "--scene", str(scene), "--hidden", "--detail", "full"],
        cwd=REPO, check=False, capture_output=True, text=True,
    )
    require(launched.returncode == 0, f"Skarness launch failed: {launched.stderr or launched.stdout}")


def complete_rows(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    deadline = time.monotonic() + 10.0
    while True:
        try:
            with path.open("rb") as source:
                for raw_line in source:
                    if not raw_line.endswith(b"\n"):
                        break
                    rows.append(json.loads(raw_line))
            return rows
        except (FileNotFoundError, PermissionError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.05)


def require_applied(connection: SkarnessConnection, command: str, arguments: dict[str, Any] | None = None) -> None:
    result = connection.wait(connection.send(command, arguments or {}))
    require(result.get("status") == "applied", f"Skarness command failed: {command}: {result}")


def validate_live(session: Path, executable: Path, scene: Path, target_name: str) -> dict[str, Any]:
    session.mkdir(parents=True, exist_ok=True)
    before_path = (session / "future-before.png").resolve()
    after_path = (session / "future-after.png").resolve()
    launch_session(session, executable, scene)
    connection = SkarnessConnection(session)
    stopped = False
    try:
        require_applied(connection, "run.pause")
        require_applied(connection, "replay.set_prediction_horizon", {"seconds": 2.0})
        require_applied(connection, "capture.screenshot", {"path": str(before_path)})
        require_applied(connection, "prediction.select_target", {"name": target_name})
        require_applied(connection, "replay.set_prediction_enabled", {"enabled": True})
        require_applied(connection, "run.until", {"condition": "prediction.causal_rendered", "maxFrames": 3000})
        require_applied(connection, "run.step_frames", {"count": 128})
        require_applied(connection, "capture.screenshot", {"path": str(after_path)})
        require_applied(connection, "session.stop")
        stopped = True
    finally:
        if not stopped:
            try:
                require_applied(connection, "session.stop")
            except (CheckFailure, OSError, RuntimeError):
                pass
        connection.close()

    rows = complete_rows(session / "runtime.skarness.ndjson")
    topics = latest_topics(rows)
    evidence = validate_evidence(topics)
    with Image.open(before_path) as before, Image.open(after_path) as after:
        raster = validate_raster(before, after)
    result: dict[str, Any] = {
        "ok": True,
        "scene": str(scene),
        "target": target_name,
        "evidence": evidence,
        "raster": {
            "newPathPixels": raster.new_path_pixels,
            "horizontalSpan": raster.horizontal_span,
            "verticalSpan": raster.vertical_span,
        },
        "artifacts": {"before": str(before_path), "after": str(after_path)},
    }
    (session / "future-render-report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput" / "skarness" / "future-render")
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--scene", type=Path, default=DEFAULT_SCENE)
    parser.add_argument("--target", default=DEFAULT_TARGET)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.self_test:
            run_self_test()
            print("PASS: Skarness future-render negative controls")
            return 0
        result = validate_live(args.session.resolve(), args.exe.resolve(), args.scene.resolve(), args.target)
        evidence = result["evidence"]
        raster = result["raster"]
        print(
            "PASS: selected causal future reached DX12 "
            f"targetId={evidence['targetId']} children={evidence['causalChildren']} "
            f"rootPoints={evidence['rootPoints']} geometryBytes={evidence['submittedGeometryBytes']} "
            f"newPathPixels={raster['newPathPixels']} "
            f"span={raster['horizontalSpan']}x{raster['verticalSpan']}"
        )
        return 0
    except (CheckFailure, json.JSONDecodeError, OSError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
