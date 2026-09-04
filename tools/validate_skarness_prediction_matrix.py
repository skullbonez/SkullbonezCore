#!/usr/bin/env python3
"""Exercise prediction across scene transitions through one Skarness session."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from dataclasses import replace
import json
from pathlib import Path
import sys
import time

from skarness import SkarnessConnection, launch, query_latest_state

STATE_KINDS = {"snapshot", "append", "change", "evict", "reset", "state"}


@dataclass(frozen=True)
class PredictionCase:
    label: str
    scene: str | None
    target_name: str | None = None
    target_id: int | None = None
    target_candidates: tuple[int, ...] = ()
    horizon: float = 2.0
    warmup_ticks: int = 30
    causal: bool = False
    screenshot: bool = False
    continuity: bool = False
    cache_stability: bool = False
    generation_stability: bool = False
    capacity_transition: bool = False
    live_cause_stability: bool = False
    causal_camera: bool = False
    causal_child_body: bool = False
    retarget_after_ticks: int = 0
    retarget_name: str | None = None


CASES = (
    PredictionCase("ragdoll-wall-first", "prediction_ragdoll_wall_200.scene.json", "prediction_striker_ball",
                   horizon=20.0, warmup_ticks=0, causal=True, screenshot=True, cache_stability=True,
                   causal_camera=True, causal_child_body=True, retarget_after_ticks=120,
                   retarget_name="prediction_wall_brick_r05_c19"),
    PredictionCase("at-rest", "at_rest.scene.json", "ball_a", warmup_ticks=120, screenshot=True),
    PredictionCase("box-only-rest", "box_only_rest.scene.json", "base_a", warmup_ticks=120),
    PredictionCase("tornado-village", "tornado_village_rampage.scene.json", "yard_ball_red", screenshot=True),
    PredictionCase("tornado-alley", "tornado_alley_showcase.scene.json", "loose_debris_00", screenshot=True),
    PredictionCase("ragdoll-playground", "ragdoll_playground.scene.json", "wake_ball"),
    PredictionCase("marble-run", "cause_effect_marble_run.scene.json", "marble_1"),
    PredictionCase("bullet-wall", "bullet_sweep_wall.scene.json", "bullet_wall"),
    PredictionCase("space-field", "space_field_200.scene.json", "field_000"),
    PredictionCase("nbody-chaos", "nbody_chaos_playground.scene.json", "chaos_a"),
    PredictionCase("box-pile", "box_pile_throw_300.scene.json", "throw_box_000"),
    PredictionCase("box-slope", "box_slope_test.scene.json", "box_slope_flat"),
    PredictionCase("ten-pin-bowling", "ten_pin_bowling_strike.scene.json", "bowling_ball",
                   horizon=63.0, warmup_ticks=240, screenshot=True, generation_stability=True,
                   capacity_transition=True),
    # Generated demo placement is intentionally time-seeded. Search a bounded,
    # stable ID prefix for a body with actual causal descendants instead of
    # turning one random body's lack of contacts into a flaky renderer result.
    PredictionCase("demo", None, target_candidates=(1, 2, 3, 4, 5, 6, 7, 8), horizon=20.0,
                   warmup_ticks=120, causal=True, screenshot=True, continuity=True, live_cause_stability=True,
                   causal_camera=True),
    PredictionCase("ragdoll-wall-after-transitions", "prediction_ragdoll_wall_200.scene.json",
                   "prediction_striker_ball", horizon=20.0, warmup_ticks=0, causal=True, screenshot=True),
)


def require_applied(connection: SkarnessConnection, command: str, arguments: dict[str, object] | None = None) -> None:
    result = connection.wait(connection.send(command, arguments or {}))
    if result.get("status") != "applied":
        raise RuntimeError(f"{command} failed: {json.dumps(result, separators=(',', ':'))}")


def warm_mismatched_prediction_capacity(connection: SkarnessConnection) -> None:
    """Build a shorter, wider frame bank before a longer small-scene prediction."""
    require_applied(connection, "scene.load", {"name": "box_pile_throw_300.scene.json"})
    require_applied(connection, "replay.set_prediction_horizon", {"seconds": 30.0})
    require_applied(connection, "prediction.select_target", {"name": "throw_box_000"})
    require_applied(connection, "replay.set_prediction_enabled", {"enabled": True})
    require_applied(connection, "run.until", {"condition": "prediction.geometry", "maxFrames": 3000})
    require_applied(connection, "replay.set_prediction_enabled", {"enabled": False})


def collect_live_replay_states(connection: SkarnessConnection, duration_seconds: float) -> list[dict[str, object]]:
    states: list[dict[str, object]] = []
    deadline = time.monotonic() + duration_seconds
    while time.monotonic() < deadline:
        event = connection.read_event()
        if event.get("kind") in STATE_KINDS and event.get("topic") == "replay.state":
            states.append(event)
    return states


def validate_prediction(case: PredictionCase, state: dict[str, object]) -> list[str]:
    payload = state.get("payload", {})
    failures: list[str] = []

    def expect(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    expect(bool(payload.get("predictionEnabled")), "prediction is disabled")
    expect(bool(payload.get("predictionComplete")), "prediction did not complete")
    expect(not bool(payload.get("predictionBuilding")), "prediction worker is still building")
    expect(not bool(payload.get("predictionDirty")), "completed prediction retained a dirty restart")
    expect(not bool(payload.get("predictionRestartPending")), "completed prediction retained a pending restart")
    expect(bool(payload.get("predictionGenerationPermitted")), "generation is not permitted")
    expect(int(payload.get("pathTargetId", 0)) != 0, "no selected path target")
    expect(payload.get("pathTargetId") == payload.get("publishedPredictionTargetId"), "published target is stale")
    expect(int(payload.get("publishedPredictionFrames", 0)) >= 2, "future frame prefix is missing")
    expect(int(payload.get("selectedFutureRootPointCount", 0)) >= 2, "root future trajectory is missing")
    expect(int(payload.get("trajectoryRecordCount", 0)) >= 1, "trajectory packet is empty")
    expect(bool(payload.get("trajectorySubmitted")), "current prediction was not submitted to rendering")
    expect(payload.get("submittedPredictionTargetId") == payload.get("pathTargetId"),
           "render submission target is stale")
    expect(payload.get("submittedPredictionSourceFrame") == payload.get("predictionSourceFrame"),
           "render submission source frame is stale")
    expect(payload.get("submittedPredictionTopologyVersion") == payload.get("publishedPredictionTopologyVersion"),
           "render submission topology is stale")
    expect(int(payload.get("submittedGeometryHash", 0)) != 0, "render submission has no content hash")
    expect(int(payload.get("submittedGeometryBytes", 0)) != 0, "render submission has no geometry bytes")
    if case.continuity:
        expect(bool(payload.get("pastPathVisible")), "past path visibility was not retained")
        expect(int(payload.get("selectedPastRootPointCount", 0)) >= 2, "selected past trajectory is missing")
    expect(bool(payload.get("visualPacketHasGeometry")), "visual packet contains no future geometry")
    expect(int(payload.get("drawnEndingWireframeCount", 0)) >= 1, "predicted endpoint wireframe was not drawn")
    expect(int(payload.get("endingWireframePathMismatchCount", 0)) == 0, "endpoint wireframe is off its path")
    expect(int(payload.get("collisionWireframePathMismatchCount", 0)) == 0, "collision wireframe is off its path")
    expect(int(payload.get("incompleteContactFrameCount", 0)) == 0, "causal contact frames were truncated")

    if case.causal:
        expect(int(payload.get("futureNodeCount", 0)) > 0, "causal children are missing")
        expect(int(payload.get("contactChildIncomingCount", 0)) > 0, "incoming child paths are missing")
        expect(int(payload.get("contactChildOutgoingCount", 0)) > 0, "outgoing child paths are missing")
        expect(payload.get("drawnCollisionWireframeCount") == payload.get("retainedEntryMarkerCount"),
               "not every collision marker was drawn")
        expect(payload.get("drawnEndingWireframeCount") == payload.get("retainedEndMarkerCount"),
               "not every ending marker was drawn")
        expect(bool(payload.get("submittedFutureTreeReady")), "causal tree was not submitted")
        expect(int(payload.get("causeTreeRowCount", 0)) > 0, "completed cause tree is empty")
        expect(bool(payload.get("causeWindowAvailable")), "completed cause window is unavailable")

    return failures


def validate_prediction_generation_stability(before: dict[str, object], after: dict[str, object],
                                             stepped_frames: int) -> list[str]:
    before_payload = before.get("payload", {})
    after_payload = after.get("payload", {})
    failures: list[str] = []

    if after_payload.get("predictionGeneration") != before_payload.get("predictionGeneration"):
        failures.append(f"completed prediction restarted across {stepped_frames} stable frames")
    if bool(after_payload.get("predictionBuilding")) or bool(after_payload.get("predictionDirty")):
        failures.append(f"completed prediction re-entered its build loop across {stepped_frames} stable frames")
    if not bool(after_payload.get("predictionComplete")):
        failures.append(f"completed prediction lost its terminal state across {stepped_frames} stable frames")

    return failures


def replay_states_after(session: Path, render_frame: int) -> list[dict[str, object]]:
    states: list[dict[str, object]] = []
    trace = session / "runtime.skarness.ndjson"
    with trace.open("r", encoding="utf-8") as stream:
        for line in stream:
            if not line.endswith("\n"):
                # The host may still be appending the final event while this
                # read reaches EOF; the next query will observe it complete.
                continue
            event = json.loads(line)
            if (event.get("kind") in STATE_KINDS and event.get("topic") == "replay.state" and
                    int(event.get("renderFrame", 0)) > render_frame):
                states.append(event)
    return states


def validate_continuity(states: list[dict[str, object]], selected_target_id: int) -> list[str]:
    failures: list[str] = []
    selected = [state.get("payload", {}) for state in states
                if int(state.get("payload", {}).get("pathTargetId", 0)) == selected_target_id]
    visible_past = [payload for payload in selected if bool(payload.get("pastPathVisible"))]
    first_populated = next((index for index, payload in enumerate(visible_past)
                            if int(payload.get("selectedPastRootPointCount", 0)) >= 2), None)

    if first_populated is None:
        failures.append("past trajectory never populated after selection")
    elif any(int(payload.get("selectedPastRootPointCount", 0)) < 2 for payload in visible_past[first_populated:]):
        failures.append("past trajectory disappeared while its visibility toggle remained selected")

    prediction_states = [payload for payload in selected if bool(payload.get("predictionEnabled"))]
    first_cause = next((index for index, payload in enumerate(prediction_states)
                        if int(payload.get("causeTreeRowCount", 0)) > 0 and
                        bool(payload.get("causeWindowAvailable"))), None)
    if first_cause is not None and any(int(payload.get("causeTreeRowCount", 0)) == 0 or
                                       not bool(payload.get("causeWindowAvailable"))
                                       for payload in prediction_states[first_cause:]):
        failures.append("cause hierarchy disappeared after its first coherent rows were published")

    return failures


def validate_live_cause(states: list[dict[str, object]], selected_target_id: int) -> list[str]:
    failures: list[str] = []
    live = [state.get("payload", {}) for state in states
            if int(state.get("payload", {}).get("pathTargetId", 0)) == selected_target_id and
            bool(state.get("payload", {}).get("predictionEnabled"))]
    building = [payload for payload in live
                if bool(payload.get("predictionBuilding")) or not bool(payload.get("predictionComplete"))]

    if not live:
        failures.append("live demo produced no selected prediction state")
    if not building:
        failures.append("live demo never exposed an in-flight prediction generation")
    if any(int(payload.get("causeTreeRowCount", 0)) == 0 or not bool(payload.get("causeWindowAvailable"))
           for payload in live):
        failures.append("cause hierarchy vanished while live prediction continued presenting future frames")
    if not any(bool(payload.get("visualPacketHasGeometry")) for payload in live):
        failures.append("live demo presented no future geometry")

    return failures


def validate_cause_cache(before: dict[str, object], after: dict[str, object], stepped_frames: int) -> list[str]:
    before_payload = before.get("payload", {})
    after_payload = after.get("payload", {})
    failures: list[str] = []
    build_delta = int(after_payload.get("causeTreeRowBuildCount", 0)) - int(
        before_payload.get("causeTreeRowBuildCount", 0))
    hit_delta = int(after_payload.get("causeTreeRowCacheHitCount", 0)) - int(
        before_payload.get("causeTreeRowCacheHitCount", 0))

    if build_delta != 0:
        failures.append(f"stable cause tree rebuilt {build_delta} times across {stepped_frames} frames")
    if hit_delta < stepped_frames:
        failures.append(f"cause-tree cache served only {hit_delta} hits across {stepped_frames} frames")
    if after_payload.get("causeTreeRowCount") != before_payload.get("causeTreeRowCount"):
        failures.append("cause-tree row count changed while its prediction source was stable")
    if after_payload.get("futureNodeCount") != before_payload.get("futureNodeCount"):
        failures.append("future-node behavior changed during cause-tree cache validation")

    return failures


def vector_distance(left: object, right: object) -> float:
    if not isinstance(left, list) or not isinstance(right, list) or len(left) != 3 or len(right) != 3:
        return float("inf")
    return sum((float(a) - float(b)) ** 2 for a, b in zip(left, right)) ** 0.5


def validate_causal_camera(states: list[dict[str, object]], source: dict[str, object]) -> list[str]:
    failures: list[str] = []
    transition = [state.get("payload", {}) for state in states
                  if int(state.get("payload", {}).get("selectedCauseRow", -1)) >= 0 and
                  int(state.get("payload", {}).get("causeInspectionMode", 0)) in (1, 2)]
    transporting = [payload for payload in transition if int(payload.get("causeInspectionMode", 0)) == 1]

    if len(transporting) < 2:
        return ["causal camera exposed fewer than two transport samples"]

    if any(int(payload.get("causeSourceFrame", -1)) != 0 for payload in transporting):
        failures.append("prediction-root transport did not begin on local future frame zero")
    if any(int(payload.get("causeTargetFrame", -1)) != 0 or
           int(payload.get("causePresentedFrame", -1)) != 0 for payload in transporting):
        failures.append("prediction-root transport moved through an unrelated future frame")

    endpoint = transporting[0].get("cameraPrimaryEye")
    if any(vector_distance(payload.get("cameraPrimaryEye"), endpoint) > 0.001 for payload in transporting):
        failures.append("causal camera endpoint moved during its entry tween")
    if any(payload.get("selectedCameraHash") == source.get("selectedCameraHash") for payload in transporting):
        failures.append("generic replay camera stole the causal slot during transport")
    if vector_distance(transporting[0].get("cameraRenderEye"), source.get("cameraRenderEye")) > 0.1:
        failures.append("causal entry did not start from the visible main camera")
    if vector_distance(transporting[-1].get("cameraRenderEye"), transporting[0].get("cameraRenderEye")) < 0.1:
        failures.append("causal entry render pose never moved toward its endpoint")
    if any(float(payload.get("cameraRenderRollRadians", 1.0)) > 0.001 for payload in transition):
        failures.append("causal camera rolled away from projected world-up")

    completed = next((payload for payload in reversed(transition)
                      if int(payload.get("causeInspectionMode", 0)) == 2), None)
    if completed is None:
        failures.append("causal camera did not reach its detail pose")
    elif vector_distance(completed.get("cameraRenderEye"), completed.get("cameraPrimaryEye")) > 0.001:
        failures.append("completed causal render pose did not land on its prepared endpoint")

    return failures


def validate_causal_return(states: list[dict[str, object]], source: dict[str, object]) -> list[str]:
    failures: list[str] = []
    payloads = [state.get("payload", {}) for state in states]
    settled = next((payload for payload in reversed(payloads)
                    if int(payload.get("causeInspectionMode", -1)) == 0), None)

    if settled is None:
        return ["causal camera did not return to inactive mode"]
    if settled.get("inspectionCameraActive") or int(settled.get("selectedCauseRow", -2)) != -1:
        failures.append("return-to-live retained causal inspection state")
    if settled.get("selectedCameraHash") != source.get("selectedCameraHash"):
        failures.append("return-to-live did not restore the main camera identity")
    if vector_distance(settled.get("cameraRenderEye"), source.get("cameraRenderEye")) > 0.1:
        failures.append("return-to-live did not land on the original main camera pose")
    if abs(float(settled.get("cameraRenderRollRadians", 1.0))) > 0.001:
        failures.append("return-to-live restored a rolled camera pose")

    return failures


def validate_causal_orbit(before: dict[str, object], after: dict[str, object]) -> list[str]:
    failures: list[str] = []
    pivot = before.get("inspectionPivot")

    if int(before.get("inspectionCameraFocusKind", 0)) != 2:
        failures.append("causal orbit fixture did not select a collision manifold")
    if not before.get("inspectionFocusFadeActive") or int(before.get("inspectionFocusObjectCount", 0)) != 2:
        failures.append("collision manifold did not focus exactly its two participating objects")
    if vector_distance(before.get("cameraPrimaryView"), pivot) > 0.001:
        failures.append("causal camera was not aimed at the selected collision before orbit")
    if vector_distance(after.get("inspectionPivot"), pivot) > 0.001 or \
            vector_distance(after.get("cameraPrimaryView"), pivot) > 0.001:
        failures.append("right-drag orbit did not retain the selected collision pivot")
    if vector_distance(after.get("cameraPrimaryEye"), before.get("cameraPrimaryEye")) < 0.1:
        failures.append("right-drag orbit did not move the causal camera eye")
    if not after.get("inspectionCameraActive") or after.get("selectedCauseRow") != before.get("selectedCauseRow") or \
            after.get("causeInspectionMode") != before.get("causeInspectionMode"):
        failures.append("right-drag escaped causal inspection into the free camera")
    before_radius = vector_distance(before.get("cameraPrimaryEye"), pivot)
    after_radius = vector_distance(after.get("cameraPrimaryEye"), pivot)
    if abs(after_radius - before_radius) > 0.01:
        failures.append("right-drag orbit changed camera distance without wheel input")
    if vector_distance(after.get("cameraPrimaryUp"), [0.0, 1.0, 0.0]) > 0.001 or \
            abs(float(after.get("cameraRenderRollRadians", 1.0))) > 0.001:
        failures.append("right-drag orbit rolled the causal camera away from world-up")

    return failures


def validate_causal_focus(payload: dict[str, object]) -> list[str]:
    failures: list[str] = []

    if not payload.get("inspectionPathFocusActive"):
        failures.append("causal selection did not activate prediction-path focus")
    if payload.get("inspectionPathFocusPrimaryId") != payload.get("selectedCausePrimaryId") or \
            payload.get("inspectionPathFocusCounterpartId") != payload.get("selectedCauseCounterpartId"):
        failures.append("focused prediction paths do not match the selected causal identities")
    if int(payload.get("inspectionFocusedPathRangeCount", 0)) <= 0 or \
            int(payload.get("inspectionFocusedPathSegmentCount", 0)) <= 0:
        failures.append("selected causal objects retained no full-opacity prediction path")
    if int(payload.get("inspectionContextPathRangeCount", 0)) <= 0 or \
            int(payload.get("inspectionContextPathSegmentCount", 0)) <= 0:
        failures.append("causal focus fixture exposed no faded context prediction paths")
    if int(payload.get("inspectionPathOpacityMismatchCount", 0)) != 0:
        failures.append("prediction-path alpha does not match its causal identity")
    if int(payload.get("causeContactPointCount", 0)) <= 0:
        failures.append("selected collision published no manifold points")
    if payload.get("submittedCauseContactPointCount") != payload.get("causeContactPointCount"):
        failures.append("published collision manifold was not submitted to rendering")
    if int(payload.get("submittedCauseContactBodyCount", 0)) != 2:
        failures.append("rendered collision manifold did not contain both participating bodies")

    return failures


def validate_body_root_focus(payload: dict[str, object], row_index: int = 0,
                             require_future_frame: bool = False) -> list[str]:
    failures: list[str] = []

    if int(payload.get("inspectionCameraFocusKind", 0)) != 1 or \
            int(payload.get("selectedCauseRow", -1)) != row_index:
        failures.append(f"cause body row {row_index} did not activate body inspection")
    if payload.get("inspectionBodyMarkerSubmitted"):
        failures.append("cause root submitted a duplicate body wireframe")

    selected_frame = int(payload.get("selectedCauseFrame", -1))
    target_frame = int(payload.get("causeTargetFrame", -2))
    presented_frame = int(payload.get("causePresentedFrame", -3))
    replay_frame = int(payload.get("presentedReplayFrame", -4))
    if require_future_frame and selected_frame <= 0:
        failures.append("child cause body did not retain its first affected frame")
    if selected_frame != target_frame:
        failures.append("cause root transport target does not match the selected row frame")
    if int(payload.get("causeInspectionMode", 0)) != 2 or presented_frame != target_frame:
        failures.append("cause root transport did not settle on its target frame")
    if int(payload.get("causeSeekSource", -1)) != 1 or int(payload.get("presentedReplayFrameSource", 0)) != 2:
        failures.append("cause root did not present from the prediction frame bank")
    if replay_frame != target_frame:
        failures.append("cause root visible replay frame does not match the selected row frame")

    return failures


def run_self_test() -> int:
    case = PredictionCase("self-test", "fixture.scene.json", "ball", causal=True)
    valid_payload = {
        "predictionEnabled": True, "predictionComplete": True, "predictionBuilding": False,
        "predictionDirty": False, "predictionRestartPending": False, "predictionGenerationPermitted": True,
        "pathTargetId": 7, "publishedPredictionTargetId": 7, "predictionSourceFrame": 12,
        "publishedPredictionFrames": 3, "publishedPredictionTopologyVersion": 5,
        "pastPathVisible": True, "selectedPastRootPointCount": 3,
        "selectedFutureRootPointCount": 3, "trajectoryRecordCount": 3, "visualPacketHasGeometry": True,
        "drawnEndingWireframeCount": 2, "retainedEndMarkerCount": 2, "endingWireframePathMismatchCount": 0,
        "collisionWireframePathMismatchCount": 0, "incompleteContactFrameCount": 0, "futureNodeCount": 1,
        "contactChildIncomingCount": 1, "contactChildOutgoingCount": 1, "drawnCollisionWireframeCount": 1,
        "retainedEntryMarkerCount": 1, "trajectorySubmitted": True, "submittedPredictionTargetId": 7,
        "submittedPredictionSourceFrame": 12, "submittedPredictionTopologyVersion": 5,
        "submittedGeometryHash": 99, "submittedGeometryBytes": 1024, "submittedFutureTreeReady": True,
        "causeTreeRowCount": 2, "causeWindowAvailable": True,
    }
    if validate_prediction(case, {"payload": valid_payload}):
        return 1

    for field in ("predictionComplete", "visualPacketHasGeometry", "submittedFutureTreeReady"):
        broken = dict(valid_payload)
        broken[field] = False
        if not validate_prediction(case, {"payload": broken}):
            return 1

    broken = dict(valid_payload)
    broken["endingWireframePathMismatchCount"] = 1
    if not validate_prediction(case, {"payload": broken}):
        return 1

    stale_submission = dict(valid_payload)
    stale_submission["submittedPredictionTargetId"] = 6
    if not validate_prediction(case, {"payload": stale_submission}):
        return 1

    stable_generation = {"payload": {"predictionGeneration": 9, "predictionBuilding": False,
                                      "predictionDirty": False, "predictionComplete": True}}
    if validate_prediction_generation_stability(stable_generation, stable_generation, 120):
        return 1
    restarted_generation = {"payload": {"predictionGeneration": 10, "predictionBuilding": True,
                                         "predictionDirty": False, "predictionComplete": False}}
    if not validate_prediction_generation_stability(stable_generation, restarted_generation, 120):
        return 1

    valid_continuity_states = [
        {"payload": {"pathTargetId": 7, "pastPathVisible": True, "selectedPastRootPointCount": 3,
                     "predictionEnabled": True, "predictionComplete": False, "causeTreeRowCount": 1,
                     "causeWindowAvailable": True}},
        {"payload": {"pathTargetId": 7, "pastPathVisible": True, "selectedPastRootPointCount": 4,
                     "predictionEnabled": True, "predictionComplete": True, "causeTreeRowCount": 2,
                     "causeWindowAvailable": True}},
    ]
    if validate_continuity(valid_continuity_states, 7):
        return 1

    broken_continuity_states = [
        valid_continuity_states[1],
        {"payload": {"pathTargetId": 7, "pastPathVisible": True, "selectedPastRootPointCount": 0,
                     "predictionEnabled": True, "predictionComplete": False, "causeTreeRowCount": 0,
                     "causeWindowAvailable": False}},
    ]
    if not validate_continuity(broken_continuity_states, 7):
        return 1

    valid_live_states = [
        {"payload": {"pathTargetId": 7, "publishedPredictionTargetId": 7, "predictionEnabled": True,
                     "predictionBuilding": True, "predictionComplete": False, "publishedPredictionFrames": 3,
                     "causeTreeRowCount": 1, "causeWindowAvailable": True, "visualPacketHasGeometry": True}},
    ]
    if validate_live_cause(valid_live_states, 7):
        return 1

    broken_live_states = [{"payload": dict(valid_live_states[0]["payload"])}]
    broken_live_states[0]["payload"]["causeTreeRowCount"] = 0
    broken_live_states[0]["payload"]["causeWindowAvailable"] = False
    if not validate_live_cause(broken_live_states, 7):
        return 1

    stable_before = {"payload": {"causeTreeRowBuildCount": 4, "causeTreeRowCacheHitCount": 10,
                                  "causeTreeRowCount": 12, "futureNodeCount": 3}}
    stable_after = {"payload": {"causeTreeRowBuildCount": 4, "causeTreeRowCacheHitCount": 250,
                                 "causeTreeRowCount": 12, "futureNodeCount": 3}}
    if validate_cause_cache(stable_before, stable_after, 120):
        return 1

    rebuilt_after = {"payload": dict(stable_after["payload"])}
    rebuilt_after["payload"]["causeTreeRowBuildCount"] = 5
    if not validate_cause_cache(stable_before, rebuilt_after, 120):
        return 1

    source_camera = {"selectedCameraHash": 1, "cameraRenderEye": [0.0, 0.0, 0.0]}
    valid_camera_states = [
        {"payload": {"selectedCauseRow": 0, "causeInspectionMode": 1, "selectedCameraHash": 2,
                     "causeSourceFrame": 0, "causeTargetFrame": 0, "causePresentedFrame": 0,
                     "cameraPrimaryEye": [10.0, 0.0, 0.0], "cameraRenderEye": [0.0, 0.0, 0.0],
                     "cameraRenderRollRadians": 0.0}},
        {"payload": {"selectedCauseRow": 0, "causeInspectionMode": 1, "selectedCameraHash": 2,
                     "causeSourceFrame": 0, "causeTargetFrame": 0, "causePresentedFrame": 0,
                     "cameraPrimaryEye": [10.0, 0.0, 0.0], "cameraRenderEye": [5.0, 0.0, 0.0],
                     "cameraRenderRollRadians": 0.0}},
        {"payload": {"selectedCauseRow": 0, "causeInspectionMode": 2, "selectedCameraHash": 2,
                     "causeSourceFrame": 0, "causeTargetFrame": 0, "causePresentedFrame": 0,
                     "cameraPrimaryEye": [10.0, 0.0, 0.0], "cameraRenderEye": [10.0, 0.0, 0.0],
                     "cameraRenderRollRadians": 0.0}},
    ]
    if validate_causal_camera(valid_camera_states, source_camera):
        return 1

    broken_camera_states = list(valid_camera_states)
    broken_camera_states[1] = {"payload": dict(valid_camera_states[1]["payload"])}
    broken_camera_states[1]["payload"]["selectedCameraHash"] = 1
    if not validate_causal_camera(broken_camera_states, source_camera):
        return 1

    valid_return_states = [
        {"payload": {"causeInspectionMode": 3, "inspectionCameraActive": True,
                     "selectedCauseRow": 0, "selectedCameraHash": 2,
                     "cameraRenderEye": [5.0, 0.0, 0.0], "cameraRenderRollRadians": 0.0}},
        {"payload": {"causeInspectionMode": 0, "inspectionCameraActive": False,
                     "selectedCauseRow": -1, "selectedCameraHash": 1,
                     "cameraRenderEye": [0.0, 0.0, 0.0], "cameraRenderRollRadians": 0.0}},
    ]
    if validate_causal_return(valid_return_states, source_camera):
        return 1
    broken_return_states = list(valid_return_states)
    broken_return_states[-1] = {"payload": dict(valid_return_states[-1]["payload"])}
    broken_return_states[-1]["payload"]["selectedCameraHash"] = 2
    if not validate_causal_return(broken_return_states, source_camera):
        return 1

    orbit_before = {"inspectionCameraFocusKind": 2, "inspectionFocusFadeActive": True,
                    "inspectionFocusObjectCount": 2, "inspectionPivot": [1.0, 2.0, 3.0],
                    "cameraPrimaryEye": [1.0, 2.0, 13.0], "cameraPrimaryView": [1.0, 2.0, 3.0],
                    "cameraPrimaryUp": [0.0, 1.0, 0.0], "cameraRenderRollRadians": 0.0,
                    "inspectionCameraActive": True, "selectedCauseRow": 2, "causeInspectionMode": 2}
    orbit_before.update({"inspectionPathFocusActive": True,
                         "selectedCausePrimaryId": 10, "selectedCauseCounterpartId": 11,
                         "inspectionPathFocusPrimaryId": 10, "inspectionPathFocusCounterpartId": 11,
                         "inspectionFocusedPathRangeCount": 2, "inspectionFocusedPathSegmentCount": 8,
                         "inspectionContextPathRangeCount": 4, "inspectionContextPathSegmentCount": 16,
                         "inspectionPathOpacityMismatchCount": 0, "causeContactPointCount": 2,
                         "submittedCauseContactPointCount": 2, "submittedCauseContactBodyCount": 2})
    if validate_causal_focus(orbit_before):
        return 1
    valid_root = {"inspectionCameraFocusKind": 1, "selectedCauseRow": 0,
                  "inspectionBodyMarkerSubmitted": False, "selectedCauseFrame": 0,
                  "causeTargetFrame": 0, "causePresentedFrame": 0, "causeInspectionMode": 2,
                  "causeSeekSource": 1, "presentedReplayFrame": 0, "presentedReplayFrameSource": 2}
    if validate_body_root_focus(valid_root):
        return 1
    broken_root = dict(valid_root)
    broken_root["presentedReplayFrame"] = 1
    if not validate_body_root_focus(broken_root):
        return 1
    wireframe_root = dict(valid_root)
    wireframe_root["inspectionBodyMarkerSubmitted"] = True
    if not validate_body_root_focus(wireframe_root):
        return 1
    broken_focus = dict(orbit_before)
    broken_focus["inspectionPathFocusCounterpartId"] = 99
    if not validate_causal_focus(broken_focus):
        return 1
    orbit_after = dict(orbit_before)
    orbit_after["cameraPrimaryEye"] = [11.0, 2.0, 3.0]
    if validate_causal_orbit(orbit_before, orbit_after):
        return 1
    broken_orbit = dict(orbit_after)
    broken_orbit["cameraPrimaryView"] = [0.0, 0.0, 0.0]
    if not validate_causal_orbit(orbit_before, broken_orbit):
        return 1
    escaped_orbit = dict(orbit_after)
    escaped_orbit["inspectionCameraActive"] = False
    escaped_orbit["causeInspectionMode"] = 0
    return 0 if validate_causal_orbit(orbit_before, escaped_orbit) else 1


def run_matrix(session: Path, executable: Path, case_label: str | None = None) -> int:
    if launch(session, executable, None) != 0:
        return 1

    results: list[dict[str, object]] = []
    connection = SkarnessConnection(session)
    stop_requested = False
    try:
        selected_cases = tuple(case for case in CASES if case_label is None or case.label == case_label)
        for case in selected_cases:
            require_applied(connection, "replay.set_prediction_enabled", {"enabled": False})
            if case.capacity_transition:
                # A clean scene cannot expose uneven retained per-frame capacity.
                # Warm a shorter 300-body bank before loading the longer 11-body case.
                warm_mismatched_prediction_capacity(connection)
            require_applied(connection, "scene.load" if case.scene else "scene.load_demo",
                            {"name": case.scene} if case.scene else {})

            cleared_state = query_latest_state(session)
            if cleared_state and bool(cleared_state.get("payload", {}).get("trajectorySubmitted")):
                raise RuntimeError(f"{case.label}: prior render submission survived the scene transition")

            if case.warmup_ticks:
                require_applied(connection, "run.step", {"count": case.warmup_ticks})

            if case.continuity:
                require_applied(connection, "replay.set_past_path_visible", {"visible": True})

            require_applied(connection, "replay.set_prediction_horizon", {"seconds": case.horizon})
            if case.target_name:
                targets = ({"name": case.target_name},)
            elif case.target_candidates:
                targets = tuple({"sceneObjectId": candidate} for candidate in case.target_candidates)
            else:
                targets = ({"sceneObjectId": case.target_id},)

            live_failures: list[str] = []
            if case.live_cause_stability:
                live_target = targets[0]
                require_applied(connection, "prediction.select_target", live_target)
                require_applied(connection, "replay.set_prediction_enabled", {"enabled": True})
                require_applied(connection, "state.subscribe", {"topics": ["replay.state"]})
                require_applied(connection, "run.resume")
                live_states = collect_live_replay_states(connection, 3.0)
                require_applied(connection, "run.pause")
                require_applied(connection, "state.subscribe", {"topics": []})
                selected_target_id = next(
                    (int(state.get("payload", {}).get("pathTargetId", 0)) for state in reversed(live_states)
                     if int(state.get("payload", {}).get("pathTargetId", 0)) != 0), 0)
                live_failures = validate_live_cause(live_states, selected_target_id)
                require_applied(connection, "replay.set_prediction_enabled", {"enabled": False})

            state: dict[str, object] | None = None
            failures: list[str] = []
            selected_target: dict[str, object] | None = None
            attempt_start_frame = 0

            for target_index, target in enumerate(targets):
                latest_before = query_latest_state(session)
                attempt_start_frame = int(latest_before.get("renderFrame", 0)) if latest_before else 0
                require_applied(connection, "prediction.select_target", target)
                require_applied(connection, "replay.set_prediction_enabled", {"enabled": True})
                require_applied(connection, "run.until", {"condition": "prediction.complete", "maxFrames": 3000})
                require_applied(connection, "run.step_frames", {"count": 4})

                state = query_latest_state(session)
                if state is None:
                    raise RuntimeError(f"{case.label}: replay.state was not published")

                if case.causal and int(state.get("payload", {}).get("futureNodeCount", 0)) > 0:
                    require_applied(connection, "run.until", {"condition": "prediction.causal_rendered",
                                                               "maxFrames": 3000})
                    state = query_latest_state(session)
                    if state is None:
                        raise RuntimeError(f"{case.label}: rendered replay.state was not published")

                baseline_failures = validate_prediction(replace(case, causal=False), state)
                failures = live_failures + (baseline_failures or validate_prediction(case, state))
                selected_target = target

                if case.continuity:
                    failures.extend(validate_continuity(replay_states_after(session, attempt_start_frame),
                                                        int(state["payload"].get("pathTargetId", 0))))

                if case.cache_stability and not failures:
                    stable_before = state
                    stable_frames = 120
                    require_applied(connection, "run.step_frames", {"count": stable_frames})
                    state = query_latest_state(session)
                    if state is None:
                        raise RuntimeError(f"{case.label}: stable replay.state was not published")
                    failures.extend(validate_cause_cache(stable_before, state, stable_frames))
                    failures.extend(validate_prediction(case, state))

                if case.generation_stability and not failures:
                    stable_before = state
                    stable_frames = 120
                    require_applied(connection, "run.step_frames", {"count": stable_frames})
                    state = query_latest_state(session)
                    if state is None:
                        raise RuntimeError(f"{case.label}: stable replay.state was not published")
                    failures.extend(validate_prediction_generation_stability(stable_before, state, stable_frames))
                    failures.extend(validate_prediction(case, state))

                if case.retarget_name and not failures:
                    require_applied(connection, "run.step", {"count": case.retarget_after_ticks})
                    require_applied(connection, "prediction.select_target", {"name": case.retarget_name})
                    require_applied(connection, "run.until", {"condition": "prediction.complete", "maxFrames": 3000})
                    require_applied(connection, "run.step_frames", {"count": 4})
                    state = query_latest_state(session)
                    if state is None:
                        raise RuntimeError(f"{case.label}: retargeted replay.state was not published")

                    if case.causal and int(state.get("payload", {}).get("futureNodeCount", 0)) > 0:
                        require_applied(connection, "run.until", {"condition": "prediction.causal_rendered",
                                                                   "maxFrames": 3000})
                        state = query_latest_state(session)
                        if state is None:
                            raise RuntimeError(f"{case.label}: retargeted causal replay.state was not published")

                    selected_target = {"name": case.retarget_name}
                    failures.extend(validate_prediction(case, state))

                if case.causal_camera and not failures:
                    camera_source = state.get("payload", {})
                    require_applied(connection, "state.subscribe", {"topics": ["replay.state"]})
                    require_applied(connection, "replay.select_cause_row", {"row": 0})
                    root_states = collect_live_replay_states(connection, 2.0)
                    require_applied(connection, "run.until",
                                    {"condition": "camera.inspection_settled", "maxFrames": 1000})
                    root_states.extend(collect_live_replay_states(connection, 0.05))
                    root_focus = root_states[-1].get("payload", {}) if root_states else {}
                    failures.extend(validate_body_root_focus(root_focus))
                    failures.extend(validate_causal_camera(root_states, camera_source))
                    if case.screenshot:
                        root_screenshot = (session / f"{case.label}-causal-root.png").resolve()
                        require_applied(connection, "capture.screenshot", {"path": str(root_screenshot)})
                    require_applied(connection, "replay.select_cause_row", {"row": 1})
                    if case.causal_child_body:
                        child_states = collect_live_replay_states(connection, 2.0)
                        require_applied(connection, "run.until",
                                        {"condition": "camera.inspection_settled", "maxFrames": 1000})
                        child_states.extend(collect_live_replay_states(connection, 0.05))
                        child_focus = child_states[-1].get("payload", {}) if child_states else {}
                        failures.extend(validate_body_root_focus(child_focus, 1, True))
                    else:
                        collect_live_replay_states(connection, 0.25)
                    require_applied(connection, "replay.select_cause_row", {"row": 2})
                    manifold_states = collect_live_replay_states(connection, 2.0)
                    orbit_before = manifold_states[-1].get("payload", {}) if manifold_states else {}
                    failures.extend(validate_causal_focus(orbit_before))
                    if case.screenshot:
                        causal_screenshot = (session / f"{case.label}-causal-focus.png").resolve()
                        require_applied(connection, "capture.screenshot", {"path": str(causal_screenshot)})
                    require_applied(connection, "input.pointer_drag",
                                    {"button": "right", "x": 800, "y": 450,
                                     "deltaX": -65, "deltaY": 20})
                    orbit_states = collect_live_replay_states(connection, 0.25)
                    orbit_after = orbit_states[-1].get("payload", {}) if orbit_states else {}
                    failures.extend(validate_causal_orbit(orbit_before, orbit_after))
                    require_applied(connection, "replay.return_to_live")
                    require_applied(connection, "run.until", {"condition": "camera.main_restored", "maxFrames": 1000})
                    return_states = collect_live_replay_states(connection, 0.05)
                    require_applied(connection, "state.subscribe", {"topics": []})
                    failures.extend(validate_causal_return(return_states, camera_source))

                if not failures or live_failures or baseline_failures or target_index + 1 == len(targets):
                    break

                require_applied(connection, "replay.set_prediction_enabled", {"enabled": False})

            if case.screenshot:
                screenshot = (session / f"{case.label}.png").resolve()
                require_applied(connection, "capture.screenshot", {"path": str(screenshot)})

            assert state is not None
            payload = state["payload"]
            results.append({
                "case": case.label,
                "scene": case.scene or "demo",
                "sceneGeneration": state.get("sceneGeneration"),
                "predictionGeneration": payload.get("predictionGeneration"),
                "selectedTarget": selected_target,
                "futureNodes": payload.get("futureNodeCount"),
                "collisionWireframes": payload.get("drawnCollisionWireframeCount"),
                "endingWireframes": payload.get("drawnEndingWireframeCount"),
                "submittedSegments": payload.get("submittedSegmentCount"),
                "causeTreeBuilds": payload.get("causeTreeRowBuildCount"),
                "causeTreeCacheHits": payload.get("causeTreeRowCacheHitCount"),
                "causalCameraInputRoute": case.causal_camera,
                "failures": failures,
            })
            print(json.dumps(results[-1], separators=(",", ":")), flush=True)

        require_applied(connection, "session.stop")
        stop_requested = True
    finally:
        if not stop_requested:
            try:
                require_applied(connection, "session.stop")
            except (OSError, RuntimeError, TimeoutError):
                pass
        connection.close()

    report = {"ok": not any(row["failures"] for row in results), "caseCount": len(results), "results": results}
    report_path = session / "prediction-matrix.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["ok"] else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, default=Path("TestOutput/skarness/prediction-matrix"))
    parser.add_argument("--exe", type=Path, default=Path("Automation/SKULLBONEZ_CORE.exe"))
    parser.add_argument("--case", choices=tuple(case.label for case in CASES))
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    return run_self_test() if arguments.self_test else run_matrix(arguments.session, arguments.exe, arguments.case)


if __name__ == "__main__":
    sys.exit(main())
