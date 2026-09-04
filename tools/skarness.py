#!/usr/bin/env python3
"""Small local client for the Skarness named-pipe protocol."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import time
import uuid


SCHEMA_VERSION = 1
DEFAULT_ROW_LIMIT = 200


def load_manifest(session: Path) -> dict[str, object]:
    manifest_path = session / "session.json"
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        try:
            return json.loads(manifest_path.read_text(encoding="utf-8"))
        except (FileNotFoundError, PermissionError, json.JSONDecodeError):
            # The host replaces the manifest atomically, but Windows can deny
            # a read briefly while antivirus or indexing observes that edge.
            time.sleep(0.05)
    raise RuntimeError(f"Skarness manifest was not ready: {manifest_path}")


def parse_value(text: str) -> object:
    lowered = text.lower()
    if lowered == "true":
        return True
    if lowered == "false":
        return False
    try:
        return int(text)
    except ValueError:
        pass
    try:
        return float(text)
    except ValueError:
        return text


def parse_arguments(values: list[str]) -> dict[str, object]:
    result: dict[str, object] = {}
    for value in values:
        if "=" not in value:
            raise RuntimeError(f"arguments use name=value syntax: {value}")
        name, raw = value.split("=", 1)
        result[name] = parse_value(raw)
    return result


class SkarnessConnection:
    def __init__(self, session: Path):
        self.manifest = load_manifest(session)
        self.token = str(self.manifest["sessionToken"])
        pipe_path = str(self.manifest["pipe"])
        deadline = time.monotonic() + 10.0
        while True:
            try:
                self.pipe = open(pipe_path, "r+b", buffering=0)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)
        self.buffer = bytearray()

    def close(self) -> None:
        self.pipe.close()

    def send(self, command: str, arguments: dict[str, object] | None = None) -> str:
        request_id = uuid.uuid4().hex
        request = {
            "schemaVersion": SCHEMA_VERSION,
            "sessionToken": self.token,
            "requestId": request_id,
            "command": command,
            "arguments": arguments or {},
        }
        self.pipe.write((json.dumps(request, separators=(",", ":")) + "\n").encode("utf-8"))
        return request_id

    def read_event(self) -> dict[str, object]:
        while b"\n" not in self.buffer:
            chunk = self.pipe.read(4096)
            if not chunk:
                raise RuntimeError("Skarness pipe closed")
            self.buffer.extend(chunk)
        line, _, remainder = self.buffer.partition(b"\n")
        self.buffer = bytearray(remainder)
        return json.loads(line.decode("utf-8"))

    def wait(self, request_id: str) -> dict[str, object]:
        while True:
            event = self.read_event()
            if event.get("requestId") != request_id:
                continue
            status = event.get("status")
            if status in {"applied", "rejected", "duplicate"}:
                return event


def run_command(session: Path, command: str, arguments: dict[str, object]) -> int:
    connection = SkarnessConnection(session)
    try:
        result = connection.wait(connection.send(command, arguments))
        print(json.dumps(result, indent=2))
        return 0 if result.get("status") == "applied" else 1
    finally:
        connection.close()


def run_predict(session: Path, name: str, frames: int) -> int:
    connection = SkarnessConnection(session)
    try:
        for command, arguments in (
            ("prediction.select_target", {"name": name}),
            ("replay.set_prediction_enabled", {"enabled": True}),
            ("run.until", {"condition": "prediction.causal_rendered", "maxFrames": frames}),
        ):
            result = connection.wait(connection.send(command, arguments))
            print(json.dumps(result, separators=(",", ":")))
            if result.get("status") != "applied":
                return 1
        return 0
    finally:
        connection.close()


def verify_future(session: Path, name: str, frames: int) -> int:
    from check_prediction_future_render import validate_raster
    from PIL import Image

    before = (session / "prediction-before.png").resolve()
    after = (session / "prediction-after.png").resolve()
    connection = SkarnessConnection(session)
    try:
        workflow = (
            ("capture.screenshot", {"path": str(before)}),
            ("prediction.select_target", {"name": name}),
            ("replay.set_prediction_enabled", {"enabled": True}),
            ("run.until", {"condition": "prediction.causal_rendered", "maxFrames": frames}),
            ("capture.screenshot", {"path": str(after)}),
        )
        final_result: dict[str, object] | None = None
        for command, arguments in workflow:
            result = connection.wait(connection.send(command, arguments))
            print(json.dumps(result, separators=(",", ":")))
            if result.get("status") != "applied":
                return 1
            final_result = result
        with Image.open(before) as before_image, Image.open(after) as after_image:
            raster = validate_raster(before_image, after_image)
        summary = query_latest_state(session)
        print(json.dumps({
            "ok": True,
            "target": name,
            "raster": {
                "newPathPixels": raster.new_path_pixels,
                "horizontalSpan": raster.horizontal_span,
                "verticalSpan": raster.vertical_span,
            },
            "renderState": summary,
            "command": final_result,
        }, indent=2))
        return 0
    finally:
        connection.close()


def watch(session: Path, topic: str) -> int:
    connection = SkarnessConnection(session)
    try:
        result = connection.wait(connection.send("state.subscribe", {"topics": [topic]}))
        if result.get("status") != "applied":
            print(json.dumps(result, indent=2), file=sys.stderr)
            return 1
        while True:
            event = connection.read_event()
            if event.get("kind") == "state" and (topic == "*" or event.get("topic") == topic):
                print(json.dumps(event, separators=(",", ":")), flush=True)
    except KeyboardInterrupt:
        return 0
    finally:
        connection.close()


def launch(session: Path, executable: Path, scene: Path | None, hidden: bool = False, manual: bool = False) -> int:
    session.mkdir(parents=True, exist_ok=True)
    manifest = session / "session.json"
    if manifest.exists():
        manifest.unlink()
    skarness_option = "--skarness-manual" if manual else "--skarness"
    command = [str(executable.resolve()), skarness_option, str(session.resolve())]
    if scene is not None:
        command.extend(("--scene", str(scene.resolve())))
    if hidden:
        command.append("--automation-hidden-window")
    stdout = open(session / "process.stdout.log", "wb")
    stderr = open(session / "process.stderr.log", "wb")
    try:
        process = subprocess.Popen(
            command,
            cwd=executable.resolve().parent.parent,
            stdin=subprocess.DEVNULL,
            stdout=stdout,
            stderr=stderr,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        )
    finally:
        stdout.close()
        stderr.close()
    published = load_manifest(session)
    print(json.dumps({"processId": process.pid, "manifest": published}, indent=2))
    return 0


def artifact_path(session: Path, manifest_key: str, fallback: str) -> Path:
    manifest = load_manifest(session)
    value = manifest.get(manifest_key)
    return Path(str(value)) if value else session / fallback


def query_latest_state(session: Path) -> dict[str, object] | None:
    trace = artifact_path(session, "stateTrace", "runtime.skarness.ndjson")
    latest: dict[str, object] | None = None
    with trace.open("rb") as source:
        for raw_line in source:
            if not raw_line.endswith(b"\n"):
                break
            try:
                row = json.loads(raw_line)
            except json.JSONDecodeError:
                continue
            if row.get("kind") == "state" and row.get("topic") == "replay.state":
                latest = row
    return latest


def source_identity(path: Path) -> str:
    with path.open("rb") as source:
        first_line = source.readline()
    return hashlib.sha256(first_line if first_line.endswith(b"\n") else b"").hexdigest()


def open_query_cache(session: Path) -> sqlite3.Connection:
    cache = session / "session.skarness.sqlite"
    connection = sqlite3.connect(cache)
    connection.executescript(
        """
        PRAGMA journal_mode=WAL;
        CREATE TABLE IF NOT EXISTS sources (
            source TEXT PRIMARY KEY,
            path TEXT NOT NULL,
            identity TEXT NOT NULL,
            imported_offset INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS rows (
            id INTEGER PRIMARY KEY,
            source TEXT NOT NULL,
            sequence INTEGER,
            topic TEXT,
            kind TEXT,
            runtime_turn INTEGER,
            scene_generation INTEGER,
            simulation_tick INTEGER,
            render_frame INTEGER,
            replay_frame INTEGER,
            row_json TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS rows_topic_sequence ON rows(topic, sequence);
        CREATE INDEX IF NOT EXISTS rows_scene_tick ON rows(scene_generation, simulation_tick);
        """
    )
    return connection


def import_trace(connection: sqlite3.Connection, source_name: str, path: Path) -> int:
    if not path.exists():
        return 0

    identity = source_identity(path)
    stored = connection.execute(
        "SELECT path, identity, imported_offset FROM sources WHERE source = ?", (source_name,)
    ).fetchone()
    offset = int(stored[2]) if stored else 0
    if stored and (stored[0] != str(path) or stored[1] != identity or path.stat().st_size < offset):
        connection.execute("DELETE FROM rows WHERE source = ?", (source_name,))
        offset = 0

    imported = 0
    with path.open("rb") as source:
        source.seek(offset)
        while True:
            start = source.tell()
            raw_line = source.readline()
            if not raw_line or not raw_line.endswith(b"\n"):
                source.seek(start)
                break
            offset = source.tell()
            try:
                row = json.loads(raw_line)
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            connection.execute(
                """
                INSERT INTO rows(source, sequence, topic, kind, runtime_turn, scene_generation,
                                 simulation_tick, render_frame, replay_frame, row_json)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    source_name,
                    row.get("sequence"),
                    row.get("topic"),
                    row.get("kind"),
                    row.get("runtimeTurn"),
                    row.get("sceneGeneration"),
                    row.get("simulationTick"),
                    row.get("renderFrame"),
                    row.get("replayFrame"),
                    json.dumps(row, separators=(",", ":")),
                ),
            )
            imported += 1

    connection.execute(
        """
        INSERT INTO sources(source, path, identity, imported_offset) VALUES (?, ?, ?, ?)
        ON CONFLICT(source) DO UPDATE SET path=excluded.path, identity=excluded.identity,
                                                imported_offset=excluded.imported_offset
        """,
        (source_name, str(path), identity, offset),
    )
    connection.commit()
    return imported


