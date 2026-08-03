#!/usr/bin/env python3
#
# File: tools/measure_dense_pile_sleep.py
# Purpose:
#   Reduce one SkullScope trace to repeatable sleep-resolution and kinetic-energy metrics.
#
# Summary:
#   Reuses the Physics query cache as the trace owner, then derives permanent
#   all-sleep time, per-body sleep/wake transitions, and fixed kinetic-energy
#   checkpoints without loading raw diagnostic rows into agent context.
#
# Glossary:
#   Permanent all-sleep frame: Earliest frame from which every dynamic body is
#     sleeping through the end of the measured horizon.
#   Wake oscillation: A sleeping-to-awake transition for one dynamic body.
#   Sleep-state quiescence: First frame after the final sleep-bit transition;
#     this can exist even when a body remains permanently awake.
#
# Invariants:
#   - Dynamic bodies are selected by positive inverse mass in the trace, never
#     by an authored-name convention or expected count.
#   - Quiescence is not inferred from a low-energy threshold. Permanent sleep
#     and the last sleep-bit transition remain separately visible facts.
#   - Kinetic energy comes from Physics-owned frame totals; this tool neither
#     reconstructs inertia nor changes the simulation.
#   - Every SQL answer is bounded. Full per-body rows are written only to the
#     requested JSON artifact, while stdout remains a compact decision packet.
#
# Related:
#   - tools/physics_query.py
#   - SkullbonezData/scenes/box_pile_throw_300.scene.json
#   - Agentic/Plans/TODO/dense-pile-sleep-resolution.md
#   - Agentic/Reference/engine-glossary.md
#
"""Measure sleep resolution and kinetic-energy history from a Physics trace."""

from __future__ import annotations

import argparse
import csv
import json
import sqlite3
import sys
from pathlib import Path
from typing import Any

import physics_query


DEFAULT_CHECKPOINTS = (0, 300, 600, 1200, 2400, 3600, 4800, 6800)
DEFAULT_TAIL_FRAMES = 300


def row_dict(row: sqlite3.Row | None) -> dict[str, Any] | None:
    if row is None:
        return None
    return {key: row[key] for key in row.keys()}


def trace_run_id(conn: sqlite3.Connection) -> str:
    rows = conn.execute("SELECT DISTINCT run_id FROM frames ORDER BY run_id").fetchall()
    if len(rows) != 1:
        raise ValueError(f"expected exactly one run_id, found {len(rows)}")
    return str(rows[0][0])


def body_rows(conn: sqlite3.Connection, run_id: str) -> list[dict[str, Any]]:
    # Concept: a transition is a change in the published sleeping bit, not a
    # speed crossing. Keeping the two facts separate prevents threshold policy
    # from manufacturing or hiding a wake oscillation.
    rows = conn.execute(
        """
        WITH dynamic AS (
            SELECT frame, body_id, name, sleeping,
                   LAG(sleeping) OVER (PARTITION BY body_id ORDER BY frame) previous_sleeping
            FROM bodies
            WHERE run_id = ? AND inv_mass > 0
        )
        SELECT body_id,
               MAX(name) name,
               MIN(CASE WHEN sleeping = 1 THEN frame END) first_sleep_frame,
               MAX(CASE WHEN previous_sleeping = 1 AND sleeping = 0 THEN frame END) last_wake_frame,
               SUM(CASE WHEN previous_sleeping IS NOT NULL
                             AND sleeping != previous_sleeping THEN 1 ELSE 0 END) sleep_state_transitions,
               SUM(CASE WHEN previous_sleeping = 1 AND sleeping = 0 THEN 1 ELSE 0 END) wake_transitions,
               MAX(sleeping) ever_slept,
               MAX(CASE WHEN frame = (SELECT MAX(frame) FROM frames WHERE run_id = ?)
                        THEN sleeping ELSE 0 END) final_sleeping
        FROM dynamic
        GROUP BY body_id
        ORDER BY body_id
        """,
        (run_id, run_id),
    ).fetchall()
    return [row_dict(row) for row in rows]


