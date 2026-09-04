#!/usr/bin/env python3
"""Validate incremental, bounded Skarness queries and live Physics joins."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from skarness import SkarnessConnection
from skarness_query import import_session, open_query_cache, query_profile, tail_rows


REPO = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def encoded(row: dict[str, object]) -> bytes:
    return json.dumps(row, separators=(",", ":")).encode("utf-8")


def write_fixture(directory: Path) -> tuple[Path, Path]:
    runtime = directory / "runtime.skarness.ndjson"
    physics = directory / "physics.physicsdiag.ndjson"
    runtime_rows = [
        {
            "schemaVersion": 1,
            "sequence": 1,
            "runtimeTurn": 4,
            "sceneGeneration": 2,
            "simulationTick": 10,
            "renderFrame": 4,
            "replayFrame": 10,
            "topic": "session.state",
            "kind": "snapshot",
            "payload": {"paused": True},
        },
        {
            "schemaVersion": 1,
            "sequence": 2,
            "runtimeTurn": 4,
            "sceneGeneration": 2,
            "simulationTick": 10,
            "renderFrame": 4,
            "replayFrame": 10,
            "topic": "replay.cause",
            "kind": "snapshot",
            "payload": {"rows": [{"kind": 0, "id": 7, "name": "ball_x"}]},
        },
        {
            "schemaVersion": 1,
            "sequence": 3,
            "runtimeTurn": 4,
            "sceneGeneration": 2,
            "simulationTick": 10,
            "renderFrame": 4,
            "replayFrame": 10,
            "topic": "replay.prediction.topology",
            "kind": "change",
            "payload": {"targetId": 7, "nodes": [{"id": 7}]},
        },
        {
            "schemaVersion": 1,
            "sequence": 4,
            "runtimeTurn": 4,
            "sceneGeneration": 2,
            "simulationTick": 10,
            "renderFrame": 4,
            "replayFrame": 10,
            "topic": "replay.render_submission",
            "kind": "change",
            "payload": {"targetId": 7, "segmentCount": 8, "stableHash": 99},
        },
    ]
    partial_runtime = {
        "schemaVersion": 1,
        "sequence": 5,
        "runtimeTurn": 5,
        "sceneGeneration": 2,
        "simulationTick": 11,
        "renderFrame": 5,
        "replayFrame": 11,
        "topic": "replay.prediction.frames",
        "kind": "append",
        "payload": {"count": 1, "frames": [{"frame": 11}]},
    }
    runtime.write_bytes(b"\n".join(encoded(row) for row in runtime_rows) + b"\n" + encoded(partial_runtime))
    physics_rows = [
        {"kind": "correlation", "runtimeTurn": 4, "sceneGeneration": 2, "simulationTick": 10},
        {"kind": "body", "body_id": 7, "name": "ball_x", "pos": [1.0, 2.0, 3.0]},
    ]
    partial_physics = {"kind": "contact", "body_a": 7, "body_b": 8}
    physics.write_bytes(b"\n".join(encoded(row) for row in physics_rows) + b"\n" + encoded(partial_physics))
    return runtime, physics


def validate_incremental_import(root: Path) -> dict[str, object]:
    root.mkdir(parents=True, exist_ok=True)
    runtime, physics = write_fixture(root)
    connection = open_query_cache(root)
    try:
        first = import_session(connection, runtime, physics)
        require(first["runtime"]["importedRows"] == 4, f"runtime partial line imported: {first}")
        require(first["physics"]["importedRows"] == 2, f"Physics partial line imported: {first}")

        with runtime.open("ab") as output:
            output.write(b"\n")
        with physics.open("ab") as output:
            output.write(b"\n")
        second = import_session(connection, runtime, physics)
        require(second["runtime"]["importedRows"] == 1, f"runtime incremental import was not exact: {second}")
        require(second["physics"]["importedRows"] == 1, f"Physics incremental import was not exact: {second}")
        third = import_session(connection, runtime, physics)
        require(third["runtime"]["importedRows"] == 0, "runtime rows were imported twice")
        require(third["physics"]["importedRows"] == 0, "Physics rows were imported twice")

        prediction, target_id = query_profile(connection, "prediction", 20, target="ball_x")
        require(target_id == 7 and prediction, "named target did not resolve from durable evidence")
        render, _ = query_profile(connection, "render-submission", 20, frames="10:10", target="7")
        require(len(render) == 1, f"frame/target filtering failed: {render}")
        tailed = tail_rows(connection, 3, "*", 20)
        require([row["sequence"] for row in tailed] == [4, 5], f"tail cursor skipped or repeated rows: {tailed}")
        physics_rows = query_profile(connection, "physics", 20)[0]
        body = next(row for row in physics_rows if row.get("kind") == "body")
        require(
            body["correlation"] == {"runtimeTurn": 4, "sceneGeneration": 2, "simulationTick": 10},
            f"Physics row lost its correlation: {body}",
        )

        replacement = {
            "schemaVersion": 1,
            "sequence": 1,
            "runtimeTurn": 1,
            "sceneGeneration": 9,
            "simulationTick": 0,
            "renderFrame": 1,
            "replayFrame": 0,
            "topic": "session.state",
            "kind": "snapshot",
            "payload": {"paused": True},
        }
        runtime.write_bytes(encoded(replacement) + b"\n")
        replaced = import_session(connection, runtime, physics)
        require(replaced["runtime"]["replaced"] is True, "replaced runtime source did not invalidate cache")
        runtime_count = connection.execute("SELECT COUNT(*) FROM rows WHERE source = 'runtime'").fetchone()[0]
        require(runtime_count == 1, f"stale runtime rows survived source replacement: {runtime_count}")
    finally:
        connection.close()
    return {"initial": first, "incremental": second, "targetId": target_id}


def launch_live(session: Path, executable: Path, scene: Path) -> None:
    launched = subprocess.run(
        [
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
            "--detail",
            "full",
        ],
        cwd=REPO,
        check=False,
        capture_output=True,
        text=True,
    )
    require(launched.returncode == 0, f"live query launch failed: {launched.stderr or launched.stdout}")


def validate_live(session: Path, executable: Path, scene: Path) -> dict[str, object]:
    launch_live(session, executable, scene)
    control = SkarnessConnection(session)
    try:
        step = control.wait(control.send("run.step", {"count": 3}, request_id="query-live-step"))
        require(step.get("status") == "applied", f"live query step failed: {step}")
        manifest = control.manifest
        runtime = Path(str(manifest["stateTrace"]))
        physics = Path(str(manifest["physicsTrace"]))
        database = open_query_cache(session)
        try:
            imported = import_session(database, runtime, physics)
            require(imported["runtime"]["importedRows"] > 0, "live runtime trace was not readable")
            require(imported["physics"]["importedRows"] > 0, "live Physics trace was not readable")
            for profile in ("summary", "scene", "replay", "prediction", "cause", "render-submission", "physics"):
                rows, _ = query_profile(database, profile, 20)
                require(rows, f"live {profile} query returned no rows")
            joined = database.execute(
                """
                SELECT COUNT(*) FROM rows AS p
                WHERE p.source = 'physics' AND EXISTS (
                    SELECT 1 FROM rows AS r
                    WHERE r.source = 'runtime'
                      AND r.runtime_turn = p.runtime_turn
                      AND r.scene_generation = p.scene_generation
                      AND r.simulation_tick = p.simulation_tick
                )
                """
            ).fetchone()[0]
            require(joined > 0, "live Physics rows did not join to runtime state")
        finally:
            database.close()
        return {"imported": imported, "joinedPhysicsRows": joined}
    finally:
        try:
            control.wait(control.send("session.stop", request_id="query-live-stop"))
        except (OSError, RuntimeError):
            pass
        control.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--session", type=Path, default=REPO / "TestOutput" / "skarness" / "query-validation")
    parser.add_argument("--exe", type=Path, default=REPO / "Automation" / "SKULLBONEZ_CORE.exe")
    parser.add_argument(
        "--scene",
        type=Path,
        default=REPO / "SkullbonezData" / "scenes" / "replay_prediction_simple.scene.json",
    )
    args = parser.parse_args()
    try:
        if args.self_test:
            with tempfile.TemporaryDirectory(prefix="skarness-query-", dir=REPO / "TestOutput") as temporary:
                result = {"ok": True, "incremental": validate_incremental_import(Path(temporary))}
        else:
            result = {
                "ok": True,
                "live": validate_live(args.session.resolve(), args.exe.resolve(), args.scene.resolve()),
            }
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, RuntimeError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