def query_rows(session: Path, kind: str, limit: int) -> int:
    runtime_trace = artifact_path(session, "stateTrace", "runtime.skarness.ndjson")
    physics_trace = artifact_path(session, "physicsTrace", "physics.physicsdiag.ndjson")
    connection = open_query_cache(session)
    try:
        imported = {
            "runtime": import_trace(connection, "runtime", runtime_trace),
            "physics": import_trace(connection, "physics", physics_trace),
        }
        predicates = {
            "summary": "source = 'runtime' AND topic = 'replay.state'",
            "scene": "source = 'runtime' AND topic = 'scene.state'",
            "replay": "source = 'runtime' AND (topic LIKE 'replay.%' OR kind = 'command')",
            "prediction": "source = 'runtime' AND topic = 'replay.state'",
            "cause": "source = 'runtime' AND topic LIKE '%cause%'",
            "render-submission": "source = 'runtime' AND topic = 'replay.state'",
            "physics": "source = 'physics'",
        }
        rows = connection.execute(
            f"SELECT row_json FROM rows WHERE {predicates[kind]} ORDER BY id DESC LIMIT ?", (limit,)
        ).fetchall()
        decoded = [json.loads(row[0]) for row in reversed(rows)]
        if kind == "summary":
            decoded = decoded[-1:]
        cache = session / "session.skarness.sqlite"
        raw_bytes = sum(path.stat().st_size for path in (runtime_trace, physics_trace) if path.exists())
        result = {
            "kind": kind,
            "importedRows": imported,
            "returnedRows": len(decoded),
            "rawTraceBytes": raw_bytes,
            "sqliteBytes": cache.stat().st_size,
            "rows": decoded,
        }
        encoded = json.dumps(result, indent=2)
        result["modelReadBytes"] = len(encoded.encode("utf-8"))
        print(json.dumps(result, indent=2))
        return 0 if decoded else 1
    finally:
        connection.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="action", required=True)

    capabilities = subparsers.add_parser("capabilities")
    capabilities.add_argument("session", type=Path)

    launch_parser = subparsers.add_parser("launch")
    launch_parser.add_argument("--session", required=True, type=Path)
    launch_parser.add_argument("--exe", type=Path, default=Path("Automation/SKULLBONEZ_CORE.exe"))
    launch_parser.add_argument("--scene", type=Path)
    launch_parser.add_argument("--hidden", action="store_true")
    launch_parser.add_argument("--manual", action="store_true",
                               help="trace a player-controlled run without replacing native input or frame pacing")

    command = subparsers.add_parser("command")
    command.add_argument("session", type=Path)
    command.add_argument("command")
    command.add_argument("arguments", nargs="*", metavar="name=value")

    load_scene = subparsers.add_parser("load-scene")
    load_scene.add_argument("session", type=Path)
    load_scene.add_argument("scene")

    for name in ("reset-scene", "load-demo"):
        action = subparsers.add_parser(name)
        action.add_argument("session", type=Path)

    for name in ("pause", "resume"):
        action = subparsers.add_parser(name)
        action.add_argument("session", type=Path)

    for name in ("step", "step-frames"):
        action = subparsers.add_parser(name)
        action.add_argument("session", type=Path)
        action.add_argument("count", nargs="?", type=int, default=1)

    predict = subparsers.add_parser("predict")
    predict.add_argument("session", type=Path)
    predict.add_argument("name")
    predict.add_argument("--frames", type=int, default=120)

    verify = subparsers.add_parser("verify-future")
    verify.add_argument("session", type=Path)
    verify.add_argument("name")
    verify.add_argument("--frames", type=int, default=1200)

    wait = subparsers.add_parser("wait")
    wait.add_argument("session", type=Path)
    wait.add_argument("condition", choices=("prediction.complete", "prediction.geometry", "prediction.submitted",
                                             "prediction.rendered", "prediction.causal_rendered"))
    wait.add_argument("--max-frames", type=int, default=1200)
    wait.add_argument("--max-ticks", type=int)

    state = subparsers.add_parser("watch")
    state.add_argument("session", type=Path)
    state.add_argument("--topic", default="replay.state")

    query = subparsers.add_parser("query")
    query.add_argument("session", type=Path)
    query.add_argument(
        "kind", choices=("summary", "scene", "replay", "prediction", "cause", "render-submission", "physics")
    )
    query.add_argument("--limit", type=int, default=DEFAULT_ROW_LIMIT)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if args.action == "capabilities":
            return run_command(args.session, "capabilities.get", {})
        if args.action == "launch":
            return launch(args.session, args.exe, args.scene, args.hidden, args.manual)
        if args.action == "command":
            return run_command(args.session, args.command, parse_arguments(args.arguments))
        if args.action == "load-scene":
            return run_command(args.session, "scene.load", {"name": args.scene})
        if args.action == "reset-scene":
            return run_command(args.session, "scene.reset", {})
        if args.action == "load-demo":
            return run_command(args.session, "scene.load_demo", {})
        if args.action == "pause":
            return run_command(args.session, "run.pause", {})
        if args.action == "resume":
            return run_command(args.session, "run.resume", {})
        if args.action == "step":
            return run_command(args.session, "run.step", {"count": args.count})
        if args.action == "step-frames":
            return run_command(args.session, "run.step_frames", {"count": args.count})
        if args.action == "predict":
            return run_predict(args.session, args.name, args.frames)
        if args.action == "verify-future":
            return verify_future(args.session, args.name, args.frames)
        if args.action == "wait":
            limit = {"maxTicks": args.max_ticks} if args.max_ticks is not None else {"maxFrames": args.max_frames}
            return run_command(args.session, "run.until", {"condition": args.condition, **limit})
        if args.action == "watch":
            return watch(args.session, args.topic)
        if args.action == "query":
            if args.limit < 1 or args.limit > 10000:
                raise RuntimeError("query --limit must be in 1..10000")
            return query_rows(args.session, args.kind, args.limit)
        return 2
    except (OSError, RuntimeError, KeyError) as error:
        print(f"skarness: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
