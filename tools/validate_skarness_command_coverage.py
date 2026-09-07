#!/usr/bin/env python3
"""Verify the discoverable Skarness catalog and shared replay command routes."""

from __future__ import annotations

import argparse
import re
from pathlib import Path
import subprocess
import sys
import uuid

from skarness import SkarnessConnection


REPO = Path(__file__).resolve().parents[1]
DEFAULT_SCENE = REPO / "SkullbonezData" / "scenes" / "interaction_replay_prediction_harness.scene.json"
EXPECTED_COMMANDS = {
    "capabilities.get", "session.stop", "capture.screenshot", "scene.load", "scene.reset", "scene.load_demo",
    "scene.object.list", "scene.object.resolve", "scene.object.select", "scene.object.clear_selection", "run.pause",
    "run.resume", "run.step", "run.step_frames", "run.until", "replay.set_recording_enabled",
    "replay.set_retention_seconds", "replay.set_memory_budget_mib", "replay.jump_to_start", "replay.jump_to_end",
    "replay.set_playback_paused", "replay.step_backward", "replay.step_forward", "replay.set_reveal_speed",
    "replay.scrub", "replay.seek_frame", "replay.set_prediction_enabled", "replay.set_prediction_detail",
    "replay.set_prediction_horizon", "replay.set_velocity_edit_enabled", "replay.set_ragdoll_visuals_enabled",
    "replay.set_past_path_visible", "replay.set_guide_arcs_enabled", "replay.set_path_color_mode",
    "replay.set_intercept_target", "replay.velocity_preview", "replay.velocity_commit", "replay.velocity_cancel",
    "prediction.reveal_reset", "prediction.reveal_advance", "replay.restore_branch", "replay.save", "replay.load",
    "replay.return_to_live", "replay.select_cause_row", "replay.select_cause", "replay.set_cause_inspector_open",
    "replay.set_cause_filter_text", "replay.set_cause_filter", "replay.set_cause_inspector_tab",
    "replay.close_cause_detail", "replay.return_from_cause", "replay.copy_cause_record",
    "replay.set_porkchop_visible", "replay.select_porkchop_cell", "replay.set_trip_time_of_flight",
    "replay.trip_plan", "replay.trip_commit", "replay.trip_cancel", "prediction.forecast_start",
    "prediction.forecast_reset", "prediction.forecast_stop", "prediction.select_target", "replay.set_path_target",
    "camera.orbit_inspection", "state.subscribe", "input.pointer_drag", "input.set_arrows",
}
EXPECTED_OPERATOR_REPLAY_CONTROLS = {
    "SetRecordingEnabled", "JumpToStart", "JumpToEnd", "TogglePlayPause", "StepBackward", "StepForward",
    "SetRevealSpeed", "Scrub", "TogglePrediction", "SetPredictionHorizon", "RestoreBranch", "Save", "Load",
    "ReturnToLive", "SelectCauseRow", "SetMemoryPolicy",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def enum_members(path: Path, enum_name: str) -> set[str]:
    source = path.read_text(encoding="utf-8")
    match = re.search(rf"enum class {re.escape(enum_name)}[^{{]*\{{(?P<body>.*?)\}};", source, re.DOTALL)
    require(match is not None, f"could not find {enum_name}")
    body = re.sub(r"//.*", "", match.group("body"))
    return {
        token.strip().split("=")[0].strip()
        for token in body.split(",")
        if token.strip()
    }


def send(connection: SkarnessConnection, command: str, arguments: dict[str, object] | None = None) -> dict[str, object]:
    result = connection.wait(connection.send(command, arguments))
    require(result.get("status") in {"applied", "rejected"}, f"{command} was not parsed: {result}")
    return result


def require_applied(connection: SkarnessConnection, command: str,
                    arguments: dict[str, object] | None = None) -> dict[str, object]:
    result = send(connection, command, arguments)
    require(result.get("status") == "applied", f"{command} failed: {result}")
    return result


def validate_catalog(connection: SkarnessConnection) -> dict[str, object]:
    capability = require_applied(connection, "capabilities.get")
    catalog = capability.get("catalog")
    require(isinstance(catalog, list), "capabilities omitted the typed catalog")
    names = [row.get("name") for row in catalog]
    require(len(names) == len(set(names)), "capability catalog contains duplicate names")
    require(set(names) == EXPECTED_COMMANDS, f"capability drift: missing={EXPECTED_COMMANDS - set(names)} extra={set(names) - EXPECTED_COMMANDS}")
    for row in catalog:
        require(isinstance(row.get("owner"), str) and row["owner"], f"catalog owner missing: {row}")
        require(isinstance(row.get("arguments"), str) and row["arguments"], f"catalog schema missing: {row}")
        require(row.get("available") is True, f"automated command unexpectedly unavailable: {row}")
    return capability


def validate_player_control_inventory() -> None:
    actual = enum_members(REPO / "SkullbonezSource" / "Runtime" / "Interaction" / "OperatorEditorExchange.h",
                          "OperatorEditorReplayCommandType")
    require(actual == EXPECTED_OPERATOR_REPLAY_CONTROLS,
            f"player replay controls changed without a Skarness coverage decision: missing={EXPECTED_OPERATOR_REPLAY_CONTROLS - actual} extra={actual - EXPECTED_OPERATOR_REPLAY_CONTROLS}")


def validate_routes(connection: SkarnessConnection, output: Path) -> None:
    listed = require_applied(connection, "scene.object.list").get("result", {}).get("objects", [])
    require(listed, "scene.object.list returned no physics objects")
    selected = listed[0]
    object_id = int(selected["sceneObjectId"])
    name = str(selected["name"])

    by_id = require_applied(connection, "scene.object.resolve", {"sceneObjectId": object_id})
    by_name = require_applied(connection, "scene.object.resolve", {"name": name})
    require(by_id["result"]["objects"][0]["sceneObjectId"] == object_id, "id resolution changed identity")
    require(by_name["result"]["objects"][0]["sceneObjectId"] == object_id, "name resolution changed identity")
    require(send(connection, "scene.object.resolve", {"sceneObjectId": 0xFFFFFFFF}).get("status") == "rejected",
            "stale scene identity unexpectedly resolved")

    require_applied(connection, "scene.object.select", {"scope": "inspect", "sceneObjectId": object_id})
    require_applied(connection, "replay.set_intercept_target", {"sceneObjectId": object_id})
    require_applied(connection, "replay.set_prediction_enabled", {"enabled": True})
    require_applied(connection, "replay.set_prediction_detail", {"highDetail": True})
    require_applied(connection, "replay.set_velocity_edit_enabled", {"enabled": True})
    require_applied(connection, "replay.velocity_preview", {"linear": [0.0, 0.0, 0.0], "angular": [0.0, 0.0, 0.0]})
    require_applied(connection, "replay.velocity_cancel")
    require_applied(connection, "scene.object.clear_selection", {"scope": "inspect"})

    numeric = [
        ("replay.set_retention_seconds", {"seconds": 45}, "seconds", 45),
        ("replay.set_memory_budget_mib", {"mib": 128}, "mib", 128),
        ("replay.set_reveal_speed", {"rate": 1.5}, "rate", 1.5),
        ("replay.set_prediction_horizon", {"seconds": 7.5}, "seconds", 7.5),
        ("replay.scrub", {"normalized": 1.0}, "normalized", 1.0),
        ("replay.set_trip_time_of_flight", {"seconds": 12.0}, "seconds", 12.0),
    ]
    for command, arguments, key, expected in numeric:
        first = require_applied(connection, command, arguments)
        second = require_applied(connection, command, arguments)
        require(first.get("result", {}).get(key) == expected, f"{command} omitted its applied numeric value")
        require(second.get("result", {}).get(key) == expected, f"{command} was not idempotent")

    for mode in ("lane", "velocity", "time", "object", "causal"):
        require_applied(connection, "replay.set_path_color_mode", {"mode": mode})
    for command, arguments in (
        ("replay.set_recording_enabled", {"enabled": True}),
        ("replay.set_playback_paused", {"paused": True}),
        ("replay.set_ragdoll_visuals_enabled", {"enabled": True}),
        ("replay.set_past_path_visible", {"visible": True}),
        ("replay.set_guide_arcs_enabled", {"enabled": True}),
        ("replay.set_cause_filter_text", {"text": "impact"}),
        ("replay.set_cause_filter", {"filter": "contacts"}),
        ("replay.set_cause_inspector_tab", {"tab": "raw"}),
        ("replay.set_cause_inspector_open", {"open": True}),
        ("replay.close_cause_detail", {}),
        ("replay.set_porkchop_visible", {"visible": True}),
        ("replay.trip_plan", {}),
        ("replay.trip_cancel", {}),
        ("prediction.reveal_reset", {}),
        ("prediction.reveal_advance", {"frames": 3}),
        ("prediction.forecast_start", {}),
        ("prediction.forecast_stop", {}),
    ):
        require_applied(connection, command, arguments)

    replay_path = output / "explicit-save.sbr2"
    require_applied(connection, "replay.save", {"path": str(replay_path)})
    require(send(connection, "replay.select_cause", {"row": 0, "sceneObjectId": object_id, "frame": 0,
                 "generation": 0, "bankEpoch": 0, "topologyVersion": 0, "publicationVersion": 0}).get("status") == "rejected",
            "stale full cause identity unexpectedly applied")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=REPO / "Automation" / "SKULLBONEZ_CORE.exe")
    parser.add_argument("--scene", type=Path, default=DEFAULT_SCENE)
    parser.add_argument("--output-root", type=Path, default=REPO / "TestOutput" / "skarness")
    args = parser.parse_args()
    validate_player_control_inventory()
    session = args.output_root / f"command-coverage-{uuid.uuid4().hex[:8]}"
    launch = subprocess.run([sys.executable, str(REPO / "tools" / "skarness.py"), "launch", "--session", str(session),
                             "--exe", str(args.exe), "--scene", str(args.scene), "--hidden"], cwd=REPO,
                            check=False, capture_output=True, text=True)
    require(launch.returncode == 0, f"launch failed: {launch.stderr or launch.stdout}")
    connection = SkarnessConnection(session)
    try:
        validate_catalog(connection)
        validate_routes(connection, session)
        require_applied(connection, "session.stop")
    finally:
        connection.close()
    print(f"PASS: Skarness command catalog and shared routes ({session})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
