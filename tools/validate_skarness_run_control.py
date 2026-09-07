#!/usr/bin/env python3
"""Verify one-shot Skarness clients preserve explicit run-control state."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
import uuid

from skarness import SkarnessConnection


REPO = Path(__file__).resolve().parents[1]
STATE_KINDS = {"snapshot", "append", "change", "evict", "reset", "state"}


def require_applied(session: Path, command: str, arguments: dict[str, object] | None = None) -> None:
    connection = SkarnessConnection(session)
    try:
        result = connection.wait(connection.send(command, arguments))
    finally:
        connection.close()
    if result.get("status") != "applied":
        raise RuntimeError(f"{command} failed: {result}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def launch_session(session: Path, executable: Path, scene: Path, manual: bool = False) -> None:
    command = [
        sys.executable,
        str(REPO / "tools" / "skarness.py"),
        "launch",
        "--session",
        str(session),
        "--exe",
        str(executable),
        "--scene",
        str(scene),
        "--hidden",
    ]
    if manual:
        command.append("--manual")
    launched = subprocess.run(command, cwd=REPO, check=False, capture_output=True, text=True)
    if launched.returncode != 0:
        raise RuntimeError(f"Skarness launch failed: {launched.stderr or launched.stdout}")


def subscribe_scene(connection: SkarnessConnection) -> None:
    subscribed = connection.wait(connection.send("state.subscribe", {"topics": ["scene.state"]}))
    require(subscribed.get("status") == "applied", f"state.subscribe failed: {subscribed}")


def read_scene_state(connection: SkarnessConnection) -> dict[str, object]:
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        event = connection.read_event()
        if event.get("kind") in STATE_KINDS and event.get("topic") == "scene.state":
            return event
    raise RuntimeError("scene.state was not published before timeout")


def read_request_status(
    connection: SkarnessConnection,
    request_id: str,
    statuses: set[str],
) -> dict[str, object]:
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        event = connection.read_event()
        if event.get("requestId") == request_id and event.get("status") in statuses:
            return event
    raise RuntimeError(f"request {request_id} did not reach {sorted(statuses)}")


def trace_rows(session: Path) -> list[dict[str, object]]:
    trace = session / "runtime.skarness.ndjson"
    return [json.loads(line) for line in trace.read_text(encoding="utf-8").splitlines() if line]


def command_state_window(
    session: Path,
    request_id: str,
    terminal_status: str,
) -> tuple[list[dict[str, object]], dict[str, object]]:
    rows = trace_rows(session)
    accepted = next(
        row for row in rows if row.get("requestId") == request_id and row.get("status") == "accepted"
    )
    terminal = next(
        row for row in rows if row.get("requestId") == request_id and row.get("status") == terminal_status
    )
    states = [
        row
        for row in rows
        if row.get("kind") in STATE_KINDS
        and row.get("topic") == "scene.state"
        and int(accepted["sequence"]) < int(row["sequence"]) < int(terminal["sequence"])
    ]
    require(states, f"{request_id} acknowledged before any after-render state was flushed")
    require(
        int(states[-1]["sequence"]) < int(terminal["sequence"]),
        f"{request_id} terminal acknowledgement preceded its final state",
    )
    return states, terminal


def scene_frame_before_command(session: Path, request_id: str) -> int:
    rows = trace_rows(session)
    accepted = next(
        row for row in rows if row.get("requestId") == request_id and row.get("status") == "accepted"
    )
    preceding = [
        row
        for row in rows
        if row.get("kind") in STATE_KINDS
        and row.get("topic") == "scene.state"
        and int(row["sequence"]) < int(accepted["sequence"])
    ]
    require(preceding, f"{request_id} has no preceding after-render state")
    return int(preceding[-1]["sceneFrame"])


def latest_scene_frame(session: Path) -> int:
    states = [
        row
        for row in trace_rows(session)
        if row.get("kind") in STATE_KINDS and row.get("topic") == "scene.state"
    ]
    require(states, "runtime trace contains no scene.state rows")
    return int(states[-1]["sceneFrame"])


def wait_reconnected_terminal(
    connection: SkarnessConnection,
    request_id: str,
) -> dict[str, object]:
    first = read_request_status(connection, request_id, {"applied", "rejected", "duplicate"})
    if first.get("status") != "duplicate":
        return first
    return read_request_status(connection, request_id, {"applied", "rejected"})


def validate(session: Path, executable: Path, scene: Path) -> dict[str, object]:
    launch_session(session, executable, scene)
    connection: SkarnessConnection | None = None
    reconnected: SkarnessConnection | None = None

    try:
        connection = SkarnessConnection(session)
        subscribe_scene(connection)
        initial = read_scene_state(connection)
        still_paused = read_scene_state(connection)
        require(initial.get("paused") is True, f"new session was not paused: {initial}")
        require(still_paused.get("paused") is True, f"paused state was not retained: {still_paused}")
        require(
            int(initial["sceneFrame"]) == int(still_paused["sceneFrame"]),
            "physics advanced before an explicit run command",
        )

        resumed = connection.wait(connection.send("run.resume", request_id="run-control-resume"))
        require(resumed.get("status") == "applied", f"run.resume failed: {resumed}")
        running = read_scene_state(connection)
        require(running.get("paused") is False, f"run.resume did not apply: {running}")

        paused_result = connection.wait(connection.send("run.pause", request_id="run-control-pause"))
        require(paused_result.get("status") == "applied", f"run.pause failed: {paused_result}")
        paused = read_scene_state(connection)
        require(paused.get("paused") is True, f"run.pause did not apply: {paused}")
        stepped_result = connection.wait(
            connection.send("run.step", {"count": 3}, request_id="run-control-step-ticks")
        )
        require(stepped_result.get("status") == "applied", f"run.step failed: {stepped_result}")
        tick_states, _ = command_state_window(session, "run-control-step-ticks", "applied")
        tick_start = scene_frame_before_command(session, "run-control-step-ticks")
        require(len(tick_states) == 3, f"run.step emitted {len(tick_states)} after-render states instead of 3")
        require(
            [int(state["sceneFrame"]) for state in tick_states]
            == [tick_start + 1, tick_start + 2, tick_start + 3],
            f"run.step did not advance exactly three fixed ticks: {tick_states}",
        )

        render_result = connection.wait(
            connection.send("run.step_frames", {"count": 4}, request_id="run-control-step-frames")
        )
        require(render_result.get("status") == "applied", f"run.step_frames failed: {render_result}")
        render_states, _ = command_state_window(session, "run-control-step-frames", "applied")
        render_start = scene_frame_before_command(session, "run-control-step-frames")
        require(len(render_states) == 4, f"run.step_frames emitted {len(render_states)} states instead of 4")
        require(
            {int(state["sceneFrame"]) for state in render_states} == {render_start},
            f"render-only stepping advanced physics: {render_states}",
        )

        immediate = connection.wait(
            connection.send(
                "run.until",
                {"condition": "camera.main_restored", "maxFrames": 5},
                request_id="run-control-until-immediate",
            )
        )
        require(immediate.get("status") == "applied", f"already-true run.until failed: {immediate}")
        immediate_states, _ = command_state_window(session, "run-control-until-immediate", "applied")
        require(len(immediate_states) == 1, "already-true run.until did not finish on the first after-render sample")

        frame_timeout = connection.wait(
            connection.send(
                "run.until",
                {"condition": "camera.inspection_settled", "maxFrames": 3},
                request_id="run-control-until-frame-timeout",
            )
        )
        require(frame_timeout.get("status") == "rejected", f"frame timeout unexpectedly applied: {frame_timeout}")
        require(
            "limitKind=frames limit=3 observations=3" in str(frame_timeout.get("reason", "")),
            f"frame timeout omitted exact evidence: {frame_timeout}",
        )
        frame_timeout_states, _ = command_state_window(session, "run-control-until-frame-timeout", "rejected")
        frame_timeout_start = scene_frame_before_command(session, "run-control-until-frame-timeout")
        require(len(frame_timeout_states) == 3, "maxFrames timeout did not consume exactly three render frames")
        require(
            {int(state["sceneFrame"]) for state in frame_timeout_states} == {frame_timeout_start},
            "maxFrames timeout advanced physics",
        )

        tick_timeout = connection.wait(
            connection.send(
                "run.until",
                {"condition": "camera.inspection_settled", "maxTicks": 3},
                request_id="run-control-until-tick-timeout",
            )
        )
        require(tick_timeout.get("status") == "rejected", f"tick timeout unexpectedly applied: {tick_timeout}")
        require(
            "limitKind=ticks limit=3 observations=3" in str(tick_timeout.get("reason", "")),
            f"tick timeout omitted exact evidence: {tick_timeout}",
        )
        tick_timeout_states, _ = command_state_window(session, "run-control-until-tick-timeout", "rejected")
        tick_timeout_start = scene_frame_before_command(session, "run-control-until-tick-timeout")
        require(
            [int(state["sceneFrame"]) for state in tick_timeout_states]
            == [tick_timeout_start + 1, tick_timeout_start + 2, tick_timeout_start + 3],
            "maxTicks timeout did not advance exactly three fixed ticks",
        )

        ambiguous = connection.wait(
            connection.send(
                "run.until",
                {"condition": "camera.main_restored", "maxFrames": 2, "maxTicks": 2},
                request_id="run-control-until-ambiguous",
            )
        )
        require(ambiguous.get("status") == "rejected", f"ambiguous run.until was accepted: {ambiguous}")

        reconnect_id = "run-control-step-reconnect"
        connection.send("run.step", {"count": 25}, request_id=reconnect_id)
        read_request_status(connection, reconnect_id, {"accepted"})
        connection.close()

        time.sleep(0.15)
        disconnected_frame = latest_scene_frame(session)
        time.sleep(0.15)
        require(
            latest_scene_frame(session) == disconnected_frame,
            "physics continued stepping after the controlling pipe disconnected",
        )

        reconnected = SkarnessConnection(session)
        reconnected.send("run.step", {"count": 25}, request_id=reconnect_id)
        reconnect_result = wait_reconnected_terminal(reconnected, reconnect_id)
        require(reconnect_result.get("status") == "applied", f"reconnected step failed: {reconnect_result}")
        reconnect_states, _ = command_state_window(session, reconnect_id, "applied")
        reconnect_start = scene_frame_before_command(session, reconnect_id)
        reconnect_tick_frames = sorted(
            {int(state["sceneFrame"]) for state in reconnect_states if int(state["sceneFrame"]) > reconnect_start}
        )
        require(
            reconnect_tick_frames == list(range(reconnect_start + 1, reconnect_start + 26)),
            f"reconnected step advanced unexpected fixed ticks: {reconnect_tick_frames}",
        )
        require(
            int(reconnect_states[-1]["sceneFrame"]) == reconnect_start + 25,
            "reconnected step did not preserve its exact remaining tick count",
        )
        reconnected.close()

        return {
            "ok": True,
            "initialPausedFrame": initial["sceneFrame"],
            "resumeApplied": True,
            "pauseApplied": True,
            "stepTicks": len(tick_states),
            "stepRenderFrames": len(render_states),
            "immediateConditionFrames": len(immediate_states),
            "frameTimeoutFrames": len(frame_timeout_states),
            "tickTimeoutTicks": len(tick_timeout_states),
            "reconnectStepTicks": len(reconnect_tick_frames),
        }
    finally:
        if reconnected is not None:
            reconnected.close()
        if connection is not None:
            connection.close()
        try:
            require_applied(session, "session.stop")
        except (OSError, RuntimeError):
            pass


def validate_manual_pointer_ownership(session: Path, executable: Path, scene: Path) -> dict[str, object]:
    launch_session(session, executable, scene, manual=True)
    try:
        connection = SkarnessConnection(session)
        try:
            capabilities = connection.wait(connection.send("capabilities.get"))
        finally:
            connection.close()
        commands = capabilities.get("commands", [])
        if "input.pointer_drag" in commands:
            raise RuntimeError("manual Skarness advertised synthetic pointer ownership")

        attempted = subprocess.run(
            [
                sys.executable,
                str(REPO / "tools" / "skarness.py"),
                "command",
                str(session),
                "input.pointer_drag",
                "button=right",
                "x=800",
                "y=450",
                "deltaX=-65",
                "deltaY=20",
            ],
            cwd=REPO,
            check=False,
            capture_output=True,
            text=True,
            timeout=5.0,
        )
        response = json.loads(attempted.stdout)
        if attempted.returncode != 1 or response.get("status") != "rejected":
            raise RuntimeError(f"manual pointer command was not rejected: {attempted.stdout or attempted.stderr}")
        return {"pointerAdvertised": False, "pointerCommandStatus": response["status"]}
    finally:
        try:
            require_applied(session, "session.stop")
        except (OSError, RuntimeError):
            pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=REPO / "Automation" / "SKULLBONEZ_CORE.exe")
    parser.add_argument(
        "--scene",
        type=Path,
        default=REPO / "SkullbonezData" / "scenes" / "replay_prediction_simple.scene.json",
    )
    parser.add_argument(
        "--session",
        type=Path,
        default=REPO / "TestOutput" / "skarness" / f"run-control-{uuid.uuid4().hex[:8]}",
    )
    args = parser.parse_args()
    session = args.session.resolve()
    result = validate(session, args.exe.resolve(), args.scene.resolve())
    result["manualInput"] = validate_manual_pointer_ownership(
        session.with_name(f"{session.name}-manual"), args.exe.resolve(), args.scene.resolve()
    )
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