def permanent_all_sleep_frame(conn: sqlite3.Connection, run_id: str, dynamic_count: int) -> int | None:
    row = conn.execute(
        """
        WITH dynamic_frames AS (
            SELECT frame, SUM(sleeping) sleeping_count
            FROM bodies
            WHERE run_id = ? AND inv_mass > 0
            GROUP BY frame
        )
        SELECT MIN(candidate.frame)
        FROM dynamic_frames candidate
        WHERE candidate.sleeping_count = ?
          AND NOT EXISTS (
              SELECT 1 FROM dynamic_frames later
              WHERE later.frame >= candidate.frame AND later.sleeping_count < ?
          )
        """,
        (run_id, dynamic_count, dynamic_count),
    ).fetchone()
    return None if row is None or row[0] is None else int(row[0])


def energy_summary(
    conn: sqlite3.Connection,
    run_id: str,
    first_frame: int,
    last_frame: int,
    checkpoints: tuple[int, ...],
    tail_frames: int,
) -> dict[str, Any]:
    tail_start = max(first_frame, last_frame - tail_frames + 1)
    summary = row_dict(
        conn.execute(
            """
            SELECT MIN(total_energy) minimum,
                   MAX(total_energy) peak,
                   AVG(CASE WHEN frame >= ? THEN total_energy END) tail_mean,
                   MAX(CASE WHEN frame >= ? THEN total_energy END) tail_peak
            FROM frames
            WHERE run_id = ?
            """,
            (tail_start, tail_start, run_id),
        ).fetchone()
    )
    selected = sorted({frame for frame in checkpoints if first_frame <= frame <= last_frame} | {first_frame, last_frame})
    placeholders = ",".join("?" for _ in selected)
    rows = conn.execute(
        f"""
        SELECT frame, total_energy, linear_energy, angular_energy, sleeping_count, awake_count
        FROM frames
        WHERE run_id = ? AND frame IN ({placeholders})
        ORDER BY frame
        """,
        (run_id, *selected),
    ).fetchall()
    checkpoint_rows = [row_dict(row) for row in rows]
    if len(checkpoint_rows) != len(selected):
        found = {int(row["frame"]) for row in checkpoint_rows}
        missing = [frame for frame in selected if frame not in found]
        raise ValueError(f"missing requested energy checkpoint frames: {missing}")
    summary["tail_start_frame"] = tail_start
    summary["checkpoints"] = checkpoint_rows
    return summary


def measure_horizon_rows(rows: Any, dynamic_body_ids: set[int]) -> dict[str, Any]:
    prior_sleeping: dict[int, int | None] = {body_id: None for body_id in dynamic_body_ids}
    ever_slept: set[int] = set()
    transition_counts = {body_id: 0 for body_id in dynamic_body_ids}
    wake_counts = {body_id: 0 for body_id in dynamic_body_ids}
    first_sleep: int | None = None
    last_transition: int | None = None
    first_observed_frame: int | None = None
    current_frame: int | None = None
    current_frame_seen: set[int] = set()
    current_frame_sleeping = 0
    all_sleep_candidate: int | None = None
    frame_count = 0

    def finish_frame() -> None:
        nonlocal all_sleep_candidate, frame_count
        if current_frame is None:
            return
        if current_frame_seen != dynamic_body_ids:
            missing = sorted(dynamic_body_ids - current_frame_seen)
            raise ValueError(f"CSV frame {current_frame} is missing dynamic body ids {missing[:8]}")
        frame_count += 1
        if current_frame_sleeping == len(dynamic_body_ids):
            if all_sleep_candidate is None:
                all_sleep_candidate = current_frame
        else:
            all_sleep_candidate = None

    for row in rows:
        body_id = int(row["idx"])
        if body_id not in dynamic_body_ids:
            continue
        frame = int(row["frame"])
        if first_observed_frame is None:
            first_observed_frame = frame
        if current_frame != frame:
            finish_frame()
            current_frame = frame
            current_frame_seen = set()
            current_frame_sleeping = 0
        if body_id in current_frame_seen:
            raise ValueError(f"CSV frame {frame} repeats dynamic body id {body_id}")
        current_frame_seen.add(body_id)
        sleeping = int(row["sleeping"])
        if sleeping not in (0, 1):
            raise ValueError(f"CSV frame {frame} body {body_id} has invalid sleeping={sleeping}")
        current_frame_sleeping += sleeping
        if sleeping:
            ever_slept.add(body_id)
            first_sleep = frame if first_sleep is None else min(first_sleep, frame)
        previous = prior_sleeping[body_id]
        if previous is not None and previous != sleeping:
            transition_counts[body_id] += 1
            last_transition = frame
            if previous == 1 and sleeping == 0:
                wake_counts[body_id] += 1
        prior_sleeping[body_id] = sleeping
    finish_frame()
    if current_frame is None:
        raise ValueError("CSV contains no dynamic body rows")
    never_sleeping = sorted(dynamic_body_ids - ever_slept)
    return {
        "first_frame": first_observed_frame,
        "last_frame": current_frame,
        "frame_rows": frame_count,
        "first_sleep_frame": first_sleep,
        "permanent_all_sleep_frame": all_sleep_candidate,
        "last_sleep_state_transition_frame": last_transition,
        "sleep_state_quiescence_frame": first_observed_frame if last_transition is None else last_transition + 1,
        "never_sleeping_count": len(never_sleeping),
        "never_sleeping_body_ids": never_sleeping,
        "final_sleeping_count": sum(int(value or 0) for value in prior_sleeping.values()),
        "wake_oscillation_count": sum(wake_counts.values()),
        "bodies_with_wake_oscillation": sum(1 for count in wake_counts.values() if count > 0),
        "maximum_wake_oscillations_per_body": max(wake_counts.values(), default=0),
        "maximum_sleep_state_transitions_per_body": max(transition_counts.values(), default=0),
    }


