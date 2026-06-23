#
# File: tools/check_replay_scrub_regression.py
# Purpose:
#   Validates that replay scrub probes move presentation state backward and are
#   queryable through SkullScope.
#
# Mental model:
#   The replay scrubber is a presentation feature. This test drives the CLI
#   probe, imports the emitted SkullScope trace, and asserts relational facts
#   instead of maintaining a fragile full-trace baseline.
#
# Glossary:
#   SkullScope: Queryable physics diagnostics workflow backed by bounded trace
#   output and local SQLite queries.
#   Replay scrub: Selecting an older retained presentation sample for visual
#   inspection while live simulation state can continue afterward.
#
# Related:
#   - AGENTS.md
#   - Agentic/Reference/comment-style-guide.md
#
#
"""
Focused replay scrub SkullScope regression.

The runtime emits one replay_scrub row when --replay-scrub-test is present and
one replay_restore row when --replay-restore-test is present. The query layer
then proves that scrub selects an older frame and that restore recomputes the
original solver hash before making the selected frame live.
"""

import json
import os
from pathlib import Path
import subprocess
import sys


REPO = Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1])).resolve()
TRACE = REPO / "Debug" / "replay_scrub.physicsdiag.ndjson"
RESTORE_TRACE = REPO / "Debug" / "replay_restore.physicsdiag.ndjson"
SCENE_ARG = "SkullbonezData/scenes/physics_roll.scene.json"
EXE = REPO / "Debug" / "SKULLBONEZ_CORE.exe"
QUERY_BAT = REPO / "tools" / "physics_query.bat"


def remove_if_exists(path):
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def run_checked(args, cwd):
    result = subprocess.run(
        args,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        raise RuntimeError(f"command failed with exit code {result.returncode}: {' '.join(map(str, args))}")
    return result.stdout


def generate_trace(trace, probe_flag):
    if not EXE.exists():
        raise RuntimeError(f"Debug executable not found: {EXE}")

    remove_if_exists(trace)
    remove_if_exists(trace.with_suffix(".sqlite"))
    remove_if_exists(trace.with_suffix(".sqlite.lock"))

    command = [
        str(EXE),
        "--renderer",
        "dx12",
        "--vsync",
        "off",
        "--shadows",
        "off",
        "--scene",
        SCENE_ARG,
        "--frames",
        "120",
        "--replay",
        "on",
        "--replay-seconds",
        "1",
        probe_flag,
        "--physics-diag",
        str(trace),
    ]
    print("  Trace command:")
    print("    " + " ".join(command))
    run_checked(command, REPO)
    if not trace.exists():
        raise RuntimeError(f"diagnostic trace was not produced: {trace}")


def query_replay():
    command = [str(QUERY_BAT), str(TRACE), "replay", "--limit", "8"]
    print("  Query command:")
    print("    tools\\physics_query.bat Debug\\replay_scrub.physicsdiag.ndjson replay --limit 8")
    stdout = run_checked(command, REPO)
    return stdout, json.loads(stdout)


def query_restore():
    command = [str(QUERY_BAT), str(RESTORE_TRACE), "restore", "--limit", "8"]
    print("  Restore query command:")
    print("    tools\\physics_query.bat Debug\\replay_restore.physicsdiag.ndjson restore --limit 8")
    stdout = run_checked(command, REPO)
    return stdout, json.loads(stdout)


def validate_payload(payload):
    scrubs = payload.get("scrubs") or []
    if len(scrubs) != 1:
        raise RuntimeError(f"expected exactly one replay scrub probe row, found {len(scrubs)}")
    scrub = scrubs[0]
    if not payload.get("passed") or not scrub.get("passed"):
        raise RuntimeError(f"replay scrub query checks failed: {json.dumps(scrub.get('checks', {}), sort_keys=True)}")

    checks = scrub.get("checks", {})
    required = [
        "olderSample",
        "hashChanged",
        "movedBody",
        "appliedAndRestored",
        "selectedTraceMatches",
        "liveTraceMatches",
    ]
    missing = [name for name in required if not checks.get(name)]
    if missing:
        raise RuntimeError(f"replay scrub missing required checks: {', '.join(missing)}")

    print(
        "  PASS: selected replay frame {selected} moved body {body} back before live frame {live} (distance_sq={distance:.6f})".format(
            selected=scrub.get("selected_replay_frame"),
            live=scrub.get("live_replay_frame"),
            body=scrub.get("model_index"),
            distance=scrub.get("distance_sq") or 0.0,
        )
    )


def validate_restore_payload(payload):
    restores = payload.get("restores") or []
    if len(restores) != 1:
        raise RuntimeError(f"expected exactly one replay restore probe row, found {len(restores)}")
    restore = restores[0]
    if not payload.get("passed") or not restore.get("passed"):
        raise RuntimeError(f"replay restore query checks failed: {json.dumps(restore.get('checks', {}), sort_keys=True)}")

    checks = restore.get("checks", {})
    required = ["hashCaptured", "hashMatched", "bodyCountMatched", "noFallbackNeeded"]
    missing = [name for name in required if not checks.get(name)]
    if missing:
        raise RuntimeError(f"replay restore missing required checks: {', '.join(missing)}")

    print(
        "  PASS: restored replay frame {target} with solver hash {solver_hash}".format(
            target=restore.get("target_replay_frame"),
            solver_hash=restore.get("target_solver_hash"),
        )
    )


def main():
    try:
        print("  Generating replay scrub SkullScope trace...")
        generate_trace(TRACE, "--replay-scrub-test")
        print("  Running replay SkullScope query...")
        query_stdout, payload = query_replay()
        validate_payload(payload)
        print("  Generating replay restore SkullScope trace...")
        generate_trace(RESTORE_TRACE, "--replay-restore-test")
        print("  Running replay restore SkullScope query...")
        restore_stdout, restore_payload = query_restore()
        validate_restore_payload(restore_payload)
        sqlite_path = TRACE.with_suffix(".sqlite")
        restore_sqlite_path = RESTORE_TRACE.with_suffix(".sqlite")
        print(f"  Trace bytes: {TRACE.stat().st_size}")
        print(f"  SQLite bytes: {sqlite_path.stat().st_size if sqlite_path.exists() else 0}")
        print(f"  Query output bytes: {len(query_stdout.encode('utf-8'))}")
        print(f"  Restore trace bytes: {RESTORE_TRACE.stat().st_size}")
        print(f"  Restore SQLite bytes: {restore_sqlite_path.stat().st_size if restore_sqlite_path.exists() else 0}")
        print(f"  Restore query output bytes: {len(restore_stdout.encode('utf-8'))}")
        return 0
    except Exception as exc:
        print(f"  FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
