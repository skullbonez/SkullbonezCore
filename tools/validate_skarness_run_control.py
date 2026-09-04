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


def require_applied(session: Path, command: str, arguments: dict[str, object] | None = None) -> None:
    connection = SkarnessConnection(session)
    try:
        result = connection.wait(connection.send(command, arguments))
    finally:
        connection.close()
    if result.get("status") != "applied":
        raise RuntimeError(f"{command} failed: {result}")


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


def read_scene_state(session: Path, after_frame: int | None = None) -> dict[str, object]:
    connection = SkarnessConnection(session)
    try:
        subscribed = connection.wait(connection.send("state.subscribe", {"topics": ["scene.state"]}))
        if subscribed.get("status") != "applied":
            raise RuntimeError(f"state.subscribe failed: {subscribed}")

        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            event = connection.read_event()
            if event.get("kind") != "state" or event.get("topic") != "scene.state":
                continue
            if after_frame is None or int(event.get("sceneFrame", -1)) > after_frame:
                return event
    finally:
        connection.close()
    raise RuntimeError("scene.state did not advance before timeout")


def validate(session: Path, executable: Path, scene: Path) -> dict[str, object]:
    launch_session(session, executable, scene)

    try:
        initial = read_scene_state(session)
        if initial.get("paused") is not True:
            raise RuntimeError(f"new session was not paused: {initial}")

        require_applied(session, "run.resume")
        running = read_scene_state(session)
        if running.get("paused") is not False:
            raise RuntimeError(f"run.resume did not survive client disconnect: {running}")
        advanced = read_scene_state(session, int(running["sceneFrame"]))
        if advanced.get("paused") is not False:
            raise RuntimeError(f"resumed session paused after reconnect: {advanced}")

        require_applied(session, "run.pause")
        paused = read_scene_state(session)
        if paused.get("paused") is not True:
            raise RuntimeError(f"run.pause did not survive client disconnect: {paused}")

        paused_frame = int(paused["sceneFrame"])
        require_applied(session, "run.step", {"count": 3})
        stepped = read_scene_state(session)
        if stepped.get("paused") is not True or int(stepped["sceneFrame"]) < paused_frame + 3:
            raise RuntimeError(f"run.step did not advance three paused ticks: before={paused_frame} after={stepped}")

        return {
            "ok": True,
            "initialPaused": initial["paused"],
            "resumedPaused": running["paused"],
            "resumeFrameRange": [running["sceneFrame"], advanced["sceneFrame"]],
            "pausedAgain": paused["paused"],
            "stepFrameRange": [paused_frame, stepped["sceneFrame"]],
        }
    finally:
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
