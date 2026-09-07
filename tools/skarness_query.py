#!/usr/bin/env python3
"""Incrementally import and query Skarness runtime and Physics traces."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sqlite3
from typing import Iterable


CACHE_SCHEMA_VERSION = 1
TRACE_SCHEMA_VERSION = 1
DEFAULT_ROW_LIMIT = 200
SOURCE_COLUMNS = {
    "source",
    "path",
    "identity",
    "imported_offset",
    "source_size",
    "schema_version",
}


def source_identity(path: Path) -> str:
    with path.open("rb") as source:
        first_line = source.readline()
    return hashlib.sha256(first_line if first_line.endswith(b"\n") else b"").hexdigest()


def open_query_cache(session: Path) -> sqlite3.Connection:
    cache = session / "session.skarness.sqlite"
    connection = sqlite3.connect(cache)
    existing = connection.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='sources'"
    ).fetchone()
    if existing:
        columns = {row[1] for row in connection.execute("PRAGMA table_info(sources)")}
        if columns != SOURCE_COLUMNS:
            connection.executescript("DROP TABLE IF EXISTS rows; DROP TABLE IF EXISTS sources; DROP TABLE IF EXISTS metadata;")
    connection.executescript(
        """
        PRAGMA journal_mode=WAL;
        CREATE TABLE IF NOT EXISTS metadata (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS sources (
            source TEXT PRIMARY KEY,
            path TEXT NOT NULL,
            identity TEXT NOT NULL,
            imported_offset INTEGER NOT NULL,
            source_size INTEGER NOT NULL,
            schema_version INTEGER NOT NULL
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
        CREATE INDEX IF NOT EXISTS rows_source_id ON rows(source, id);
        CREATE INDEX IF NOT EXISTS rows_topic_sequence ON rows(topic, sequence);
        CREATE INDEX IF NOT EXISTS rows_scene_tick ON rows(scene_generation, simulation_tick);
        """
    )
    cached_version = connection.execute(
        "SELECT value FROM metadata WHERE key = 'cacheSchemaVersion'"
    ).fetchone()
    if cached_version and int(cached_version[0]) != CACHE_SCHEMA_VERSION:
        connection.executescript("DELETE FROM rows; DELETE FROM sources; DELETE FROM metadata;")
    connection.execute(
        "INSERT OR REPLACE INTO metadata(key, value) VALUES ('cacheSchemaVersion', ?)",
        (str(CACHE_SCHEMA_VERSION),),
    )
    connection.commit()
    return connection


def _last_physics_correlation(connection: sqlite3.Connection) -> tuple[int | None, int | None, int | None]:
    row = connection.execute(
        """
        SELECT runtime_turn, scene_generation, simulation_tick
        FROM rows
        WHERE source = 'physics' AND runtime_turn IS NOT NULL
        ORDER BY id DESC LIMIT 1
        """
    ).fetchone()
    return tuple(row) if row else (None, None, None)


def import_trace(connection: sqlite3.Connection, source_name: str, path: Path) -> dict[str, int | bool]:
    if not path.exists():
        return {"exists": False, "importedRows": 0, "importedBytes": 0, "sourceBytes": 0}

    identity = source_identity(path)
    source_size = path.stat().st_size
    stored = connection.execute(
        "SELECT path, identity, imported_offset, schema_version FROM sources WHERE source = ?",
        (source_name,),
    ).fetchone()
    offset = int(stored[2]) if stored else 0
    replaced = bool(
        stored
        and (
            stored[0] != str(path)
            or stored[1] != identity
            or int(stored[3]) != TRACE_SCHEMA_VERSION
            or source_size < offset
        )
    )
    if replaced:
        connection.execute("DELETE FROM rows WHERE source = ?", (source_name,))
        offset = 0

    initial_offset = offset
    imported = 0
    correlation = _last_physics_correlation(connection) if source_name == "physics" and offset else (None, None, None)
    with path.open("rb") as source:
        source.seek(offset)
        while True:
            start = source.tell()
            raw_line = source.readline()
            if not raw_line or not raw_line.endswith(b"\n"):
                source.seek(start)
                break
            row = json.loads(raw_line)
            schema_version = int(row.get("schemaVersion", TRACE_SCHEMA_VERSION))
            if schema_version != TRACE_SCHEMA_VERSION:
                raise RuntimeError(
                    f"{source_name} trace schema {schema_version} is incompatible with {TRACE_SCHEMA_VERSION}"
                )
            offset = source.tell()
            if source_name == "physics" and row.get("kind") == "correlation":
                correlation = (
                    row.get("runtimeTurn"),
                    row.get("sceneGeneration"),
                    row.get("simulationTick"),
                )
            runtime_turn = row.get("runtimeTurn", correlation[0])
            scene_generation = row.get("sceneGeneration", correlation[1])
            simulation_tick = row.get("simulationTick", correlation[2])
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
                    runtime_turn,
                    scene_generation,
                    simulation_tick,
                    row.get("renderFrame"),
                    row.get("replayFrame"),
                    json.dumps(row, separators=(",", ":")),
                ),
            )
            imported += 1

    source_size = path.stat().st_size
    connection.execute(
        """
        INSERT INTO sources(source, path, identity, imported_offset, source_size, schema_version)
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(source) DO UPDATE SET path=excluded.path, identity=excluded.identity,
            imported_offset=excluded.imported_offset, source_size=excluded.source_size,
            schema_version=excluded.schema_version
        """,
        (source_name, str(path), identity, offset, source_size, TRACE_SCHEMA_VERSION),
    )
    connection.commit()
    return {
        "exists": True,
        "importedRows": imported,
        "importedBytes": offset - initial_offset,
        "sourceBytes": source_size,
        "completeBytes": offset,
        "replaced": replaced,
    }


def import_session(
    connection: sqlite3.Connection,
    runtime_trace: Path,
    physics_trace: Path,
) -> dict[str, dict[str, int | bool]]:
    return {
        "runtime": import_trace(connection, "runtime", runtime_trace),
        "physics": import_trace(connection, "physics", physics_trace),
    }


def parse_frame_range(value: str | None) -> tuple[int | None, int | None]:
    if value is None:
        return None, None
    if ":" not in value:
        frame = int(value)
        return frame, frame
    start, end = value.split(":", 1)
    return (int(start) if start else None, int(end) if end else None)


def _walk_objects(value: object) -> Iterable[dict[str, object]]:
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from _walk_objects(child)
    elif isinstance(value, list):
        for child in value:
            yield from _walk_objects(child)


def resolve_target_id(connection: sqlite3.Connection, target: str | None) -> int | None:
    if target is None:
        return None
    try:
        return int(target)
    except ValueError:
        pass
    rows = connection.execute("SELECT row_json FROM rows WHERE source = 'runtime' ORDER BY id DESC").fetchall()
    matches: set[int] = set()
    for (encoded,) in rows:
        for value in _walk_objects(json.loads(encoded)):
            if value.get("name") != target:
                continue
            object_id = value.get("sceneObjectId", value.get("id"))
            if object_id is not None:
                matches.add(int(object_id))
    if len(matches) != 1:
        raise RuntimeError(f"target {target!r} resolved to {sorted(matches)} in imported evidence")
    return next(iter(matches))


def _compact(value: object) -> object:
    if isinstance(value, list):
        if len(value) > 24:
            return {"count": len(value), "omitted": True}
        return [_compact(item) for item in value]
    if isinstance(value, dict):
        return {key: _compact(child) for key, child in value.items() if key != "values"}
    return value


def _row_matches_target(row: dict[str, object], target_id: int) -> bool:
    identity_keys = {
        "targetId",
        "pathTargetId",
        "sourceTargetId",
        "publishedPredictionTargetId",
        "submittedPredictionTargetId",
    }
    explicit = {
        int(value[key])
        for value in _walk_objects(row.get("payload", {}))
        for key in identity_keys
        if key in value and isinstance(value[key], (int, float))
    }
    return not explicit or target_id in explicit


def _profile_predicate(kind: str) -> str:
    predicates = {
        "summary": "source = 'runtime' AND topic IN ('session.state','selection.state','replay.prediction.controls','replay.render_submission')",
        "scene": "source = 'runtime' AND topic IN ('scene.objects','selection.state','camera.state')",
        "replay": "source = 'runtime' AND (topic LIKE 'replay.%' OR kind = 'command')",
        "prediction": "source = 'runtime' AND (topic LIKE 'replay.prediction.%' OR topic = 'replay.visual_packet')",
        "cause": "source = 'runtime' AND topic = 'replay.cause'",
        "render-submission": "source = 'runtime' AND topic = 'replay.render_submission'",
        "physics": "source = 'physics'",
    }
    return predicates[kind]


def query_profile(
    connection: sqlite3.Connection,
    kind: str,
    limit: int,
    frames: str | None = None,
    target: str | None = None,
    full: bool = False,
) -> tuple[list[dict[str, object]], int | None]:
    start, end = parse_frame_range(frames)
    target_id = resolve_target_id(connection, target)
    clauses = [f"({_profile_predicate(kind)})"]
    parameters: list[object] = []
    if start is not None:
        clauses.append("COALESCE(replay_frame, simulation_tick, 0) >= ?")
        parameters.append(start)
    if end is not None:
        clauses.append("COALESCE(replay_frame, simulation_tick, 0) <= ?")
        parameters.append(end)
    parameters.append(limit)
    rows = connection.execute(
        f"SELECT row_json, runtime_turn, scene_generation, simulation_tick FROM rows "
        f"WHERE {' AND '.join(clauses)} ORDER BY id DESC LIMIT ?",
        parameters,
    ).fetchall()
    decoded: list[dict[str, object]] = []
    for encoded, runtime_turn, scene_generation, simulation_tick in reversed(rows):
        row = json.loads(encoded)
        if kind == "physics":
            row["correlation"] = {
                "runtimeTurn": runtime_turn,
                "sceneGeneration": scene_generation,
                "simulationTick": simulation_tick,
            }
        if target_id is not None and kind != "physics":
            if not _row_matches_target(row, target_id):
                continue
        decoded.append(row if full else _compact(row))
    if kind == "summary":
        latest_by_topic: dict[str, dict[str, object]] = {}
        for row in decoded:
            latest_by_topic[str(row.get("topic"))] = row
        decoded = list(latest_by_topic.values())
    return decoded, target_id


def tail_rows(
    connection: sqlite3.Connection,
    after_sequence: int,
    topic: str,
    limit: int,
    full: bool = False,
) -> list[dict[str, object]]:
    predicate = "source = 'runtime' AND sequence > ?"
    parameters: list[object] = [after_sequence]
    if topic != "*":
        predicate += " AND topic = ?"
        parameters.append(topic)
    parameters.append(limit)
    rows = connection.execute(
        f"SELECT row_json FROM rows WHERE {predicate} ORDER BY sequence LIMIT ?",
        parameters,
    ).fetchall()
    decoded = [json.loads(row[0]) for row in rows]
    return decoded if full else [_compact(row) for row in decoded]
