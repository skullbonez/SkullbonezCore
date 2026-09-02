#!/usr/bin/env python3
"""Exercise prediction across scene transitions through one Skarness session."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from dataclasses import replace
import json
from pathlib import Path
import sys

from skarness import SkarnessConnection, launch, query_latest_state


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


CASES = (
    PredictionCase("ragdoll-wall-first", "prediction_ragdoll_wall_200.scene.json", "prediction_striker_ball",
                   horizon=20.0, warmup_ticks=0, causal=True, screenshot=True),
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
    # Generated demo placement is intentionally time-seeded. Search a bounded,
    # stable ID prefix for a body with actual causal descendants instead of
    # turning one random body's lack of contacts into a flaky renderer result.
    PredictionCase("demo", None, target_candidates=(1, 2, 3, 4, 5, 6, 7, 8), horizon=20.0,
                   warmup_ticks=120, causal=True, screenshot=True),
    PredictionCase("ragdoll-wall-after-transitions", "prediction_ragdoll_wall_200.scene.json",
                   "prediction_striker_ball", horizon=20.0, warmup_ticks=0, causal=True, screenshot=True),
)


def require_applied(connection: SkarnessConnection, command: str, arguments: dict[str, object] | None = None) -> None:
    result = connection.wait(connection.send(command, arguments or {}))
    if result.get("status") != "applied":
        raise RuntimeError(f"{command} failed: {json.dumps(result, separators=(',', ':'))}")


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

    return failures


def run_self_test() -> int:
    case = PredictionCase("self-test", "fixture.scene.json", "ball", causal=True)
    valid_payload = {
        "predictionEnabled": True, "predictionComplete": True, "predictionBuilding": False,
        "predictionDirty": False, "predictionRestartPending": False, "predictionGenerationPermitted": True,
        "pathTargetId": 7, "publishedPredictionTargetId": 7, "publishedPredictionFrames": 3,
        "selectedFutureRootPointCount": 3, "trajectoryRecordCount": 3, "visualPacketHasGeometry": True,
        "drawnEndingWireframeCount": 2, "retainedEndMarkerCount": 2, "endingWireframePathMismatchCount": 0,
        "collisionWireframePathMismatchCount": 0, "incompleteContactFrameCount": 0, "futureNodeCount": 1,
        "contactChildIncomingCount": 1, "contactChildOutgoingCount": 1, "drawnCollisionWireframeCount": 1,
        "retainedEntryMarkerCount": 1, "submittedFutureTreeReady": True,
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
    return 0 if validate_prediction(case, {"payload": broken}) else 1


def run_matrix(session: Path, executable: Path) -> int:
    if launch(session, executable, None) != 0:
        return 1

    results: list[dict[str, object]] = []
    connection = SkarnessConnection(session)
    stop_requested = False
    try:
        for case in CASES:
            require_applied(connection, "replay.set_prediction_enabled", {"enabled": False})
            require_applied(connection, "scene.load" if case.scene else "scene.load_demo",
                            {"name": case.scene} if case.scene else {})

            if case.warmup_ticks:
                require_applied(connection, "run.step", {"count": case.warmup_ticks})

            require_applied(connection, "replay.set_prediction_horizon", {"seconds": case.horizon})
            if case.target_name:
                targets = ({"name": case.target_name},)
            elif case.target_candidates:
                targets = tuple({"sceneObjectId": candidate} for candidate in case.target_candidates)
            else:
                targets = ({"sceneObjectId": case.target_id},)

            state: dict[str, object] | None = None
            failures: list[str] = []
            selected_target: dict[str, object] | None = None

            for target_index, target in enumerate(targets):
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
                failures = baseline_failures or validate_prediction(case, state)
                selected_target = target

                if not failures or baseline_failures or target_index + 1 == len(targets):
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
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    return run_self_test() if arguments.self_test else run_matrix(arguments.session, arguments.exe)


if __name__ == "__main__":
    sys.exit(main())
