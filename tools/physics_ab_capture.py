#!/usr/bin/env python3
"""Capture two independent Skarness builds into an immutable comparison bundle."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import os
import hashlib
import json
import math
import struct
from pathlib import Path
import shutil
import sys

from replay_query import ReplayV2
from skarness import SkarnessConnection, launch

REPO = Path(__file__).resolve().parents[1]
TICK_RATE = 120
MEMORY_BYTES = 512 * 1024 * 1024
REQUIRED = {"run.pause", "run.step", "scene.reset", "replay.save",
            "replay.set_recording_enabled", "replay.set_retention_seconds",
            "replay.set_memory_budget_mib", "replay.set_prediction_enabled"}
# Only semantic actions with Physics-tick meaning can enter a paired capture.
# Transport, scene changes and pixel-coordinate gestures would break alignment.
SUPPORTED_ACTIONS = {"scene.object.select", "scene.object.clear_selection"}


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def load_actions(path: Path | None, ticks: int) -> list[dict]:
    if path is None:
        return []
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, list):
        raise ValueError("Actions must be a JSON array of {tick, command, arguments} records")
    previous = -1
    for action in value:
        if not isinstance(action, dict):
            raise ValueError("Every action must be an object")
        tick, name = action.get("tick"), action.get("command", "")
        if isinstance(tick, bool) or not isinstance(tick, int) or not 0 <= tick < ticks or tick < previous:
            raise ValueError("Action ticks must be ordered integers within the capture interval")
        if not isinstance(name, str) or name not in SUPPORTED_ACTIONS:
            raise ValueError(f"Action has no supported paired-capture meaning: {name}")
        if not isinstance(action.get("arguments", {}), dict):
            raise ValueError("Action arguments must be an object")
        previous = tick
    return value


def validate_recording(path: Path, ticks: int) -> dict:
    if path.stat().st_size > MEMORY_BYTES // 2:
        raise ValueError("Recording exceeds the per-side comparison memory budget")
    replay = ReplayV2(path)
    if replay.version not in (3, 4, 5):
        raise ValueError("Comparison requires a replay with recorded motion and sleep fields (v3 or later)")
    headers = replay.presentation_frame_headers()
    if len(headers) != ticks:
        raise ValueError(f"Incomplete capture: expected {ticks} post-step samples, recorded {len(headers)}")
    first = replay.read_frame(replay.frames[0], body_limit=0)
    for index, frame in enumerate(replay.frames):
        row = replay.read_frame(frame, body_limit=0)
        if row["sceneFrame"] != index:
            raise ValueError(f"Missing or duplicated physics tick {index}: {row['sceneFrame']}")
        if headers[index]["dtBits"] != f"0x{struct.unpack('<I', struct.pack('<f', 1 / TICK_RATE))[0]:08X}":
            raise ValueError(f"Non-fixed physics interval at tick {index}")
        if not row["world"]["fixedStep"]:
            raise ValueError("Producer did not record fixed-step simulation")
    # The native loader checks the actual expanded capacity too. This cold
    # estimate prevents launching the viewer with an obviously oversized pair.
    decoded = sum(frame.body_count * 192 + 256 for frame in replay.frames)
    if decoded > MEMORY_BYTES // 2:
        raise ValueError("Expanded recording exceeds the per-side memory budget")
    return {"path": path.name, "sha256": digest(path), "bytes": path.stat().st_size,
            "expandedEstimateBytes": decoded, "ticks": ticks,
            "bodyCount": first["bodyCount"], "version": replay.version,
            # The legacy recorder labels the first post-step state sceneFrame=0.
            # Preserve its bytes and expose the physical tick conversion explicitly.
            "firstPhysicsTick": 1, "sceneFrameToPhysicsTick": 1,
            "initialState": "not recorded",
            "headers": headers, "diagnosticFrames": [row.frame_index + 1 for row in replay.solver_checkpoints]}


def capture_inputs(scene: Path, bundle: Path) -> tuple[Path, dict]:
    data = json.loads(scene.read_text(encoding="utf-8"))
    if data.get("format") != "skullbonez.scene.json":
        raise ValueError("Select an authored Skullbonez scene")
    seed = data.get("simulation", {}).get("seed")
    if isinstance(seed, bool) or not isinstance(seed, int):
        raise ValueError("The scene must declare an explicit simulation seed")
    inputs = bundle / "inputs"
    inputs.mkdir()
    archived = inputs / "scene.scene.json"
    shutil.copy2(scene, archived)
    # Scene parsers resolve asset names against SkullbonezData, not the scene
    # file directory. Preserve the data tree without generated cache/output.
    assets = REPO / "SkullbonezData"
    shutil.copytree(assets, inputs / "SkullbonezData",
                    ignore=shutil.ignore_patterns("*.skreplay", "*.log", "__pycache__"))
    hashes = {p.relative_to(inputs).as_posix(): digest(p) for p in inputs.rglob("*") if p.is_file()}
    return archived, {"scene": "inputs/scene.scene.json", "seed": seed, "files": hashes}


def finish_producer(manifest: dict, executable: Path) -> None:
    """Wait for the owned producer; terminate only after checking its exact image."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    kernel.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    handle = kernel.OpenProcess(0x100000 | 0x1000 | 1, False, int(manifest["processId"]))
    if not handle:
        if ctypes.get_last_error() == 87:  # Process has already exited.
            return
        raise OSError(ctypes.get_last_error(), "Cannot verify producer shutdown")
    try:
        name = ctypes.create_unicode_buffer(32768)
        length = wintypes.DWORD(len(name))
        if not kernel.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(length)):
            if kernel.WaitForSingleObject(handle, 0) == 0:
                return
            raise OSError(ctypes.get_last_error(), "Cannot verify producer image")
        if os.path.normcase(str(Path(name.value).resolve())) != os.path.normcase(str(executable.resolve())):
            raise RuntimeError("Producer PID now belongs to a different executable; refusing to terminate it")
        if kernel.WaitForSingleObject(handle, 5000) == 0:
            return
        if not kernel.TerminateProcess(handle, 1) or kernel.WaitForSingleObject(handle, 5000) != 0:
            raise RuntimeError("Owned producer did not terminate")
        raise RuntimeError("Producer required forced shutdown; capture remains incomplete")
    finally:
        kernel.CloseHandle(handle)


