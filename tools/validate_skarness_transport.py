#!/usr/bin/env python3
"""Exercise Skarness session, transport, and request-history guarantees."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import time
import uuid
import xml.etree.ElementTree as ET

from skarness import SCHEMA_VERSION, SkarnessConnection, load_manifest


REPO = Path(__file__).resolve().parents[1]
DEFAULT_SCENE = REPO / "SkullbonezData" / "scenes" / "interaction_replay_prediction_harness.scene.json"
REQUIRED_MANIFEST_KEYS = {
    "schemaVersion",
    "processId",
    "pipe",
    "sessionToken",
    "stateTrace",
    "physicsTrace",
    "manualInput",
    "status",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def write_envelope(
    connection: SkarnessConnection,
    command: str,
    request_id: str,
    *,
    token: str | None = None,
    schema_version: int = SCHEMA_VERSION,
    arguments: dict[str, object] | None = None,
) -> None:
    envelope = {
        "schemaVersion": schema_version,
        "sessionToken": connection.token if token is None else token,
        "requestId": request_id,
        "command": command,
        "arguments": arguments or {},
    }
    connection.pipe.write((json.dumps(envelope, separators=(",", ":")) + "\n").encode("utf-8"))


def read_request_event(
    connection: SkarnessConnection,
    request_id: str,
    statuses: set[str],
) -> dict[str, object]:
    while True:
        event = connection.read_event()
        if event.get("requestId") == request_id and event.get("status") in statuses:
            return event


def validate_build_boundaries() -> dict[str, list[str]]:
    project = ET.parse(REPO / "SKULLBONEZ_CORE.vcxproj")
    root = project.getroot()
    namespace = {"msb": "http://schemas.microsoft.com/developer/msbuild/2003"}
    with_skarness: list[str] = []
    without_skarness: list[str] = []

    for group in root.findall("msb:ItemDefinitionGroup", namespace):
        condition = group.attrib.get("Condition", "")
        definitions = group.findtext("msb:ClCompile/msb:PreprocessorDefinitions", "", namespace)
        if not condition or not definitions:
            continue
        if "SKULLBONEZ_SKARNESS" in definitions.split(";"):
            with_skarness.append(condition)
        else:
            without_skarness.append(condition)

    joined_with = " ".join(with_skarness)
    joined_without = " ".join(without_skarness)
    require("Debug|x64" in joined_with, "Debug|x64 does not compile Skarness")
    require("Automation|x64" in joined_with, "Automation|x64 does not compile Skarness")
    require("Profile|x64" in joined_without, "Profile|x64 build boundary was not found")
    require("Release|x64" in joined_without, "Release|x64 build boundary was not found")
    require("Profile|x64" not in joined_with, "Profile|x64 unexpectedly compiles Skarness")
    require("Release|x64" not in joined_with, "Release|x64 unexpectedly compiles Skarness")
    return {"withSkarness": with_skarness, "withoutSkarness": without_skarness}


def validate_profile_rejection(profile_executable: Path, output_root: Path) -> dict[str, object]:
    require(profile_executable.is_file(), f"Profile executable is missing: {profile_executable}")
    rejected_session = output_root / f"profile-rejected-{uuid.uuid4().hex[:8]}"
    result = subprocess.run(
        [
            str(profile_executable),
            "--skarness",
            str(rejected_session),
            "--automation-hidden-window",
            "--frames",
            "1",
        ],
        cwd=REPO,
        capture_output=True,
        timeout=20,
        check=False,
    )
    output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
    require(result.returncode != 0, "Profile accepted --skarness instead of rejecting it")
    require(
        "--skarness is available only in Debug and Automation builds." in output,
        "Profile rejection omitted the Skarness build-boundary diagnostic",
    )
    require(not (rejected_session / "session.json").exists(), "Profile published a dormant Skarness manifest")
    return {"exitCode": result.returncode, "manifestPublished": False}


def launch_host(executable: Path, scene: Path, session: Path) -> subprocess.Popen[bytes]:
    session.mkdir(parents=True, exist_ok=False)
    stdout = (session / "process.stdout.log").open("wb")
    stderr = (session / "process.stderr.log").open("wb")
    try:
        return subprocess.Popen(
            [
                str(executable),
                "--skarness",
                str(session),
                "--renderer",
                "dx12",
                "--vsync",
                "off",
                "--shadows",
                "off",
                "--hide-top-text",
                "--automation-hidden-window",
                "--scene",
                str(scene),
            ],
            cwd=REPO,
            stdin=subprocess.DEVNULL,
            stdout=stdout,
            stderr=stderr,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        )
    finally:
        stdout.close()
        stderr.close()


def validate_manifest(session: Path, process: subprocess.Popen[bytes]) -> dict[str, object]:
    manifest = load_manifest(session)
    missing = REQUIRED_MANIFEST_KEYS - manifest.keys()
    require(not missing, f"session manifest omitted keys: {sorted(missing)}")
    require(manifest["schemaVersion"] == SCHEMA_VERSION, "session manifest schema mismatch")
    require(manifest["processId"] == process.pid, "session manifest process identity mismatch")
    require(str(manifest["pipe"]).startswith(r"\\.\pipe\skarness-"), "session pipe name is invalid")
    require(len(str(manifest["sessionToken"])) == 32, "session token is not a 128-bit hexadecimal value")
    require(manifest["manualInput"] is False, "automated session unexpectedly retained manual input")
    require(not (session / "session.json.partial").exists(), "atomic manifest partial remained visible")

    for key in ("stateTrace", "physicsTrace"):
        path = Path(str(manifest[key])).resolve()
        require(path.is_relative_to(session.resolve()), f"{key} escaped the session directory")

    return manifest


def validate_request_lifecycle(session: Path) -> dict[str, object]:
    stable_id = "transport-capability-stable"
    connection = SkarnessConnection(session)
    try:
        capability = connection.wait(connection.send("capabilities.get", request_id=stable_id))
        require(capability.get("status") == "applied", "capabilities request did not apply")
        require("state.subscribe" in capability.get("commands", []), "capability catalog is incomplete")

        write_envelope(connection, "capabilities.get", stable_id, token="0" * 32)
        unauthorized = read_request_event(connection, stable_id, {"rejected"})
        require("token" in str(unauthorized.get("reason", "")), "unauthorized request had the wrong rejection")

        replayed = connection.wait(connection.send("capabilities.get", request_id=stable_id))
        require(replayed == capability, "unauthorized request overwrote a completed duplicate result")
    finally:
        connection.close()

    reconnect = SkarnessConnection(session)
    try:
        replayed_after_reconnect = reconnect.wait(reconnect.send("capabilities.get", request_id=stable_id))
        require(replayed_after_reconnect == capability, "reconnect lost the duplicate-safe completed result")

        write_envelope(reconnect, "capabilities.get", "transport-schema-mismatch", schema_version=999)
        schema_rejection = read_request_event(reconnect, "transport-schema-mismatch", {"rejected"})
        require("schema" in str(schema_rejection.get("reason", "")), "schema mismatch had the wrong rejection")

        reconnect.pipe.write(b"{not-json}\n")
        malformed = read_request_event(reconnect, "", {"rejected"})
        require(malformed.get("reason") == "invalid JSON request", "malformed JSON had the wrong rejection")

        pending_id = "transport-pending-step"
        write_envelope(reconnect, "run.step", pending_id, arguments={"count": 100000})
        accepted = read_request_event(reconnect, pending_id, {"accepted"})
        require(accepted.get("status") == "accepted", "bounded step was not accepted")

        # Fill and roll the bounded completed history while the oldest id remains
        # unresolved. A duplicate must still resolve as pending, never reapply.
        for index in range(260):
            request_id = f"transport-history-{index:03d}"
            result = reconnect.wait(reconnect.send("capabilities.get", request_id=request_id))
            require(result.get("status") == "applied", f"history request {index} did not apply")

        write_envelope(reconnect, "run.step", pending_id, arguments={"count": 1})
        duplicate = read_request_event(reconnect, pending_id, {"duplicate", "accepted"})
        require(duplicate.get("status") == "duplicate", "history pressure forgot an in-flight request id")

        paused = reconnect.wait(reconnect.send("run.pause", request_id="transport-cancel-step"))
        require(paused.get("status") == "applied", "run.pause did not cancel the bounded step")
        cached_cancel = reconnect.wait(reconnect.send("run.step", {"count": 1}, request_id=pending_id))
        require(cached_cancel.get("status") == "rejected", "cancelled request did not retain its terminal result")

        queue_ids = [f"transport-queue-{index:03d}" for index in range(130)]
        batch = bytearray()
        for request_id in queue_ids:
            envelope = {
                "schemaVersion": SCHEMA_VERSION,
                "sessionToken": reconnect.token,
                "requestId": request_id,
                "command": "replay.jump_to_start",
                "arguments": {},
            }
            batch.extend((json.dumps(envelope, separators=(",", ":")) + "\n").encode("utf-8"))
        reconnect.pipe.write(batch)

        first_status: dict[str, dict[str, object]] = {}
        while len(first_status) < len(queue_ids):
            event = reconnect.read_event()
            request_id = str(event.get("requestId", ""))
            if request_id in queue_ids and request_id not in first_status:
                first_status[request_id] = event

        full = [
            event
            for event in first_status.values()
            if event.get("status") == "rejected" and event.get("reason") == "command queue is full"
        ]
        require(len(full) == 2, f"command queue boundary was not exact: full_rejections={len(full)}")

        reconnect.wait(reconnect.send("run.pause", request_id="transport-post-queue-pause"))
        return {
            "completedDuplicateStable": True,
            "unauthorizedCollisionSafe": True,
            "reconnectDuplicateStable": True,
            "inFlightHistoryStable": True,
            "historyRequests": 260,
            "commandQueueAccepted": 128,
            "commandQueueRejected": len(full),
        }
    finally:
        reconnect.close()


def validate_trace(session: Path) -> dict[str, object]:
    trace = session / "runtime.skarness.ndjson"
    require(trace.is_file(), "runtime trace was not published")
    rows: list[dict[str, object]] = []
    for raw_line in trace.read_bytes().splitlines(keepends=True):
        require(raw_line.endswith(b"\n"), "runtime trace ended with an incomplete row")
        rows.append(json.loads(raw_line))
    sequences = [int(row["sequence"]) for row in rows]
    require(sequences == sorted(sequences), "runtime trace sequence regressed")
    require(len(sequences) == len(set(sequences)), "runtime trace sequence was duplicated")
    require(
        any(row.get("requestId") == "transport-pending-step" and row.get("status") == "rejected" for row in rows),
        "cancelled pending request was not durable",
    )
    return {"rows": len(rows), "firstSequence": sequences[0], "lastSequence": sequences[-1]}


def stop_host(session: Path, process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    try:
        connection = SkarnessConnection(session)
        try:
            result = connection.wait(connection.send("session.stop"))
            require(result.get("status") == "applied", "session.stop did not apply")
        finally:
            connection.close()
        process.wait(timeout=15)
    except (OSError, RuntimeError, subprocess.TimeoutExpired):
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, default=REPO / "Automation" / "SKULLBONEZ_CORE.exe")
    parser.add_argument("--profile-executable", type=Path, default=REPO / "Profile" / "SKULLBONEZ_CORE.exe")
    parser.add_argument("--scene", type=Path, default=DEFAULT_SCENE)
    parser.add_argument(
        "--output-root",
        type=Path,
        default=REPO / "TestOutput" / "skarness" / "transport",
    )
    args = parser.parse_args()
    executable = args.executable.resolve()
    profile_executable = args.profile_executable.resolve()
    scene = args.scene.resolve()
    output_root = args.output_root.resolve()
    require(executable.is_file(), f"Automation executable is missing: {executable}")
    require(scene.is_file(), f"scene is missing: {scene}")
    output_root.mkdir(parents=True, exist_ok=True)
    session = output_root / f"session-{uuid.uuid4().hex[:8]}"

    process = launch_host(executable, scene, session)
    summary: dict[str, object] = {}
    try:
        summary["buildBoundaries"] = validate_build_boundaries()
        summary["profileRejection"] = validate_profile_rejection(profile_executable, output_root)
        summary["manifest"] = validate_manifest(session, process)
        summary["requests"] = validate_request_lifecycle(session)
        summary["trace"] = validate_trace(session)
    finally:
        stop_host(session, process)

    summary["session"] = str(session)
    summary["processExitCode"] = process.returncode
    print(json.dumps(summary, indent=2))
    print("PASS: Skarness session host, build boundaries, reconnect, deduplication, and bounded queues")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