def measure_horizon_csv(path: Path, dynamic_body_ids: set[int]) -> dict[str, Any]:
    with path.open("r", encoding="utf-8", newline="") as stream:
        result = measure_horizon_rows(csv.DictReader(stream), dynamic_body_ids)
    result["csv"] = str(path.resolve())
    result["csv_bytes"] = path.stat().st_size
    return result


def measure(
    conn: sqlite3.Connection,
    cache: dict[str, Any],
    checkpoints: tuple[int, ...],
    tail_frames: int,
    horizon_csv: Path | None = None,
) -> dict[str, Any]:
    run_id = trace_run_id(conn)
    bounds = row_dict(
        conn.execute(
            """
            SELECT MIN(frame) first_frame, MAX(frame) last_frame, COUNT(*) frame_rows,
                   MIN(dt) minimum_dt, MAX(dt) maximum_dt
            FROM frames WHERE run_id = ?
            """,
            (run_id,),
        ).fetchone()
    )
    if bounds is None or bounds["first_frame"] is None:
        raise ValueError("trace contains no frame rows")
    first_frame = int(bounds["first_frame"])
    last_frame = int(bounds["last_frame"])

    body_metrics = body_rows(conn, run_id)
    dynamic_count = len(body_metrics)
    if dynamic_count == 0:
        raise ValueError("trace contains no dynamic bodies")
    first_sleep = min(
        (int(row["first_sleep_frame"]) for row in body_metrics if row["first_sleep_frame"] is not None),
        default=None,
    )
    last_transition = conn.execute(
        """
        WITH dynamic AS (
            SELECT frame, body_id, sleeping,
                   LAG(sleeping) OVER (PARTITION BY body_id ORDER BY frame) previous_sleeping
            FROM bodies WHERE run_id = ? AND inv_mass > 0
        )
        SELECT MAX(frame) FROM dynamic
        WHERE previous_sleeping IS NOT NULL AND sleeping != previous_sleeping
        """,
        (run_id,),
    ).fetchone()[0]
    permanent_sleep = permanent_all_sleep_frame(conn, run_id, dynamic_count)
    wake_bodies = [row for row in body_metrics if int(row["wake_transitions"] or 0) > 0]
    never_sleeping = [row for row in body_metrics if int(row["ever_slept"] or 0) == 0]
    final_sleeping = sum(int(row["final_sleeping"] or 0) for row in body_metrics)

    result = {
        "trace": cache["trace"],
        "sqlite": cache["sqlite"],
        "cache_rebuilt": bool(cache["rebuilt"]),
        "trace_bytes": Path(cache["trace"]).stat().st_size,
        "sqlite_bytes": Path(cache["sqlite"]).stat().st_size,
        "run_id": run_id,
        "first_frame": first_frame,
        "last_frame": last_frame,
        "frame_rows": int(bounds["frame_rows"]),
        "minimum_dt": bounds["minimum_dt"],
        "maximum_dt": bounds["maximum_dt"],
        "dynamic_body_count": dynamic_count,
        "first_sleep_frame": first_sleep,
        "permanent_all_sleep_frame": permanent_sleep,
        "last_sleep_state_transition_frame": None if last_transition is None else int(last_transition),
        "sleep_state_quiescence_frame": first_frame if last_transition is None else int(last_transition) + 1,
        "never_sleeping_count": len(never_sleeping),
        "never_sleeping_body_ids": [int(row["body_id"]) for row in never_sleeping],
        "final_sleeping_count": final_sleeping,
        "wake_oscillation_count": sum(int(row["wake_transitions"] or 0) for row in body_metrics),
        "bodies_with_wake_oscillation": len(wake_bodies),
        "maximum_wake_oscillations_per_body": max(
            (int(row["wake_transitions"] or 0) for row in body_metrics), default=0
        ),
        "kinetic_energy": energy_summary(conn, run_id, first_frame, last_frame, checkpoints, tail_frames),
        "body_sleep_rows": body_metrics,
    }
    if horizon_csv is not None:
        result["extended_horizon"] = measure_horizon_csv(
            horizon_csv, {int(row["body_id"]) for row in body_metrics}
        )
    return result