class CaptureSide:
    def __init__(self, executable: Path, directory: Path, scene: Path):
        self.executable = executable
        self.directory = directory
        self.scene = scene
        self.connection: SkarnessConnection | None = None

    def start(self) -> set[str]:
        launch(self.directory, self.executable, self.scene, hidden=True, fixed_step=True)
        self.connection = SkarnessConnection(self.directory)
        request = self.connection.send("capabilities.get")
        while True:
            response = self.connection.read_event()
            if response.get("requestId") == request and response.get("kind") == "capabilities":
                break
        write_json(self.directory / "capabilities.json", response)
        commands = response.get("commands", response.get("payload", {}).get("commands", []))
        return {row["name"] if isinstance(row, dict) else row for row in commands}

    def command(self, name: str, arguments: dict | None = None) -> dict:
        assert self.connection is not None
        result = self.connection.wait(self.connection.send(name, arguments))
        with (self.directory / "commands.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(result, separators=(",", ":")) + "\n")
        if result.get("status") != "applied":
            raise RuntimeError(f"{name}: {result.get('message', result)}")
        return result

    def stop(self) -> None:
        if self.connection is None:
            return
        manifest = self.connection.manifest
        try:
            self.command("session.stop")
        finally:
            self.connection.close()
            self.connection = None
            finish_producer(manifest, self.executable)


def run_side(side: CaptureSide, bundle: Path, label: str, ticks: int, actions: list[dict]) -> dict:
    executable_hash = digest(side.executable)
    try:
        commands = side.start()
        missing = (REQUIRED | {a["command"] for a in actions}) - commands
        if missing:
            raise ValueError(f"Build {label} lacks required Skarness commands: {sorted(missing)}")
        side.command("state.subscribe", {"topics": ["frame.clocks"], "detail": "summary"})
        side.command("run.pause")
        side.command("replay.set_prediction_enabled", {"enabled": False})
        side.command("replay.set_memory_budget_mib", {"mib": 256})
        side.command("replay.set_retention_seconds", {"seconds": max(20, math.ceil(ticks / TICK_RATE) + 1)})
        side.command("replay.set_recording_enabled", {"enabled": True})
        side.command("scene.reset")
        cursor = 0
        for action in actions + [{"tick": ticks, "command": "", "arguments": {}}]:
            while cursor < action["tick"]:
                if (bundle / "cancel.request").exists():
                    raise InterruptedError("Capture cancelled")
                count = min(TICK_RATE, action["tick"] - cursor)
                side.command("run.step", {"count": count})
                cursor += count
                write_json(bundle / "progress.json", {"status": "capturing", "side": label,
                                                       "tick": cursor, "totalTicks": ticks})
            if action["command"]:
                side.command(action["command"], action.get("arguments", {}))
        artifact = bundle / f"{label}.skreplay"
        side.command("replay.save", {"path": str(artifact)})
        result = validate_recording(artifact, ticks)
        write_json(bundle / f"{label}.headers.json", result.pop("headers"))
        result["executable"] = str(side.executable)
        if digest(side.executable) != executable_hash:
            raise ValueError("Executable changed during capture")
        result["executableSha256"] = executable_hash
        side.stop()  # Flush diagnostics before hashing the completed evidence.
        diagnostics = side.directory / "physics.physicsdiag.ndjson"
        if diagnostics.exists():
            result["diagnosticsSha256"] = digest(diagnostics)
        return result
    finally:
        side.stop()


def verify_producer_assets(executable: Path, archived_inputs: Path) -> dict[str, str]:
    # Skarness launches each build from its own repository root. An absolute
    # scene path does not change the parser's relative asset lookup root.
    producer_data = executable.parent.parent / "SkullbonezData"
    expected_data = archived_inputs / "SkullbonezData"
    identities = {}
    for directory in ("assets", "hulls", "styles"):
        expected_files = {p.relative_to(expected_data).as_posix(): digest(p)
                          for p in (expected_data / directory).rglob("*") if p.is_file()}
        actual_files = {p.relative_to(producer_data).as_posix(): digest(p)
                        for p in (producer_data / directory).rglob("*") if p.is_file()}
        if actual_files != expected_files:
            raise ValueError(f"Build {executable} resolves different {directory} inputs in {producer_data}")
        identities.update(actual_files)
    return identities


def capture(build_a: Path, build_b: Path, scene: Path, output: Path,
            seconds: float = 20, actions_path: Path | None = None) -> Path:
    if not math.isfinite(seconds) or seconds <= 0 or seconds > 120:
        raise ValueError("Duration must be greater than zero and at most 120 seconds")
    ticks = round(seconds * TICK_RATE)
    if ticks < 1 or not math.isclose(ticks / TICK_RATE, seconds, abs_tol=1e-7):
        raise ValueError("Duration must be a whole number of physics ticks")
    builds = [build_a.resolve(strict=True), build_b.resolve(strict=True)]
    actions = load_actions(actions_path, ticks)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    write_json(output / "progress.json", {"status": "preparing", "totalTicks": ticks})
    try:
        archived_scene, inputs = capture_inputs(scene.resolve(strict=True), output)
        write_json(output / "actions.json", actions)
        inputs["actionsSha256"] = digest(output / "actions.json")
        # Check both existing executables before advancing either scenario.
        # Each probe is sequential and launches from the producer's own root.
        for label, executable in zip(("A", "B"), builds):
            asset_hashes = verify_producer_assets(executable, output / "inputs")
            probe = CaptureSide(executable, output / f"preflight-{label}", archived_scene)
            try:
                capabilities = probe.start()
                missing = (REQUIRED | {a["command"] for a in actions}) - capabilities
                if missing:
                    raise ValueError(f"Build {label} lacks required commands: {sorted(missing)}")
                write_json(output / f"{label}.build.json", {"executable": str(executable),
                           "sha256": digest(executable), "runtimeRoot": str(executable.parent), "assetFiles": asset_hashes,
                           "dependencies": {p.name: digest(p) for p in executable.parent.glob("*.dll")}})
            finally:
                probe.stop()
        # Both builds resolve the same absolute scene and shared asset files.
        # Detect edits during either producer run instead of accepting a mixed pair.
        shared_hashes = {p: digest(p) for p in (REPO / "SkullbonezData").rglob("*") if p.is_file()}
        sides = []
        for label, executable in zip(("A", "B"), builds):
            verify_producer_assets(executable, output / "inputs")
            sides.append(run_side(CaptureSide(executable, output / label, archived_scene),
                                  output, label, ticks, actions))
            verify_producer_assets(executable, output / "inputs")
            if any(not p.exists() or digest(p) != expected for p, expected in shared_hashes.items()):
                raise ValueError("Scene assets changed during capture")
        if sum(side["expandedEstimateBytes"] for side in sides) > MEMORY_BYTES:
            raise ValueError("Combined recordings exceed the comparison memory budget")
        manifest = {"format": "skullbonez.physics-comparison", "version": 1, "status": "complete",
                    "tickRate": TICK_RATE, "ticks": ticks, "inputs": inputs, "sides": sides}
        write_json(output / "comparison.json", manifest)
        write_json(output / "progress.json", {"status": "complete", "totalTicks": ticks})
        return output / "comparison.json"
    except BaseException as error:
        write_json(output / "progress.json", {"status": "incomplete", "error": str(error)})
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-a", type=Path, required=True)
    parser.add_argument("--build-b", type=Path, required=True)
    parser.add_argument("--scene", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--actions", type=Path)
    args = parser.parse_args()
    try:
        print(capture(args.build_a, args.build_b, args.scene, args.output, args.seconds, args.actions))
        return 0
    except (OSError, ValueError, RuntimeError, InterruptedError) as error:
        print(f"physics-ab: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