def planted_database() -> sqlite3.Connection:
    conn = sqlite3.connect(":memory:")
    conn.row_factory = sqlite3.Row
    conn.executescript(
        """
        CREATE TABLE frames(run_id TEXT, frame INTEGER, dt REAL, total_energy REAL,
                            linear_energy REAL, angular_energy REAL, sleeping_count INTEGER,
                            awake_count INTEGER);
        CREATE TABLE bodies(run_id TEXT, frame INTEGER, body_id INTEGER, name TEXT,
                            inv_mass REAL, sleeping INTEGER);
        """
    )
    for frame, sleeping in enumerate(((0, 0), (1, 0), (1, 1), (0, 1), (1, 1))):
        conn.execute(
            "INSERT INTO frames VALUES('plant', ?, 0.5, ?, ?, ?, ?, ?)",
            (frame, 10.0 - frame, 8.0 - frame, 2.0, sum(sleeping), 2 - sum(sleeping)),
        )
        for body_id, value in enumerate(sleeping):
            conn.execute(
                "INSERT INTO bodies VALUES('plant', ?, ?, ?, 1.0, ?)",
                (frame, body_id, f"body_{body_id}", value),
            )
    return conn


def self_test() -> None:
    conn = planted_database()
    try:
        cache = {"trace": __file__, "sqlite": __file__, "rebuilt": False}
        result = measure(conn, cache, (0, 2, 4), 2)
    finally:
        conn.close()
    assert result["first_sleep_frame"] == 1
    assert result["permanent_all_sleep_frame"] == 4
    assert result["last_sleep_state_transition_frame"] == 4
    assert result["wake_oscillation_count"] == 1
    assert result["bodies_with_wake_oscillation"] == 1
    assert result["never_sleeping_count"] == 0
    assert result["final_sleeping_count"] == 2
    assert [row["frame"] for row in result["kinetic_energy"]["checkpoints"]] == [0, 2, 4]
    horizon_rows = [
        {"frame": str(frame), "idx": str(body_id), "sleeping": str(sleeping[body_id])}
        for frame, sleeping in enumerate(((0, 0), (1, 0), (1, 1), (0, 1), (1, 1)))
        for body_id in range(2)
    ]
    horizon = measure_horizon_rows(horizon_rows, {0, 1})
    assert horizon["permanent_all_sleep_frame"] == 4
    assert horizon["wake_oscillation_count"] == 1
    assert horizon["final_sleeping_count"] == 2
    print("measure_dense_pile_sleep self-test: PASS")


def compact_packet(result: dict[str, Any]) -> dict[str, Any]:
    return {key: value for key, value in result.items() if key != "body_sleep_rows"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", nargs="?", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--checkpoint", type=int, action="append")
    parser.add_argument("--tail-frames", type=int, default=DEFAULT_TAIL_FRAMES)
    parser.add_argument("--horizon-csv", type=Path)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.trace is None or args.output is None:
        raise SystemExit("ERROR: trace and --output are required unless --self-test is used")
    if args.tail_frames <= 0:
        raise SystemExit("ERROR: --tail-frames must be positive")
    checkpoints = tuple(args.checkpoint) if args.checkpoint else DEFAULT_CHECKPOINTS
    conn, cache = physics_query.ensure_db(args.trace)
    try:
        result = measure(conn, cache, checkpoints, args.tail_frames, args.horizon_csv)
    finally:
        conn.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(compact_packet(result), indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
