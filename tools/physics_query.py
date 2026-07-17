#
# File: tools/physics_query.py
# Purpose:
#   Documents and runs the physics_query.py developer/validation helper script.
#
# Mental model:
#   Tools are command-line guardrails around builds, validation, screenshots,
#   diagnostics, and artifact handling. They make the safe path repeatable and
#   keep output bounded for humans and agents.
#
# Glossary:
#   SQLite: Local embedded database used as a bounded query cache for large
#   diagnostics traces.
#   Contact-audio event: Runtime diagnostic row for one copied contact-audio
#   verdict, including rejection reason, score, body ids, and submitted flag.
#   Contact-audio frame: Compact reducer summary row with raw fact, patch,
#   merge, budget, quiet-rejection, and submitted voice counts for one step.
#   Contact-audio kind: Perceptual class that says what the sound model thought
#   a contact represented, separate from the pass/reject reason.
#   Validation gate: Repository script that proves a class of changes before
#   commit or PR.
#
# Invariants:
#   - Tool output should be bounded and readable because agents and humans use
#   it for decisions.
#
# Related:
#   - AGENTS.md
#   - Agentic/Reference/comment-style-guide.md
#
#
"""
Queryable physics diagnostics for SkullbonezCore.

Imports a *.physicsdiag.ndjson trace into a sibling SQLite cache, then runs
bounded local queries so agents do not need to ingest full physics logs.
"""

import argparse
import datetime as _dt
import json
import math
import os
from pathlib import Path
import sqlite3
import sys
import time


SCHEMA_VERSION = 7
DEFAULT_LIMIT = 50
SUMMARY_LIMIT = 20
BODY_SAMPLE_LIMIT = 120
# Invariant: contact-audio verdict timestamps are frame-based. The runtime uses
# a 120 Hz fixed physics step for deterministic diagnostics, so timeline windows
# convert milliseconds to frame buckets with this rate.
CONTACT_AUDIO_FIXED_HZ = 120.0
QUESTIONS_PATH = Path(__file__).resolve().parents[1] / "Agentic" / "Reference" / "physics-query-questions.json"


def utc_now():
    return _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds")


def utc_from_ns(ns):
    return _dt.datetime.fromtimestamp(ns / 1_000_000_000, _dt.timezone.utc).isoformat(timespec="seconds")


def as_int(value, default=None):
    if value is None:
        return default
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def as_float(value, default=None):
    if value is None:
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def as_json(value):
    return json.dumps(value if value is not None else {}, separators=(",", ":"), sort_keys=True)


def vector3(value):
    if not isinstance(value, list) or len(value) < 3:
        return (None, None, None)
    return (as_float(value[0]), as_float(value[1]), as_float(value[2]))


def vector4(value):
    if not isinstance(value, list) or len(value) < 4:
        return (None, None, None, None)
    return (as_float(value[0]), as_float(value[1]), as_float(value[2]), as_float(value[3]))


def parse_frame_range(text):
    if text is None:
        return (None, None)
    if ":" in text:
        start_text, end_text = text.split(":", 1)
        start = as_int(start_text, None) if start_text else None
        end = as_int(end_text, None) if end_text else None
        return (start, end)
    frame = as_int(text)
    return (frame, frame)


def split_csv_filter(text):
    if not text:
        return []
    return [part.strip() for part in text.split(",") if part.strip()]


def sqlite_path_for(trace_path):
    return trace_path.with_suffix(".sqlite")


def connect_db(sqlite_path):
    conn = sqlite3.connect(str(sqlite_path))
    conn.row_factory = sqlite3.Row
    return conn


def row_to_dict(row):
    if row is None:
        return None
    return {key: row[key] for key in row.keys()}


def rows_to_dicts(rows):
    return [row_to_dict(row) for row in rows]


def json_response(payload, pretty=False):
    indent = 2 if pretty else None
    print(json.dumps(payload, indent=indent, separators=None if pretty else (",", ":"), sort_keys=not pretty))


def create_schema(conn):
    # Concept: import raw NDJSON once, then answer bounded questions from SQLite.
    #
    # SkullScope traces can be large and noisy. The schema below turns the raw
    # event stream into indexed tables for the specific questions agents and
    # humans ask most often: frame summaries, body trajectories, contacts,
    # islands, support edges, broadphase stats, solver stats, pipeline stage
    # counts, and notable events. Query commands should print small JSON/text
    # answers rather than dumping raw trace files into the model context.
    conn.executescript(
        """
        create table metadata(
            key text primary key,
            value text not null
        );

        create table source_files(
            path text primary key,
            size_bytes integer not null,
            modified_ns integer not null,
            modified_utc text not null,
            imported_utc text not null,
            schema_version integer not null
        );

        create table runs(
            run_id text primary key,
            scene text,
            suite text,
            scene_index integer,
            load_count integer,
            manual_reset_count integer,
            seed integer,
            fixed_step integer,
            fixed_step_forced_by_diag integer,
            renderer text,
            solver text,
            target_frames integer,
            model_count integer,
            frame_count integer,
            end_frame integer,
            end_status text,
            config_json text
        );

        create table frames(
            run_id text not null,
            frame integer not null,
            time_seconds real,
            dt real,
            body_count integer,
            awake_count integer,
            sleeping_count integer,
            supported_count integer,
            inhibited_count integer,
            contact_count integer,
            island_count integer,
            total_energy real,
            linear_energy real,
            angular_energy real,
            max_speed real,
            max_speed_body integer,
            max_omega real,
            max_omega_body integer,
            max_penetration real,
            max_penetration_contact text,
            primary key(run_id, frame)
        );

        create table bodies(
            run_id text not null,
            frame integer not null,
            body_id integer not null,
            name text,
            shape text,
            pos_x real,
            pos_y real,
            pos_z real,
            vel_x real,
            vel_y real,
            vel_z real,
            omega_x real,
            omega_y real,
            omega_z real,
            q_x real,
            q_y real,
            q_z real,
            q_w real,
            speed real,
            omega_mag real,
            mass real,
            inv_mass real,
            inertia_x real,
            inertia_y real,
            inertia_z real,
            radius real,
            half_x real,
            half_y real,
            half_z real,
            linear_energy real,
            angular_energy real,
            sleeping integer,
            sleep_supported integer,
            sleep_inhibited integer,
            sleep_counter integer,
            island_id integer,
            primary key(run_id, frame, body_id)
        );

        create table contacts(
            run_id text not null,
            frame integer not null,
            contact_id text not null,
            body_a integer,
            body_b integer,
            contact_type text,
            feature_id integer,
            point_count integer,
            normal_x real,
            normal_y real,
            normal_z real,
            penetration real,
            normal_impulse real,
            pre_solve_normal_speed real,
            pre_solve_closing_speed real,
            pre_solve_slip_speed real,
            tangent_impulse real,
            slip_speed real,
            rolling_residual real,
            warm_started integer,
            supports_sleep integer,
            primary key(run_id, frame, contact_id)
        );

        create table islands(
            run_id text not null,
            frame integer not null,
            island_id integer not null,
            body_count integer,
            awake_count integer,
            sleeping_count integer,
            supported_count integer,
            inhibited_count integer,
            eligible integer,
            can_sleep integer,
            max_speed real,
            max_omega real,
            total_energy real,
            primary key(run_id, frame, island_id)
        );

        create table island_members(
            run_id text not null,
            frame integer not null,
            island_id integer not null,
            body_id integer not null,
            primary key(run_id, frame, island_id, body_id)
        );

        create table support_edges(
            run_id text not null,
            frame integer not null,
            supporter integer,
            supported integer,
            source text
        );

        create table broadphase(
            run_id text not null,
            frame integer not null,
            candidate_pairs integer,
            contact_pairs integer,
            rejected_pairs integer,
            active_cells integer,
            max_cell_occupancy integer,
            collision_cell_count integer,
            body_insertions integer,
            exact_aabb_cell_visits integer,
            sampled_sweep_cell_visits integer,
            entry_writes integer,
            duplicate_rejections integer,
            bucket_chain_entries_inspected integer,
            raw_pair_combinations integer,
            unique_pairs integer,
            filter_rejections integer,
            grid_emitted_candidates integer,
            primary key(run_id, frame)
        );

        create table solver_stats(
            run_id text not null,
            frame integer not null,
            row_count integer,
            cache_previous_rows integer,
            cache_hits integer,
            cache_misses integer,
            warm_started_rows integer,
            position_correction_rows integer,
            position_correction_total real,
            position_correction_max real,
            solver_iterations integer,
            primary key(run_id, frame)
        );

        create table pipeline_stages(
            run_id text not null,
            frame integer not null,
            record_count integer,
            stage_counts_json text,
            primary key(run_id, frame)
        );

        create table replay_scrubs(
            run_id text not null,
            frame integer not null,
            normalized real,
            selected_replay_frame integer,
            live_replay_frame integer,
            selected_scene_frame integer,
            live_scene_frame integer,
            selected_state_hash text,
            live_state_hash text,
            replay_body_id integer,
            model_index integer,
            name text,
            selected_x real,
            selected_y real,
            selected_z real,
            live_x real,
            live_y real,
            live_z real,
            distance_sq real,
            selected_body_count integer,
            live_body_count integer,
            applied integer,
            restored integer,
            pre_live_delta_sq real,
            applied_delta_sq real,
            restored_delta_sq real,
            primary key(run_id, frame)
        );

        create table replay_restores(
            run_id text not null,
            frame integer not null,
            target_replay_frame integer,
            restore_source text,
            checkpoint_replay_frame integer,
            target_scene_frame integer,
            target_solver_hash text,
            target_presentation_hash text,
            restored_solver_hash text,
            restored_presentation_hash text,
            target_body_count integer,
            restored_body_count integer,
            contact_count integer,
            pipeline_record_count integer,
            checkpoint_boundary integer,
            hash_captured integer,
            hash_matched integer,
            fallback_attempted integer,
            fallback_restored integer,
            failure_reason text,
            primary key(run_id, frame)
        );

        create table events(
            run_id text not null,
            event_id text not null,
            frame integer,
            type text,
            severity text,
            body_a integer,
            body_b integer,
            island_id integer,
            summary text,
            data_json text,
            primary key(run_id, event_id)
        );
        """
    )


def create_indexes(conn):
    conn.executescript(
        """
        create index idx_frames_energy on frames(run_id, total_energy desc);
        create index idx_frames_speed on frames(run_id, max_speed desc);
        create index idx_events_type on events(run_id, type, severity, frame);
        create index idx_events_frame on events(run_id, frame);
        create index idx_bodies_body_frame on bodies(run_id, body_id, frame);
        create index idx_bodies_name_frame on bodies(run_id, name, frame);
        create index idx_bodies_frame_speed on bodies(run_id, frame, speed desc);
        create index idx_contacts_frame_body_a on contacts(run_id, frame, body_a);
        create index idx_contacts_frame_body_b on contacts(run_id, frame, body_b);
        create index idx_contacts_penetration on contacts(run_id, penetration desc);
        create index idx_islands_frame on islands(run_id, frame);
        create index idx_members_body_frame on island_members(run_id, body_id, frame);
        create index idx_support_edges_frame on support_edges(run_id, frame);
        create index idx_solver_stats_frame on solver_stats(run_id, frame);
        create index idx_pipeline_stages_frame on pipeline_stages(run_id, frame);
        create index idx_replay_scrubs_frame on replay_scrubs(run_id, selected_replay_frame, live_replay_frame);
        create index idx_replay_restores_frame on replay_restores(run_id, target_replay_frame);
        """
    )


def is_cache_fresh(sqlite_path, trace_path, stat):
    if not sqlite_path.exists():
        return False
    try:
        conn = connect_db(sqlite_path)
        try:
            row = conn.execute(
                "select size_bytes, modified_ns, schema_version from source_files where path=?",
                (str(trace_path.resolve()),),
            ).fetchone()
        finally:
            conn.close()
    except sqlite3.Error:
        return False
    if row is None:
        return False
    return (
        row["size_bytes"] == stat.st_size
        and row["modified_ns"] == stat.st_mtime_ns
        and row["schema_version"] == SCHEMA_VERSION
    )


def rebuild_cache(trace_path, sqlite_path, stat):
    if sqlite_path.exists():
        sqlite_path.unlink()

    conn = connect_db(sqlite_path)
    try:
        conn.execute("pragma journal_mode=off")
        conn.execute("pragma synchronous=off")
        create_schema(conn)
        with conn:
            conn.execute(
                "insert into metadata(key, value) values(?, ?)",
                ("schema_version", str(SCHEMA_VERSION)),
            )
            conn.execute(
                """
                insert into source_files(path, size_bytes, modified_ns, modified_utc, imported_utc, schema_version)
                values(?, ?, ?, ?, ?, ?)
                """,
                (
                    str(trace_path.resolve()),
                    stat.st_size,
                    stat.st_mtime_ns,
                    utc_from_ns(stat.st_mtime_ns),
                    utc_now(),
                    SCHEMA_VERSION,
                ),
            )
            import_trace(conn, trace_path)
        create_indexes(conn)
        conn.commit()
    finally:
        conn.close()


def ensure_db(trace_arg):
    trace_path = Path(trace_arg)
    if not trace_path.exists():
        raise SystemExit(f"ERROR: trace not found: {trace_path}")
    trace_path = trace_path.resolve()
    sqlite_path = sqlite_path_for(trace_path)
    stat = trace_path.stat()
    rebuilt = False
    if not is_cache_fresh(sqlite_path, trace_path, stat):
        lock_path = sqlite_path.with_suffix(sqlite_path.suffix + ".lock")
        lock_fd = None
        deadline = time.time() + 60.0
        while lock_fd is None:
            if is_cache_fresh(sqlite_path, trace_path, stat):
                break
            try:
                lock_fd = os.open(str(lock_path), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
                os.write(lock_fd, str(os.getpid()).encode("ascii", errors="ignore"))
            except FileExistsError:
                if time.time() >= deadline:
                    raise SystemExit(f"ERROR: timed out waiting for SQLite cache rebuild lock: {lock_path}")
                time.sleep(0.1)
        if lock_fd is not None:
            try:
                if not is_cache_fresh(sqlite_path, trace_path, stat):
                    rebuild_cache(trace_path, sqlite_path, stat)
                    rebuilt = True
            finally:
                os.close(lock_fd)
                try:
                    lock_path.unlink()
                except FileNotFoundError:
                    pass
    conn = connect_db(sqlite_path)
    return conn, {"trace": str(trace_path), "sqlite": str(sqlite_path), "rebuilt": rebuilt}


def import_trace(conn, trace_path):
    # Import is deliberately tolerant of bad individual lines. A partially
    # written or interrupted diagnostic run should still be queryable up to the
    # last valid JSON object, and the bad-line count is recorded in metadata so
    # final reports can disclose trace quality.
    bad_lines = 0
    imported = {
        "run": 0,
        "frame": 0,
        "body": 0,
        "contact": 0,
        "island": 0,
        "island_member": 0,
        "support_edge": 0,
        "broadphase": 0,
        "solver_stats": 0,
        "pipeline_stages": 0,
        "replay_scrub": 0,
        "replay_restore": 0,
        "event": 0,
        "end": 0,
        "unknown": 0,
    }

    with trace_path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, 1):
            line = line.strip()
            if not line:
                continue
            try:
                item = json.loads(line)
            except json.JSONDecodeError:
                bad_lines += 1
                continue

            kind = item.get("kind")
            if kind == "run":
                insert_run(conn, item)
            elif kind == "frame":
                insert_frame(conn, item)
            elif kind == "body":
                insert_body(conn, item)
            elif kind == "contact":
                insert_contact(conn, item)
            elif kind == "island":
                insert_island(conn, item)
            elif kind == "island_member":
                insert_island_member(conn, item)
            elif kind == "support_edge":
                insert_support_edge(conn, item)
            elif kind == "broadphase":
                insert_broadphase(conn, item)
            elif kind == "solver_stats":
                insert_solver_stats(conn, item)
            elif kind == "pipeline_stages":
                insert_pipeline_stages(conn, item)
            elif kind == "replay_scrub":
                insert_replay_scrub(conn, item)
            elif kind == "replay_restore":
                insert_replay_restore(conn, item)
            elif kind == "event":
                insert_event(conn, item)
            elif kind == "end":
                insert_end(conn, item)
            else:
                imported["unknown"] += 1
                continue
            imported[kind] += 1

    conn.execute("insert into metadata(key, value) values(?, ?)", ("bad_lines", str(bad_lines)))
    conn.execute("insert into metadata(key, value) values(?, ?)", ("imported_counts", as_json(imported)))


def insert_run(conn, item):
    run_id = item.get("run") or item.get("run_id") or "run_0001"
    conn.execute(
        """
        insert or replace into runs(
            run_id, scene, suite, scene_index, load_count, manual_reset_count, seed,
            fixed_step, fixed_step_forced_by_diag, renderer, solver, target_frames,
            model_count, frame_count, config_json
        )
        values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            run_id,
            item.get("scene"),
            item.get("suite"),
            as_int(item.get("scene_index")),
            as_int(item.get("load_count")),
            as_int(item.get("manual_reset_count")),
            as_int(item.get("seed")),
            as_int(item.get("fixed_step")),
            as_int(item.get("fixed_step_forced_by_diag")),
            item.get("renderer"),
            item.get("solver"),
            as_int(item.get("target_frames")),
            as_int(item.get("model_count")),
            as_int(item.get("frame_count")),
            as_json(item.get("config")),
        ),
    )


def insert_frame(conn, item):
    conn.execute(
        """
        insert or replace into frames(
            run_id, frame, time_seconds, dt, body_count, awake_count, sleeping_count,
            supported_count, inhibited_count, contact_count, island_count, total_energy,
            linear_energy, angular_energy, max_speed, max_speed_body, max_omega,
            max_omega_body, max_penetration, max_penetration_contact
        )
        values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            item.get("run"),
            as_int(item.get("frame")),
            as_float(item.get("time_seconds")),
            as_float(item.get("dt")),
            as_int(item.get("body_count")),
            as_int(item.get("awake_count")),
            as_int(item.get("sleeping_count")),
            as_int(item.get("supported_count")),
            as_int(item.get("inhibited_count")),
            as_int(item.get("contact_count")),
            as_int(item.get("island_count")),
            as_float(item.get("total_energy")),
            as_float(item.get("linear_energy")),
            as_float(item.get("angular_energy")),
            as_float(item.get("max_speed")),
            as_int(item.get("max_speed_body")),
            as_float(item.get("max_omega")),
            as_int(item.get("max_omega_body")),
            as_float(item.get("max_penetration")),
            item.get("max_penetration_contact"),
        ),
    )


def insert_body(conn, item):
    pos = vector3(item.get("pos"))
    vel = vector3(item.get("vel"))
    omega = vector3(item.get("omega"))
    quat = vector4(item.get("q"))
    inertia = vector3(item.get("inertia"))
    half_extents = vector3(item.get("half_extents"))
    conn.execute(
        """
        insert or replace into bodies(
            run_id, frame, body_id, name, shape, pos_x, pos_y, pos_z, vel_x, vel_y,
            vel_z, omega_x, omega_y, omega_z, q_x, q_y, q_z, q_w, speed, omega_mag,
            mass, inv_mass, inertia_x, inertia_y, inertia_z, radius, half_x, half_y,
            half_z, linear_energy, angular_energy, sleeping, sleep_supported,
            sleep_inhibited, sleep_counter, island_id
        )
        values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            item.get("run"),
            as_int(item.get("frame")),
            as_int(item.get("body_id")),
            item.get("name"),
            item.get("shape"),
            pos[0],
            pos[1],
            pos[2],
            vel[0],
            vel[1],
            vel[2],
            omega[0],
            omega[1],
            omega[2],
            quat[0],
            quat[1],
            quat[2],
            quat[3],
            as_float(item.get("speed")),
            as_float(item.get("omega_mag")),
            as_float(item.get("mass")),
            as_float(item.get("inv_mass")),
            inertia[0],
            inertia[1],
            inertia[2],
            as_float(item.get("radius")),
            half_extents[0],
            half_extents[1],
            half_extents[2],
            as_float(item.get("linear_energy")),
            as_float(item.get("angular_energy")),
            as_int(item.get("sleeping")),
            as_int(item.get("sleep_supported")),
            as_int(item.get("sleep_inhibited")),
            as_int(item.get("sleep_counter")),
            as_int(item.get("island_id")),
        ),
    )


def insert_contact(conn, item):
    normal = vector3(item.get("normal"))
    conn.execute(
        """
        insert or replace into contacts(
            run_id, frame, contact_id, body_a, body_b, contact_type, feature_id,
            point_count, normal_x, normal_y, normal_z, penetration, normal_impulse,
            pre_solve_normal_speed, pre_solve_closing_speed, pre_solve_slip_speed,
            tangent_impulse, slip_speed, rolling_residual, warm_started, supports_sleep
        )
        values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            item.get("run"),
            as_int(item.get("frame")),
            item.get("contact_id") or item.get("id") or "",
            as_int(item.get("body_a")),
            as_int(item.get("body_b")),
            item.get("contact_type") or item.get("type"),
            as_int(item.get("feature_id")),
            as_int(item.get("point_count")),
            normal[0],
            normal[1],
            normal[2],
            as_float(item.get("penetration")),
            as_float(item.get("normal_impulse")),
            as_float(item.get("pre_solve_normal_speed")),
            as_float(item.get("pre_solve_closing_speed")),
            as_float(item.get("pre_solve_slip_speed")),
            as_float(item.get("tangent_impulse")),
            as_float(item.get("slip_speed")),
            as_float(item.get("rolling_residual")),
            as_int(item.get("warm_started")),
            as_int(item.get("supports_sleep")),
        ),
    )


def insert_island(conn, item):
    conn.execute(
        """
        insert or replace into islands(
            run_id, frame, island_id, body_count, awake_count, sleeping_count,
            supported_count, inhibited_count, eligible, can_sleep, max_speed,
            max_omega, total_energy
        )
        values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            item.get("run"),
            as_int(item.get("frame")),
            as_int(item.get("island_id")),
            as_int(item.get("body_count")),
            as_int(item.get("awake_count")),
            as_int(item.get("sleeping_count")),
            as_int(item.get("supported_count")),
            as_int(item.get("inhibited_count")),
            as_int(item.get("eligible")),
            as_int(item.get("can_sleep")),
            as_float(item.get("max_speed")),
            as_float(item.get("max_omega")),
            as_float(item.get("total_energy")),
        ),
    )


def insert_island_member(conn, item):
    conn.execute(
        "insert or replace into island_members(run_id, frame, island_id, body_id) values(?, ?, ?, ?)",
        (
            item.get("run"),
            as_int(item.get("frame")),
            as_int(item.get("island_id")),
            as_int(item.get("body_id")),
        ),
    )


def insert_support_edge(conn, item):
    conn.execute(
        "insert into support_edges(run_id, frame, supporter, supported, source) values(?, ?, ?, ?, ?)",
        (
            item.get("run"),
            as_int(item.get("frame")),
            as_int(item.get("supporter")),
            as_int(item.get("supported")),
            item.get("source"),
        ),
    )


def insert_broadphase(conn, item):
    conn.execute(
        """
        insert or replace into broadphase(
            run_id, frame, candidate_pairs, contact_pairs, rejected_pairs, active_cells,
            max_cell_occupancy, collision_cell_count, body_insertions,
            exact_aabb_cell_visits, sampled_sweep_cell_visits, entry_writes,
            duplicate_rejections, bucket_chain_entries_inspected,
            raw_pair_combinations, unique_pairs, filter_rejections,
            grid_emitted_candidates
        )
        values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            item.get("run"),
            as_int(item.get("frame")),
            as_int(item.get("candidate_pairs")),
            as_int(item.get("contact_pairs")),
            as_int(item.get("rejected_pairs")),
            as_int(item.get("active_cells")),
            as_int(item.get("max_cell_occupancy")),
            as_int(item.get("collision_cell_count")),
            as_int(item.get("body_insertions")),
            as_int(item.get("exact_aabb_cell_visits")),
            as_int(item.get("sampled_sweep_cell_visits")),
            as_int(item.get("entry_writes")),
            as_int(item.get("duplicate_rejections")),
            as_int(item.get("bucket_chain_entries_inspected")),
            as_int(item.get("raw_pair_combinations")),
            as_int(item.get("unique_pairs")),
            as_int(item.get("filter_rejections")),
            as_int(item.get("grid_emitted_candidates")),
        ),
    )


def insert_solver_stats(conn, item):
    conn.execute(
        """
        insert or replace into solver_stats(
            run_id, frame, row_count, cache_previous_rows, cache_hits, cache_misses,
            warm_started_rows, position_correction_rows, position_correction_total,
            position_correction_max, solver_iterations
        )
        values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            item.get("run"),
            as_int(item.get("frame")),
            as_int(item.get("row_count")),
            as_int(item.get("cache_previous_rows")),
            as_int(item.get("cache_hits")),
            as_int(item.get("cache_misses")),
            as_int(item.get("warm_started_rows")),
            as_int(item.get("position_correction_rows")),
            as_float(item.get("position_correction_total")),
            as_float(item.get("position_correction_max")),
            as_int(item.get("solver_iterations")),
        ),
    )


def insert_pipeline_stages(conn, item):
    stage_counts = {
        key: as_int(value, 0)
        for key, value in item.items()
        if key not in ("kind", "run", "frame", "record_count")
    }
    conn.execute(
        """
        insert or replace into pipeline_stages(
            run_id, frame, record_count, stage_counts_json
        )
        values(?, ?, ?, ?)
        """,
        (
            item.get("run"),
            as_int(item.get("frame")),
            as_int(item.get("record_count")),
            as_json(stage_counts),
        ),
    )


def insert_replay_scrub(conn, item):
    selected_pos = vector3(item.get("selected_pos"))
    live_pos = vector3(item.get("live_pos"))
    conn.execute(
        """
        insert or replace into replay_scrubs(
            run_id, frame, normalized, selected_replay_frame, live_replay_frame,
            selected_scene_frame, live_scene_frame, selected_state_hash, live_state_hash,
            replay_body_id, model_index, name, selected_x, selected_y, selected_z,
            live_x, live_y, live_z, distance_sq, selected_body_count, live_body_count,
            applied, restored, pre_live_delta_sq, applied_delta_sq, restored_delta_sq
        )
        values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            item.get("run"),
            as_int(item.get("frame")),
            as_float(item.get("normalized")),
            as_int(item.get("selected_replay_frame")),
            as_int(item.get("live_replay_frame")),
            as_int(item.get("selected_scene_frame")),
            as_int(item.get("live_scene_frame")),
            str(item.get("selected_state_hash")) if item.get("selected_state_hash") is not None else None,
            str(item.get("live_state_hash")) if item.get("live_state_hash") is not None else None,
            as_int(item.get("body_id")),
            as_int(item.get("model_index")),
            item.get("name"),
            selected_pos[0],
            selected_pos[1],
            selected_pos[2],
            live_pos[0],
            live_pos[1],
            live_pos[2],
            as_float(item.get("distance_sq")),
            as_int(item.get("selected_body_count")),
            as_int(item.get("live_body_count")),
            as_int(item.get("applied")),
            as_int(item.get("restored")),
            as_float(item.get("pre_live_delta_sq")),
            as_float(item.get("applied_delta_sq")),
            as_float(item.get("restored_delta_sq")),
        ),
    )


def insert_replay_restore(conn, item):
    conn.execute(
        """
        insert or replace into replay_restores(
            run_id, frame, target_replay_frame, restore_source, checkpoint_replay_frame, target_scene_frame, target_solver_hash,
            target_presentation_hash, restored_solver_hash, restored_presentation_hash,
            target_body_count, restored_body_count, contact_count, pipeline_record_count,
            checkpoint_boundary, hash_captured, hash_matched, fallback_attempted, fallback_restored, failure_reason
        )
        values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            item.get("run"),
            as_int(item.get("frame")),
            as_int(item.get("target_replay_frame")),
            item.get("restore_source"),
            as_int(item.get("checkpoint_replay_frame")),
            as_int(item.get("target_scene_frame")),
            str(item.get("target_solver_hash")) if item.get("target_solver_hash") is not None else None,
            str(item.get("target_presentation_hash")) if item.get("target_presentation_hash") is not None else None,
            str(item.get("restored_solver_hash")) if item.get("restored_solver_hash") is not None else None,
            str(item.get("restored_presentation_hash")) if item.get("restored_presentation_hash") is not None else None,
            as_int(item.get("target_body_count")),
            as_int(item.get("restored_body_count")),
            as_int(item.get("contact_count")),
            as_int(item.get("pipeline_record_count")),
            as_int(item.get("checkpoint_boundary")),
            as_int(item.get("hash_captured")),
            as_int(item.get("hash_matched")),
            as_int(item.get("fallback_attempted")),
            as_int(item.get("fallback_restored")),
            item.get("failure_reason"),
        ),
    )


def insert_event(conn, item):
    conn.execute(
        """
        insert or replace into events(
            run_id, event_id, frame, type, severity, body_a, body_b, island_id,
            summary, data_json
        )
        values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            item.get("run"),
            item.get("event_id"),
            as_int(item.get("frame")),
            item.get("type"),
            item.get("severity"),
            as_int(item.get("body_a")),
            as_int(item.get("body_b")),
            as_int(item.get("island_id")),
            item.get("summary"),
            as_json(item.get("data")),
        ),
    )


def insert_end(conn, item):
    run_id = item.get("run") or "run_0001"
    conn.execute(
        """
        insert into runs(run_id, end_frame, end_status)
        values(?, ?, ?)
        on conflict(run_id) do update set
            end_frame=excluded.end_frame,
            end_status=excluded.end_status
        """,
        (run_id, as_int(item.get("frame")), item.get("status")),
    )


def apply_frame_where(where, params, frame_range=None, frame=None, prefix=""):
    col = f"{prefix}frame"
    if frame is not None:
        where.append(f"{col}=?")
        params.append(frame)
        return
    start, end = parse_frame_range(frame_range)
    if start is not None:
        where.append(f"{col}>=?")
        params.append(start)
    if end is not None:
        where.append(f"{col}<=?")
        params.append(end)


def get_runs(conn):
    return rows_to_dicts(conn.execute("select * from runs order by run_id").fetchall())


def resolve_run_id(conn, args):
    if getattr(args, "run", None):
        row = conn.execute("select run_id from runs where run_id=?", (args.run,)).fetchone()
        if row is None:
            raise SystemExit(f"ERROR: run not found: {args.run}")
        return args.run
    row = conn.execute("select run_id from runs order by run_id limit 1").fetchone()
    if row is None:
        raise SystemExit("ERROR: no run rows in trace")
    return row["run_id"]


def decode_event_data(row):
    data_json = row["data_json"] if row else None
    if not data_json:
        return {}
    try:
        return json.loads(data_json)
    except json.JSONDecodeError:
        return {"raw": data_json}


def resolve_body_id(conn, run_id, ref):
    body_id = as_int(ref, None)
    if body_id is not None:
        return body_id
    row = conn.execute(
        "select body_id from bodies where run_id=? and name=? order by frame limit 1",
        (run_id, ref),
    ).fetchone()
    if row is not None:
        return row["body_id"]
    raise SystemExit(f"ERROR: body not found by id or name: {ref}")


def query_summary(conn, cache, args):
    if args.run:
        run_rows = conn.execute("select * from runs where run_id=? order by run_id", (args.run,)).fetchall()
        if not run_rows:
            raise SystemExit(f"ERROR: run not found: {args.run}")
    else:
        run_rows = conn.execute("select * from runs order by run_id").fetchall()

    runs = []
    for run in run_rows:
        run_id = run["run_id"]
        frame_stats = conn.execute(
            """
            select count(*) as frame_count, min(frame) as first_frame, max(frame) as last_frame,
                   min(total_energy) as min_energy, max(total_energy) as max_energy,
                   avg(total_energy) as avg_energy
            from frames where run_id=?
            """,
            (run_id,),
        ).fetchone()
        final_frame = conn.execute(
            "select * from frames where run_id=? order by frame desc limit 1",
            (run_id,),
        ).fetchone()
        max_speed = conn.execute(
            "select frame, max_speed, max_speed_body from frames where run_id=? order by max_speed desc limit 1",
            (run_id,),
        ).fetchone()
        max_omega = conn.execute(
            "select frame, max_omega, max_omega_body from frames where run_id=? order by max_omega desc limit 1",
            (run_id,),
        ).fetchone()
        max_penetration = conn.execute(
            """
            select frame, max_penetration, max_penetration_contact
            from frames where run_id=? order by max_penetration desc limit 1
            """,
            (run_id,),
        ).fetchone()
        top_events = conn.execute(
            """
            select event_id, frame, type, severity, body_a, body_b, island_id, summary
            from events where run_id=?
            order by case severity when 'high' then 0 when 'medium' then 1 when 'low' then 2 else 3 end,
                     frame, event_id
            limit ?
            """,
            (run_id, args.limit or SUMMARY_LIMIT),
        ).fetchall()
        event_counts = conn.execute(
            """
            select type, severity, count(*) as count
            from events where run_id=?
            group by type, severity
            order by count desc, type, severity
            """,
            (run_id,),
        ).fetchall()
        top_bodies = conn.execute(
            """
            select body_id, max(nullif(name, '')) as name, shape,
                   max(speed) as max_speed, max(omega_mag) as max_omega,
                   max(linear_energy + angular_energy) as max_energy
            from bodies where run_id=?
            group by body_id, shape
            order by max_speed desc
            limit ?
            """,
            (run_id, args.limit or SUMMARY_LIMIT),
        ).fetchall()

        runs.append(
            {
                "run": row_to_dict(run),
                "frames": row_to_dict(frame_stats),
                "finalFrame": row_to_dict(final_frame),
                "maxSpeed": row_to_dict(max_speed),
                "maxOmega": row_to_dict(max_omega),
                "maxPenetration": row_to_dict(max_penetration),
                "eventCounts": rows_to_dicts(event_counts),
                "topEvents": rows_to_dicts(top_events),
                "topBodies": rows_to_dicts(top_bodies),
                "relatedQueries": [
                    "events --limit 20",
                    "energy --frames 0:1000",
                    "frame <frame>",
                    "body <body> --frames <start>:<end>",
                ],
            }
        )

    return {"cache": cache, "runs": runs}


def query_events(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    where = ["run_id=?"]
    params = [run_id]
    types = split_csv_filter(args.type)
    if types:
        where.append("type in (%s)" % ",".join("?" for _ in types))
        params.extend(types)
    severities = split_csv_filter(args.severity)
    if severities:
        where.append("severity in (%s)" % ",".join("?" for _ in severities))
        params.extend(severities)
    apply_frame_where(where, params, frame_range=args.frames)
    params.append(args.limit or DEFAULT_LIMIT)
    rows = conn.execute(
        f"""
        select event_id, frame, type, severity, body_a, body_b, island_id, summary, data_json
        from events
        where {' and '.join(where)}
        order by case severity when 'high' then 0 when 'medium' then 1 when 'low' then 2 else 3 end,
                 frame, event_id
        limit ?
        """,
        params,
    ).fetchall()
    events = []
    for row in rows:
        item = row_to_dict(row)
        item["data"] = decode_event_data(row)
        item.pop("data_json", None)
        events.append(item)
    return {
        "cache": cache,
        "run": run_id,
        "events": events,
        "relatedQueries": ["event <id> --window 30", "frame <frame>", "body <body> --frames <start>:<end>"],
    }


def contact_audio_item(row):
    data = decode_event_data(row)
    decision = data.get("decision") or "unknown"
    submitted = bool(as_int(data.get("submitted"), 0))
    return {
        "eventId": row["event_id"],
        "frame": as_int(row["frame"], -1),
        "decision": decision,
        "kind": data.get("kind") or "unknown",
        "submitted": submitted,
        "flashEligible": bool(as_int(data.get("flash_eligible"), 0)),
        "bodyA": row["body_a"],
        "bodyB": row["body_b"],
        "featureId": as_int(data.get("feature_id"), 0),
        "isTerrain": bool(as_int(data.get("is_terrain"), 0)),
        "materialA": as_int(data.get("material_a"), 0),
        "materialB": as_int(data.get("material_b"), 0),
        "normalImpulse": as_float(data.get("normal_impulse"), 0.0),
        "normalClosingSpeed": as_float(data.get("normal_closing_speed"), 0.0),
        "tangentSlipSpeed": as_float(data.get("tangent_slip_speed"), 0.0),
        "impactScore": as_float(data.get("impact_score"), 0.0),
        "gain": as_float(data.get("gain"), 0.0),
        "distance": as_float(data.get("distance"), 0.0),
        "maxDistance": as_float(data.get("max_distance"), 0.0),
        "contactAgeSeconds": as_float(data.get("contact_age_seconds"), 0.0),
        "ongoingContact": bool(as_int(data.get("ongoing_contact"), 0)),
        "impulseSpike": bool(as_int(data.get("impulse_spike"), 0)),
        "soundSet": data.get("sound_set") or "",
        "band": data.get("band") or "",
        "sample": data.get("sample") or "",
    }


def contact_audio_rows(conn, run_id, args, submitted=None):
    # Concept: contact-audio verdicts are ordinary event rows. Keeping the query
    # on the existing event table avoids a schema bump while the runtime still
    # logs one compact verdict packet per candidate.
    where = ["run_id=?", "type='contact_audio'"]
    params = [run_id]
    apply_frame_where(where, params, frame_range=getattr(args, "frames", None), frame=getattr(args, "frame", None))
    body_ref = getattr(args, "body", None)
    if body_ref is not None:
        body_id = resolve_body_id(conn, run_id, body_ref)
        where.append("(body_a=? or body_b=?)")
        params.extend([body_id, body_id])
    rows = conn.execute(
        f"""
        select event_id, frame, severity, body_a, body_b, summary, data_json
        from events
        where {' and '.join(where)}
        order by frame, event_id
        """,
        params,
    ).fetchall()
    reasons = set(split_csv_filter(getattr(args, "reason", None)))
    items = []
    for row in rows:
        item = contact_audio_item(row)
        if reasons and item["decision"] not in reasons:
            continue
        if submitted is not None and item["submitted"] != submitted:
            continue
        items.append(item)
    return items


def contact_audio_frame_item(row):
    data = decode_event_data(row)
    return {
        "eventId": row["event_id"],
        "frame": as_int(row["frame"], -1),
        "factsSeen": as_int(data.get("facts_seen"), 0),
        "patchCandidates": as_int(data.get("patch_candidates"), 0),
        "mergedCandidates": as_int(data.get("merged_candidates"), 0),
        "candidateOverflows": as_int(data.get("candidate_overflows"), 0),
        "burstWindowSkippedCandidates": as_int(data.get("burst_window_skipped_candidates"), 0),
        "budgetRejectedCandidates": as_int(data.get("budget_rejected_candidates"), 0),
        "rejectedByThreshold": as_int(data.get("rejected_by_threshold"), 0),
        "rejectedByCooldown": as_int(data.get("rejected_by_cooldown"), 0),
        "submittedVoices": as_int(data.get("submitted_voices"), 0),
        "droppedVoices": as_int(data.get("dropped_voices"), 0),
    }


def contact_audio_frame_rows(conn, run_id, args):
    # Invariant: aggregate rows are frame-level reducer counters, so body and
    # reason filters intentionally keep them out of scoped verdict queries.
    if getattr(args, "body", None) is not None or getattr(args, "reason", None):
        return []
    where = ["run_id=?", "type='contact_audio_frame'"]
    params = [run_id]
    apply_frame_where(where, params, frame_range=getattr(args, "frames", None), frame=getattr(args, "frame", None))
    rows = conn.execute(
        f"""
        select event_id, frame, severity, body_a, body_b, summary, data_json
        from events
        where {' and '.join(where)}
        order by frame, event_id
        """,
        params,
    ).fetchall()
    return [contact_audio_frame_item(row) for row in rows]


def contact_audio_aggregate_summary(frame_items, limit):
    totals = {
        "factsSeen": 0,
        "patchCandidates": 0,
        "mergedCandidates": 0,
        "candidateOverflows": 0,
        "burstWindowSkippedCandidates": 0,
        "budgetRejectedCandidates": 0,
        "rejectedByThreshold": 0,
        "rejectedByCooldown": 0,
        "submittedVoices": 0,
        "droppedVoices": 0,
    }
    for item in frame_items:
        for key in totals:
            totals[key] += item.get(key, 0)
    bounded_limit = limit or SUMMARY_LIMIT
    hotspots = sorted(
        frame_items,
        key=lambda item: (
            item["submittedVoices"],
            item["factsSeen"],
            item["mergedCandidates"],
            item["budgetRejectedCandidates"] + item["candidateOverflows"],
        ),
        reverse=True,
    )[:bounded_limit]
    return {"frameAggregateTotals": totals, "frameAggregateHotspots": hotspots}


def contact_audio_summary_from_items(items, limit, frame_items=None):
    decision_counts = {}
    kind_counts = {}
    frame_stats = {}
    submitted = 0
    rejected = 0
    for item in items:
        decision = item["decision"]
        kind = item["kind"]
        decision_counts[decision] = decision_counts.get(decision, 0) + 1
        kind_counts[kind] = kind_counts.get(kind, 0) + 1
        if item["submitted"]:
            submitted += 1
        else:
            rejected += 1

        frame = item["frame"]
        bucket = frame_stats.setdefault(
            frame,
            {
                "frame": frame,
                "events": 0,
                "submitted": 0,
                "rejected": 0,
                "flashEligible": 0,
                "decisionCounts": {},
                "kindCounts": {},
                "maxImpulse": 0.0,
                "maxImpactScore": 0.0,
                "maxClosingSpeed": 0.0,
            },
        )
        bucket["events"] += 1
        bucket["submitted"] += 1 if item["submitted"] else 0
        bucket["rejected"] += 0 if item["submitted"] else 1
        bucket["flashEligible"] += 1 if item["flashEligible"] else 0
        bucket["decisionCounts"][decision] = bucket["decisionCounts"].get(decision, 0) + 1
        bucket["kindCounts"][kind] = bucket["kindCounts"].get(kind, 0) + 1
        bucket["maxImpulse"] = max(bucket["maxImpulse"], item["normalImpulse"] or 0.0)
        bucket["maxImpactScore"] = max(bucket["maxImpactScore"], item["impactScore"] or 0.0)
        bucket["maxClosingSpeed"] = max(bucket["maxClosingSpeed"], item["normalClosingSpeed"] or 0.0)

    bounded_limit = limit or SUMMARY_LIMIT
    frame_hotspots = sorted(
        frame_stats.values(),
        key=lambda item: (
            item["submitted"],
            item["rejected"],
            item["events"],
            item["maxImpactScore"],
            item["maxImpulse"],
        ),
        reverse=True,
    )[:bounded_limit]
    top_impacts = sorted(
        items,
        key=lambda item: (item["impactScore"] or 0.0, item["normalImpulse"] or 0.0, item["normalClosingSpeed"] or 0.0),
        reverse=True,
    )[:bounded_limit]
    summary = {
        "eventCount": len(items),
        "submitted": submitted,
        "rejected": rejected,
        "decisionCounts": decision_counts,
        "kindCounts": kind_counts,
        "frameHotspots": frame_hotspots,
        "topImpacts": top_impacts,
    }
    if frame_items:
        summary.update(contact_audio_aggregate_summary(frame_items, limit))
    return summary


def query_contact_audio_summary(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    items = contact_audio_rows(conn, run_id, args)
    frame_items = contact_audio_frame_rows(conn, run_id, args)
    summary = contact_audio_summary_from_items(items, args.limit, frame_items=frame_items)
    return {
        "cache": cache,
        "run": run_id,
        "frames": getattr(args, "frames", None),
        **summary,
        "relatedQueries": [
            "contact-audio-events --frames <start>:<end>",
            "contact-audio-rejections --reason propagated_impulse --limit 40",
            "contact-audio-timeline --window-ms 100",
            "event <eventId> --window 20",
        ],
    }


def query_contact_audio_events(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    submitted_filter = None
    if getattr(args, "submitted", None) == "yes":
        submitted_filter = True
    elif getattr(args, "submitted", None) == "no":
        submitted_filter = False
    items = contact_audio_rows(conn, run_id, args, submitted=submitted_filter)
    limit = args.limit or DEFAULT_LIMIT
    return {
        "cache": cache,
        "run": run_id,
        "events": items[:limit],
        "truncated": len(items) > limit,
        "totalMatched": len(items),
        "relatedQueries": ["contact-audio-summary", "event <eventId> --window 20", "contact-audio-body --body <body>"],
    }


def query_contact_audio_rejections(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    items = contact_audio_rows(conn, run_id, args, submitted=False)
    limit = args.limit or DEFAULT_LIMIT
    return {
        "cache": cache,
        "run": run_id,
        "reason": args.reason,
        "rejections": items[:limit],
        "truncated": len(items) > limit,
        "totalMatched": len(items),
        "relatedQueries": ["contact-audio-summary", "contact-audio-events --submitted no", "event <eventId> --window 20"],
    }


def query_contact_audio_body(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    body_id = resolve_body_id(conn, run_id, args.body)
    items = contact_audio_rows(conn, run_id, args)
    limit = args.limit or DEFAULT_LIMIT
    return {
        "cache": cache,
        "run": run_id,
        "body": body_id,
        "summary": contact_audio_summary_from_items(items, args.limit),
        "events": items[:limit],
        "truncated": len(items) > limit,
        "totalMatched": len(items),
        "relatedQueries": ["body %d --frames <start>:<end>" % body_id, "contact-audio-summary"],
    }


def query_contact_audio_timeline(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    items = contact_audio_rows(conn, run_id, args)
    window_ms = max(1, args.window_ms)
    frames_per_window = max(1, int(round((window_ms / 1000.0) * CONTACT_AUDIO_FIXED_HZ)))
    buckets = {}
    for item in items:
        frame = item["frame"]
        start = (frame // frames_per_window) * frames_per_window
        end = start + frames_per_window - 1
        bucket = buckets.setdefault(
            start,
            {
                "startFrame": start,
                "endFrame": end,
                "events": 0,
                "submitted": 0,
                "rejected": 0,
                "decisionCounts": {},
                "kindCounts": {},
                "maxImpactScore": 0.0,
                "maxImpulse": 0.0,
            },
        )
        decision = item["decision"]
        kind = item["kind"]
        bucket["events"] += 1
        bucket["submitted"] += 1 if item["submitted"] else 0
        bucket["rejected"] += 0 if item["submitted"] else 1
        bucket["decisionCounts"][decision] = bucket["decisionCounts"].get(decision, 0) + 1
        bucket["kindCounts"][kind] = bucket["kindCounts"].get(kind, 0) + 1
        bucket["maxImpactScore"] = max(bucket["maxImpactScore"], item["impactScore"] or 0.0)
        bucket["maxImpulse"] = max(bucket["maxImpulse"], item["normalImpulse"] or 0.0)
    timeline = sorted(
        buckets.values(),
        key=lambda item: (item["submitted"], item["rejected"], item["events"], item["maxImpactScore"]),
        reverse=True,
    )
    limit = args.limit or SUMMARY_LIMIT
    return {
        "cache": cache,
        "run": run_id,
        "windowMs": window_ms,
        "framesPerWindow": frames_per_window,
        "timeline": timeline[:limit],
        "truncated": len(timeline) > limit,
        "totalWindows": len(timeline),
        "relatedQueries": ["contact-audio-events --frames <start>:<end>", "contact-audio-summary"],
    }


def query_audio(conn, cache, args):
    return query_contact_audio_summary(conn, cache, args)


def query_event(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    row = conn.execute(
        "select * from events where run_id=? and event_id=?",
        (run_id, args.event_id),
    ).fetchone()
    if row is None:
        raise SystemExit(f"ERROR: event not found: {args.event_id}")
    event = row_to_dict(row)
    event["data"] = decode_event_data(row)
    event.pop("data_json", None)
    frame = as_int(row["frame"])
    start = max(0, frame - args.window)
    end = frame + args.window
    frame_rows = conn.execute(
        """
        select * from frames where run_id=? and frame>=? and frame<=?
        order by frame
        """,
        (run_id, start, end),
    ).fetchall()
    body_ids = [row["body_a"], row["body_b"]]
    bodies = []
    for body_id in body_ids:
        if body_id is None or body_id < 0:
            continue
        bodies.extend(
            rows_to_dicts(
                conn.execute(
                    """
                    select * from bodies
                    where run_id=? and body_id=? and frame>=? and frame<=?
                    order by frame
                    limit ?
                    """,
                    (run_id, body_id, start, end, args.limit or BODY_SAMPLE_LIMIT),
                ).fetchall()
            )
        )
    contacts = conn.execute(
        """
        select * from contacts
        where run_id=? and frame>=? and frame<=?
          and (body_a=? or body_b=? or body_a=? or body_b=?)
        order by frame, contact_id
        limit ?
        """,
        (
            run_id,
            start,
            end,
            row["body_a"],
            row["body_a"],
            row["body_b"],
            row["body_b"],
            args.limit or DEFAULT_LIMIT,
        ),
    ).fetchall()
    return {
        "cache": cache,
        "run": run_id,
        "event": event,
        "window": {"start": start, "end": end},
        "frames": rows_to_dicts(frame_rows),
        "bodies": bodies,
        "contacts": rows_to_dicts(contacts),
        "relatedQueries": event.get("data", {}).get("followups", []),
    }


def sample_rows(rows, limit):
    if len(rows) <= limit:
        return rows
    if limit <= 1:
        return rows[:1]
    sampled = []
    last_index = len(rows) - 1
    for i in range(limit):
        index = round(i * last_index / (limit - 1))
        sampled.append(rows[index])
    return sampled


def query_body(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    body_id = resolve_body_id(conn, run_id, args.body)
    where = ["run_id=?", "body_id=?"]
    params = [run_id, body_id]
    apply_frame_where(where, params, frame_range=args.frames, frame=args.frame)
    rows = conn.execute(
        f"select * from bodies where {' and '.join(where)} order by frame",
        params,
    ).fetchall()
    limit = args.limit or BODY_SAMPLE_LIMIT
    sampled = sample_rows(rows, limit)
    stats = conn.execute(
        f"""
        select count(*) as row_count, min(frame) as first_frame, max(frame) as last_frame,
               max(speed) as max_speed, max(omega_mag) as max_omega,
               max(linear_energy + angular_energy) as max_energy,
               sum(case when sleeping=1 then 1 else 0 end) as sleeping_rows,
               sum(case when sleep_supported=1 then 1 else 0 end) as supported_rows,
               sum(case when sleep_inhibited=1 then 1 else 0 end) as inhibited_rows
        from bodies where {' and '.join(where)}
        """,
        params,
    ).fetchone()
    events = conn.execute(
        """
        select event_id, frame, type, severity, body_a, body_b, island_id, summary
        from events
        where run_id=? and (body_a=? or body_b=?)
        order by frame, event_id
        limit ?
        """,
        (run_id, body_id, body_id, args.limit or DEFAULT_LIMIT),
    ).fetchall()
    return {
        "cache": cache,
        "run": run_id,
        "body": body_id,
        "stats": row_to_dict(stats),
        "sampled": len(sampled),
        "totalRows": len(rows),
        "timeline": rows_to_dicts(sampled),
        "events": rows_to_dicts(events),
        "relatedQueries": ["contacts --body %s --frame <frame>" % body_id, "frame <frame>", "island <id> --frame <frame>"],
    }


def query_frame(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    frame = as_int(args.frame)
    frame_row = conn.execute("select * from frames where run_id=? and frame=?", (run_id, frame)).fetchone()
    if frame_row is None:
        raise SystemExit(f"ERROR: frame not found: {frame}")
    limit = args.limit or SUMMARY_LIMIT
    top_speed = conn.execute(
        """
        select body_id, name, shape, speed, omega_mag, linear_energy, angular_energy, sleeping,
               sleep_supported, sleep_inhibited, island_id, pos_x, pos_y, pos_z
        from bodies where run_id=? and frame=?
        order by speed desc
        limit ?
        """,
        (run_id, frame, limit),
    ).fetchall()
    top_energy = conn.execute(
        """
        select body_id, name, shape, speed, omega_mag, linear_energy, angular_energy, sleeping,
               sleep_supported, sleep_inhibited, island_id, pos_x, pos_y, pos_z
        from bodies where run_id=? and frame=?
        order by (linear_energy + angular_energy) desc
        limit ?
        """,
        (run_id, frame, limit),
    ).fetchall()
    contacts = conn.execute(
        """
        select * from contacts where run_id=? and frame=?
        order by penetration desc
        limit ?
        """,
        (run_id, frame, limit),
    ).fetchall()
    events = conn.execute(
        """
        select event_id, frame, type, severity, body_a, body_b, island_id, summary
        from events where run_id=? and frame=?
        order by event_id
        limit ?
        """,
        (run_id, frame, limit),
    ).fetchall()
    return {
        "cache": cache,
        "run": run_id,
        "frame": row_to_dict(frame_row),
        "topBodiesBySpeed": rows_to_dicts(top_speed),
        "topBodiesByEnergy": rows_to_dicts(top_energy),
        "contacts": rows_to_dicts(contacts),
        "events": rows_to_dicts(events),
        "relatedQueries": ["body <body> --frames %d:%d" % (max(0, frame - 30), frame + 30), "events --frames %d:%d" % (max(0, frame - 30), frame + 30)],
    }


def query_contacts(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    where = ["run_id=?"]
    params = [run_id]
    apply_frame_where(where, params, frame_range=args.frames, frame=args.frame)
    if args.body:
        body_id = resolve_body_id(conn, run_id, args.body)
        where.append("(body_a=? or body_b=?)")
        params.extend([body_id, body_id])
    if args.type:
        types = split_csv_filter(args.type)
        where.append("contact_type in (%s)" % ",".join("?" for _ in types))
        params.extend(types)
    order_by = {
        "penetration": "penetration desc",
        "impulse": "(coalesce(normal_impulse, 0.0) + coalesce(tangent_impulse, 0.0)) desc",
        "slip": "slip_speed desc",
        "frame": "frame, contact_id",
    }.get(args.top or "frame", "frame, contact_id")
    params.append(args.limit or DEFAULT_LIMIT)
    rows = conn.execute(
        f"select * from contacts where {' and '.join(where)} order by {order_by} limit ?",
        params,
    ).fetchall()
    return {
        "cache": cache,
        "run": run_id,
        "contacts": rows_to_dicts(rows),
        "note": None if rows else "No contact rows are present for this trace yet.",
        "relatedQueries": ["frame <frame>", "body <body> --frames <start>:<end>"],
    }


def query_island(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    frame = args.frame
    if frame is None:
        row = conn.execute("select max(frame) as frame from frames where run_id=?", (run_id,)).fetchone()
        frame = row["frame"] if row else 0
    island_id = as_int(args.island, None) if args.island is not None else None
    island_rows = conn.execute("select count(*) as count from islands where run_id=?", (run_id,)).fetchone()["count"]
    if island_rows:
        where = ["run_id=?", "frame=?"]
        params = [run_id, frame]
        if island_id is not None:
            where.append("island_id=?")
            params.append(island_id)
        islands = conn.execute(
            f"select * from islands where {' and '.join(where)} order by island_id",
            params,
        ).fetchall()
        members = conn.execute(
            """
            select m.island_id, b.body_id, b.name, b.shape, b.speed, b.omega_mag, b.sleeping,
                   b.sleep_supported, b.sleep_inhibited
            from island_members m
            left join bodies b on b.run_id=m.run_id and b.frame=m.frame and b.body_id=m.body_id
            where m.run_id=? and m.frame=? and (? is null or m.island_id=?)
            order by m.island_id, m.body_id
            limit ?
            """,
            (run_id, frame, island_id, island_id, args.limit or DEFAULT_LIMIT),
        ).fetchall()
        note = None
    else:
        where = ["run_id=?", "frame=?"]
        params = [run_id, frame]
        if island_id is not None:
            where.append("island_id=?")
            params.append(island_id)
        islands = conn.execute(
            f"""
            select island_id, count(*) as body_count,
                   sum(case when sleeping=0 then 1 else 0 end) as awake_count,
                   sum(case when sleeping=1 then 1 else 0 end) as sleeping_count,
                   sum(case when sleep_supported=1 then 1 else 0 end) as supported_count,
                   sum(case when sleep_inhibited=1 then 1 else 0 end) as inhibited_count,
                   max(speed) as max_speed, max(omega_mag) as max_omega,
                   sum(linear_energy + angular_energy) as total_energy
            from bodies
            where {' and '.join(where)}
            group by island_id
            order by island_id
            """,
            params,
        ).fetchall()
        members = conn.execute(
            f"""
            select island_id, body_id, name, shape, speed, omega_mag, sleeping,
                   sleep_supported, sleep_inhibited
            from bodies
            where {' and '.join(where)}
            order by island_id, body_id
            limit ?
            """,
            params + [args.limit or DEFAULT_LIMIT],
        ).fetchall()
        note = "Synthesized from body rows; dedicated island rows are not present for this trace yet."
    return {
        "cache": cache,
        "run": run_id,
        "frame": frame,
        "islands": rows_to_dicts(islands),
        "members": rows_to_dicts(members),
        "note": note,
        "relatedQueries": ["body <body> --frames %d:%d" % (max(0, frame - 30), frame + 30), "events --frames %d:%d" % (max(0, frame - 30), frame + 30)],
    }


def query_stacks(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    where = ["run_id=?"]
    params = [run_id]
    apply_frame_where(where, params, frame_range=args.frames)
    edges = conn.execute(
        f"select * from support_edges where {' and '.join(where)} order by frame, supporter, supported limit ?",
        params + [args.limit or DEFAULT_LIMIT],
    ).fetchall()
    unsupported_sleepers = conn.execute(
        f"""
        select frame, body_id, name, shape, island_id, sleep_counter
        from bodies
        where {' and '.join(where)} and sleeping=1 and sleep_supported=0
        order by frame, body_id
        limit ?
        """,
        params + [args.limit or DEFAULT_LIMIT],
    ).fetchall()
    drifting = conn.execute(
        f"""
        select body_id, max(nullif(name, '')) as name, max(speed) as max_speed,
               max(omega_mag) as max_omega, min(frame) as first_frame, max(frame) as last_frame
        from bodies
        where {' and '.join(where)} and sleep_supported=1
        group by body_id
        having max(speed) > 0.05 or max(omega_mag) > 0.05
        order by max_speed desc
        limit ?
        """,
        params + [args.limit or DEFAULT_LIMIT],
    ).fetchall()
    return {
        "cache": cache,
        "run": run_id,
        "supportEdges": rows_to_dicts(edges),
        "unsupportedSleepers": rows_to_dicts(unsupported_sleepers),
        "supportedMovingBodies": rows_to_dicts(drifting),
        "note": None if edges else "Support-edge stack rows are not present for this trace yet; body sleep/support flags were used.",
        "relatedQueries": ["events --type unsupported_sleep,stack_drift", "island <id> --frame <frame>", "body <body> --frames <start>:<end>"],
    }


def query_energy(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    where = ["run_id=?"]
    params = [run_id]
    apply_frame_where(where, params, frame_range=args.frames)
    frames = conn.execute(
        f"""
        select frame, time_seconds, total_energy, linear_energy, angular_energy,
               max_speed, max_speed_body, max_omega, max_omega_body
        from frames
        where {' and '.join(where)}
        order by frame
        """,
        params,
    ).fetchall()
    sampled = sample_rows(frames, args.limit or BODY_SAMPLE_LIMIT)
    deltas = []
    prev = None
    for row in frames:
        if prev is not None:
            deltas.append(
                {
                    "frame": row["frame"],
                    "previousFrame": prev["frame"],
                    "deltaEnergy": (row["total_energy"] or 0.0) - (prev["total_energy"] or 0.0),
                    "totalEnergy": row["total_energy"],
                    "maxSpeedBody": row["max_speed_body"],
                    "maxOmegaBody": row["max_omega_body"],
                }
            )
        prev = row
    deltas.sort(key=lambda item: item["deltaEnergy"], reverse=True)
    max_frame = None
    if frames:
        max_frame = max(frames, key=lambda row: row["total_energy"] if row["total_energy"] is not None else -math.inf)["frame"]
    contributors = []
    if max_frame is not None:
        contributors = rows_to_dicts(
            conn.execute(
                """
                select body_id, name, shape, speed, omega_mag, linear_energy, angular_energy,
                       (linear_energy + angular_energy) as total_body_energy
                from bodies
                where run_id=? and frame=?
                order by total_body_energy desc
                limit ?
                """,
                (run_id, max_frame, args.limit or SUMMARY_LIMIT),
            ).fetchall()
        )
    return {
        "cache": cache,
        "run": run_id,
        "sampledFrames": rows_to_dicts(sampled),
        "largestPositiveDeltas": deltas[: args.limit or SUMMARY_LIMIT],
        "topContributorsAtMaxEnergy": contributors,
        "relatedQueries": ["events --type energy_spike", "frame <frame>", "body <body> --frames <start>:<end>"],
    }


def query_rolling(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    where = ["run_id=?"]
    params = [run_id]
    apply_frame_where(where, params, frame_range=args.frames)
    contact_rows = conn.execute(
        f"""
        select * from contacts
        where {' and '.join(where)} and (rolling_residual is not null or slip_speed is not null)
        order by coalesce(rolling_residual, 0.0) desc, coalesce(slip_speed, 0.0) desc
        limit ?
        """,
        params + [args.limit or DEFAULT_LIMIT],
    ).fetchall()
    high_spin = conn.execute(
        f"""
        select body_id, max(nullif(name, '')) as name, shape, max(speed) as max_speed,
               max(omega_mag) as max_omega, min(frame) as first_frame, max(frame) as last_frame
        from bodies
        where {' and '.join(where)}
        group by body_id, shape
        order by max_omega desc
        limit ?
        """,
        params + [args.limit or SUMMARY_LIMIT],
    ).fetchall()
    return {
        "cache": cache,
        "run": run_id,
        "rollingContacts": rows_to_dicts(contact_rows),
        "highestSpinBodies": rows_to_dicts(high_spin),
        "note": None if contact_rows else "Contact slip/rolling residual rows are not present for this trace yet; highest-spin body rows are shown instead.",
        "relatedQueries": ["events --type rolling_slip,friction_saturation", "contacts --top slip", "body <body> --frames <start>:<end>"],
    }


def query_broadphase(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    where = ["run_id=?"]
    params = [run_id]
    apply_frame_where(where, params, frame_range=args.frames)
    columns = "*" if args.attribution else (
        "run_id, frame, candidate_pairs, contact_pairs, rejected_pairs, "
        "active_cells, max_cell_occupancy, collision_cell_count"
    )
    rows = conn.execute(
        f"""
        select {columns} from broadphase
        where {' and '.join(where)}
        order by candidate_pairs desc
        limit ?
        """,
        params + [args.limit or DEFAULT_LIMIT],
    ).fetchall()
    fallback = conn.execute(
        f"""
        select frame, body_count, contact_count, island_count
        from frames
        where {' and '.join(where)}
        order by contact_count desc, body_count desc
        limit ?
        """,
        params + [args.limit or DEFAULT_LIMIT],
    ).fetchall()
    return {
        "cache": cache,
        "run": run_id,
        "broadphase": rows_to_dicts(rows),
        "frameFallback": rows_to_dicts(fallback),
        "note": None if rows else "Dedicated broadphase rows are not present for this trace yet; frame counts are shown instead.",
        "relatedQueries": ["events --type broadphase_spike", "frame <frame>"],
    }


def query_solver(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    where = ["run_id=?"]
    params = [run_id]
    apply_frame_where(where, params, frame_range=args.frames)

    rows = conn.execute(
        f"""
        select frame, row_count, cache_previous_rows, cache_hits, cache_misses,
               warm_started_rows, position_correction_rows, position_correction_total,
               position_correction_max, solver_iterations
        from solver_stats
        where {' and '.join(where)}
        order by frame
        """,
        params,
    ).fetchall()
    stats_row = conn.execute(
        f"""
        select count(*) as sample_count, min(frame) as first_frame, max(frame) as last_frame,
               max(row_count) as max_contact_rows, avg(row_count) as avg_contact_rows,
               sum(row_count) as total_contact_rows,
               sum(cache_previous_rows) as cache_previous_rows,
               sum(cache_hits) as cache_hits, sum(cache_misses) as cache_misses,
               sum(warm_started_rows) as warm_started_rows,
               sum(position_correction_rows) as position_correction_rows,
               sum(position_correction_total) as position_correction_total,
               max(position_correction_max) as position_correction_max,
               max(solver_iterations) as max_solver_iterations,
               avg(solver_iterations) as avg_solver_iterations
        from solver_stats
        where {' and '.join(where)}
        """,
        params,
    ).fetchone()
    stats = row_to_dict(stats_row)
    if stats is None:
        stats = {}
    cache_hits = stats.get("cache_hits") or 0
    cache_misses = stats.get("cache_misses") or 0
    total_lookups = cache_hits + cache_misses
    total_contact_rows = stats.get("total_contact_rows") or 0
    correction_rows = stats.get("position_correction_rows") or 0
    warm_started_rows = stats.get("warm_started_rows") or 0
    stats["cache_hit_rate"] = (cache_hits / total_lookups) if total_lookups else None
    stats["warm_start_rate"] = (warm_started_rows / total_contact_rows) if total_contact_rows else None
    stats["position_correction_row_rate"] = (correction_rows / total_contact_rows) if total_contact_rows else None

    return {
        "cache": cache,
        "run": run_id,
        "stats": stats,
        "timeline": rows_to_dicts(sample_rows(rows, args.limit or DEFAULT_LIMIT)),
        "note": None if rows else "Dedicated solver_stats rows are not present for this trace yet.",
        "relatedQueries": ["contacts --top impulse", "contacts --top penetration", "frame <frame>"],
    }


def query_pipeline(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    where = ["run_id=?"]
    params = [run_id]
    apply_frame_where(where, params, frame_range=args.frames, frame=args.frame)

    rows = conn.execute(
        f"""
        select frame, record_count, stage_counts_json
        from pipeline_stages
        where {' and '.join(where)}
        order by frame
        """,
        params,
    ).fetchall()

    aggregate = {}
    total_records = 0
    timeline = []
    for row in rows:
        try:
            stage_counts = json.loads(row["stage_counts_json"] or "{}")
        except json.JSONDecodeError:
            stage_counts = {}
        total_records += row["record_count"] or 0
        for stage, count in stage_counts.items():
            aggregate[stage] = aggregate.get(stage, 0) + (count or 0)
        timeline.append(
            {
                "frame": row["frame"],
                "record_count": row["record_count"],
                "stages": stage_counts,
            }
        )

    return {
        "cache": cache,
        "run": run_id,
        "stats": {
            "sample_count": len(rows),
            "total_records": total_records,
            "stage_totals": dict(sorted(aggregate.items())),
        },
        "timeline": sample_rows(timeline, args.limit or DEFAULT_LIMIT),
        "note": None if rows else "Dedicated pipeline_stages rows are not present for this trace yet.",
        "relatedQueries": ["solver --frames <start>:<end>", "frame <frame>", "events --frames <start>:<end>"],
    }


def query_water(conn, cache, args):
    return {
        "cache": cache,
        "run": resolve_run_id(conn, args),
        "water": [],
        "note": "Water diagnostics are not instrumented in this trace schema yet.",
        "relatedQueries": ["summary", "events --type water_float_sleep"],
    }


def vector_distance_sq(a, b):
    if a is None or b is None:
        return None
    components = []
    for key in ("x", "y", "z"):
        av = a.get(key)
        bv = b.get(key)
        if av is None or bv is None:
            return None
        components.append(av - bv)
    return sum(value * value for value in components)


def trace_body_at(conn, run_id, frame, model_index):
    if frame is None or model_index is None:
        return None
    row = conn.execute(
        """
        select body_id, name, pos_x, pos_y, pos_z, speed, sleeping
        from bodies
        where run_id=? and frame=? and body_id=?
        """,
        (run_id, frame, model_index),
    ).fetchone()
    if row is None:
        return None
    return {
        "body_id": row["body_id"],
        "name": row["name"],
        "pos": {"x": row["pos_x"], "y": row["pos_y"], "z": row["pos_z"]},
        "speed": row["speed"],
        "sleeping": row["sleeping"],
    }


def query_replay(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    rows = conn.execute(
        """
        select * from replay_scrubs
        where run_id=?
        order by frame
        limit ?
        """,
        (run_id, args.limit or DEFAULT_LIMIT),
    ).fetchall()
    scrubs = []
    for row in rows:
        selected_trace = trace_body_at(conn, run_id, row["selected_scene_frame"], row["model_index"])
        live_trace = trace_body_at(conn, run_id, row["live_scene_frame"], row["model_index"])
        selected_pos = {"x": row["selected_x"], "y": row["selected_y"], "z": row["selected_z"]}
        live_pos = {"x": row["live_x"], "y": row["live_y"], "z": row["live_z"]}
        selected_trace_delta_sq = vector_distance_sq(selected_pos, selected_trace["pos"]) if selected_trace else None
        live_trace_delta_sq = vector_distance_sq(live_pos, live_trace["pos"]) if live_trace else None

        checks = {
            "olderSample": row["selected_replay_frame"] is not None
            and row["live_replay_frame"] is not None
            and row["selected_replay_frame"] < row["live_replay_frame"],
            "hashChanged": row["selected_state_hash"] is not None
            and row["live_state_hash"] is not None
            and row["selected_state_hash"] != row["live_state_hash"],
            "movedBody": row["distance_sq"] is not None and row["distance_sq"] >= (args.min_distance_sq or 0.0001),
            "appliedAndRestored": row["applied"] == 1
            and row["restored"] == 1
            and row["applied_delta_sq"] is not None
            and row["applied_delta_sq"] <= args.trace_tolerance_sq
            and row["restored_delta_sq"] is not None
            and row["restored_delta_sq"] <= args.trace_tolerance_sq
            and row["pre_live_delta_sq"] is not None
            and row["pre_live_delta_sq"] <= args.trace_tolerance_sq,
            "selectedTraceMatches": selected_trace_delta_sq is not None and selected_trace_delta_sq <= args.trace_tolerance_sq,
            "liveTraceMatches": live_trace_delta_sq is not None and live_trace_delta_sq <= args.trace_tolerance_sq,
        }

        scrub = row_to_dict(row)
        scrub["selectedPos"] = selected_pos
        scrub["livePos"] = live_pos
        scrub["selectedTraceBody"] = selected_trace
        scrub["liveTraceBody"] = live_trace
        scrub["selectedTraceDeltaSq"] = selected_trace_delta_sq
        scrub["liveTraceDeltaSq"] = live_trace_delta_sq
        scrub["checks"] = checks
        scrub["passed"] = all(checks.values())
        scrubs.append(scrub)

    return {
        "cache": cache,
        "run": run_id,
        "scrubs": scrubs,
        "passed": bool(scrubs) and all(item["passed"] for item in scrubs),
        "note": None if rows else "No replay scrub probe rows are present for this trace.",
        "relatedQueries": ["body <model_index> --frames <selected>:<live>", "frame <selected_replay_frame>", "frame <live_replay_frame>"],
    }


def query_restore(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    rows = conn.execute(
        """
        select * from replay_restores
        where run_id=?
        order by frame
        limit ?
        """,
        (run_id, args.limit or DEFAULT_LIMIT),
    ).fetchall()
    restores = []
    for row in rows:
        checks = {
            "hashCaptured": row["hash_captured"] == 1,
            "hashMatched": row["hash_matched"] == 1
            and row["target_solver_hash"] is not None
            and row["restored_solver_hash"] is not None
            and row["target_solver_hash"] == row["restored_solver_hash"],
            "bodyCountMatched": row["target_body_count"] is not None
            and row["restored_body_count"] is not None
            and row["target_body_count"] == row["restored_body_count"],
            "noFallbackNeeded": row["fallback_attempted"] == 0 and row["fallback_restored"] == 0,
        }
        restore = row_to_dict(row)
        restore["checks"] = checks
        restore["passed"] = all(checks.values())
        restore["failed"] = not restore["passed"]
        restores.append(restore)

    return {
        "cache": cache,
        "run": run_id,
        "restores": restores,
        "passed": bool(restores) and all(item["passed"] for item in restores),
        "note": None if rows else "No replay restore probe rows are present for this trace.",
        "relatedQueries": ["frame <target_scene_frame>", "replay --limit 8"],
    }


def load_questions():
    if not QUESTIONS_PATH.exists():
        return {}
    with QUESTIONS_PATH.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def query_questions(conn, cache, args):
    questions = load_questions()
    trace = cache["trace"]
    if args.name is None:
        return {
            "cache": cache,
            "questionsFile": str(QUESTIONS_PATH),
            "questions": [
                {
                    "name": name,
                    "description": item.get("description"),
                    "commandCount": len(item.get("commands", [])),
                }
                for name, item in sorted(questions.items())
            ],
            "usage": "tools\\physics_query.bat <trace.ndjson> questions <name>",
        }
    if args.name not in questions:
        raise SystemExit(f"ERROR: unknown question: {args.name}")
    item = questions[args.name]
    commands = item.get("commands", [])
    followups = item.get("followups", [])
    return {
        "cache": cache,
        "questionsFile": str(QUESTIONS_PATH),
        "question": args.name,
        "description": item.get("description"),
        "commands": commands,
        "followups": followups,
        "batCommands": [f'tools\\physics_query.bat "{trace}" {command}' for command in commands],
        "batFollowups": [f'tools\\physics_query.bat "{trace}" {command}' for command in followups],
    }


def query_compare(conn, cache, args):
    run_id = resolve_run_id(conn, args)
    other_conn, other_cache = ensure_db(args.other_trace)
    try:
        other_run_id = resolve_run_id(other_conn, args)
        this_summary = one_run_compare_summary(conn, run_id)
        other_summary = one_run_compare_summary(other_conn, other_run_id)
        first_diff = first_frame_difference(conn, run_id, other_conn, other_run_id, args.frames)
        return {
            "cache": cache,
            "otherCache": other_cache,
            "run": run_id,
            "otherRun": other_run_id,
            "summary": {"base": this_summary, "other": other_summary},
            "firstDifferentFrame": first_diff,
            "eventDiff": compare_event_counts(conn, run_id, other_conn, other_run_id),
            "relatedQueries": ["frame <firstDifferentFrame>", "events --frames <start>:<end>", "energy --frames <start>:<end>"],
        }
    finally:
        other_conn.close()


def one_run_compare_summary(conn, run_id):
    return row_to_dict(
        conn.execute(
            """
            select count(*) as frame_count, min(frame) as first_frame, max(frame) as last_frame,
                   min(total_energy) as min_energy, max(total_energy) as max_energy,
                   max(max_speed) as max_speed, max(max_omega) as max_omega,
                   max(max_penetration) as max_penetration
            from frames where run_id=?
            """,
            (run_id,),
        ).fetchone()
    )


def first_frame_difference(conn_a, run_a, conn_b, run_b, frame_range):
    start, end = parse_frame_range(frame_range)
    frames_a = conn_a.execute(
        "select * from frames where run_id=? order by frame",
        (run_a,),
    ).fetchall()
    map_b = {
        row["frame"]: row
        for row in conn_b.execute("select * from frames where run_id=? order by frame", (run_b,)).fetchall()
    }
    for frame_a in frames_a:
        frame = frame_a["frame"]
        if start is not None and frame < start:
            continue
        if end is not None and frame > end:
            continue
        frame_b = map_b.get(frame)
        if frame_b is None:
            return {"frame": frame, "reason": "missing_in_other"}
        keys = ["body_count", "contact_count", "island_count", "sleeping_count", "total_energy", "max_speed", "max_omega", "max_penetration"]
        for key in keys:
            a_val = frame_a[key]
            b_val = frame_b[key]
            if isinstance(a_val, float) or isinstance(b_val, float):
                if abs((a_val or 0.0) - (b_val or 0.0)) > 1.0e-5:
                    return {"frame": frame, "field": key, "base": a_val, "other": b_val}
            elif a_val != b_val:
                return {"frame": frame, "field": key, "base": a_val, "other": b_val}
    return None


def compare_event_counts(conn_a, run_a, conn_b, run_b):
    rows_a = conn_a.execute(
        "select type, severity, count(*) as count from events where run_id=? group by type, severity",
        (run_a,),
    ).fetchall()
    rows_b = conn_b.execute(
        "select type, severity, count(*) as count from events where run_id=? group by type, severity",
        (run_b,),
    ).fetchall()
    counts = {}
    for row in rows_a:
        counts[(row["type"], row["severity"])] = {"base": row["count"], "other": 0}
    for row in rows_b:
        counts.setdefault((row["type"], row["severity"]), {"base": 0, "other": 0})["other"] = row["count"]
    return [
        {"type": key[0], "severity": key[1], "base": value["base"], "other": value["other"]}
        for key, value in sorted(counts.items())
        if value["base"] != value["other"]
    ]


def query_sql(conn, cache, args):
    statement = args.statement.strip()
    lowered = statement.lower()
    allowed = lowered.startswith("select ") or lowered.startswith("with ") or lowered.startswith("pragma ")
    forbidden = ["insert ", "update ", "delete ", "drop ", "alter ", "create ", "replace ", "vacuum", "attach ", "detach "]
    if not allowed or any(token in lowered for token in forbidden):
        raise SystemExit("ERROR: sql command only accepts read-only SELECT/WITH/PRAGMA statements")
    conn.execute("pragma query_only=on")
    cur = conn.execute(statement)
    rows = cur.fetchmany((args.limit or 100) + 1)
    truncated = len(rows) > (args.limit or 100)
    rows = rows[: args.limit or 100]
    return {
        "cache": cache,
        "columns": [desc[0] for desc in cur.description] if cur.description else [],
        "rows": rows_to_dicts(rows),
        "truncated": truncated,
    }


def add_common(parser):
    parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON output.")
    parser.add_argument("--limit", type=int, default=None, help="Maximum rows for bounded lists.")
    parser.add_argument("--run", default=None, help="Run id to query when a trace contains multiple runs.")


def build_parser():
    parser = argparse.ArgumentParser(description="Query SkullbonezCore physics diagnostic traces.")
    parser.add_argument("trace", help="Path to *.physicsdiag.ndjson")
    sub = parser.add_subparsers(dest="command", required=True)

    summary = sub.add_parser("summary", help="Compact run overview and top anomalies.")
    add_common(summary)
    summary.add_argument("--top", default=None, help="Reserved shorthand, e.g. --top bodies.")
    summary.set_defaults(func=query_summary)

    events = sub.add_parser("events", help="List anomaly/transition events.")
    add_common(events)
    events.add_argument("--type", default=None, help="Comma-separated event types.")
    events.add_argument("--severity", default=None, help="Comma-separated severities.")
    events.add_argument("--frames", default=None, help="Frame range A:B.")
    events.set_defaults(func=query_events)

    audio = sub.add_parser("audio", help="Summarize contact audio decision events.")
    add_common(audio)
    audio.add_argument("--frames", default=None, help="Frame range A:B.")
    audio.set_defaults(func=query_audio)

    audio_summary = sub.add_parser("contact-audio-summary", help="Summarize contact-audio decisions.")
    add_common(audio_summary)
    audio_summary.add_argument("--frames", default=None, help="Frame range A:B.")
    audio_summary.add_argument("--frame", type=int, default=None, help="Single frame.")
    audio_summary.add_argument("--body", default=None, help="Body id or name.")
    audio_summary.add_argument("--reason", default=None, help="Comma-separated decision/rejection reasons.")
    audio_summary.set_defaults(func=query_contact_audio_summary)

    audio_events = sub.add_parser("contact-audio-events", help="List bounded contact-audio decisions.")
    add_common(audio_events)
    audio_events.add_argument("--frames", default=None, help="Frame range A:B.")
    audio_events.add_argument("--frame", type=int, default=None, help="Single frame.")
    audio_events.add_argument("--body", default=None, help="Body id or name.")
    audio_events.add_argument("--reason", default=None, help="Comma-separated decision/rejection reasons.")
    audio_events.add_argument("--submitted", choices=["any", "yes", "no"], default="any")
    audio_events.set_defaults(func=query_contact_audio_events)

    audio_rejections = sub.add_parser("contact-audio-rejections", help="List rejected contact-audio decisions.")
    add_common(audio_rejections)
    audio_rejections.add_argument("--frames", default=None, help="Frame range A:B.")
    audio_rejections.add_argument("--frame", type=int, default=None, help="Single frame.")
    audio_rejections.add_argument("--body", default=None, help="Body id or name.")
    audio_rejections.add_argument("--reason", default=None, help="Comma-separated rejection reasons.")
    audio_rejections.set_defaults(func=query_contact_audio_rejections)

    audio_body = sub.add_parser("contact-audio-body", help="Contact-audio decisions for one body.")
    add_common(audio_body)
    audio_body.add_argument("--body", required=True, help="Body id or name.")
    audio_body.add_argument("--frames", default=None, help="Frame range A:B.")
    audio_body.add_argument("--frame", type=int, default=None, help="Single frame.")
    audio_body.add_argument("--reason", default=None, help="Comma-separated decision/rejection reasons.")
    audio_body.set_defaults(func=query_contact_audio_body)

    audio_timeline = sub.add_parser("contact-audio-timeline", help="Contact-audio decision buckets over time.")
    add_common(audio_timeline)
    audio_timeline.add_argument("--frames", default=None, help="Frame range A:B.")
    audio_timeline.add_argument("--body", default=None, help="Body id or name.")
    audio_timeline.add_argument("--reason", default=None, help="Comma-separated decision/rejection reasons.")
    audio_timeline.add_argument("--window-ms", type=int, default=100)
    audio_timeline.set_defaults(func=query_contact_audio_timeline)

    event = sub.add_parser("event", help="Focused event context.")
    add_common(event)
    event.add_argument("event_id")
    event.add_argument("--window", type=int, default=30)
    event.set_defaults(func=query_event)

    body = sub.add_parser("body", help="Body state timeline.")
    add_common(body)
    body.add_argument("body", help="Body id or name.")
    body.add_argument("--frames", default=None, help="Frame range A:B.")
    body.add_argument("--frame", type=int, default=None, help="Single frame.")
    body.set_defaults(func=query_body)

    frame = sub.add_parser("frame", help="Frame aggregate plus top bodies/contacts.")
    add_common(frame)
    frame.add_argument("frame", type=int)
    frame.set_defaults(func=query_frame)

    contacts = sub.add_parser("contacts", help="Contact rows by frame/body/type.")
    add_common(contacts)
    contacts.add_argument("--frame", type=int, default=None)
    contacts.add_argument("--frames", default=None)
    contacts.add_argument("--body", default=None)
    contacts.add_argument("--type", default=None)
    contacts.add_argument("--top", choices=["penetration", "impulse", "slip", "frame"], default="frame")
    contacts.set_defaults(func=query_contacts)

    island = sub.add_parser("island", help="Island summary and membership.")
    add_common(island)
    island.add_argument("island", nargs="?", default=None)
    island.add_argument("--frame", type=int, default=None)
    island.set_defaults(func=query_island)

    stacks = sub.add_parser("stacks", help="Stack/support health.")
    add_common(stacks)
    stacks.add_argument("--frames", default=None)
    stacks.set_defaults(func=query_stacks)

    energy = sub.add_parser("energy", help="Energy trend, spikes, and contributors.")
    add_common(energy)
    energy.add_argument("--frames", default=None)
    energy.set_defaults(func=query_energy)

    rolling = sub.add_parser("rolling", help="Rolling/slip/spin diagnostics.")
    add_common(rolling)
    rolling.add_argument("--frames", default=None)
    rolling.set_defaults(func=query_rolling)

    broadphase = sub.add_parser("broadphase", help="Broadphase pair/grid diagnostics.")
    add_common(broadphase)
    broadphase.add_argument("--frames", default=None)
    broadphase.add_argument(
        "--attribution",
        action="store_true",
        help="Include allocation-free grid insertion and pair-generation counters.",
    )
    broadphase.set_defaults(func=query_broadphase)

    solver = sub.add_parser("solver", help="Persistent contact solver cache/projection diagnostics.")
    add_common(solver)
    solver.add_argument("--frames", default=None)
    solver.set_defaults(func=query_solver)

    pipeline = sub.add_parser("pipeline", help="Catto pipeline stage counts by frame.")
    add_common(pipeline)
    pipeline.add_argument("--frames", default=None)
    pipeline.add_argument("--frame", type=int, default=None)
    pipeline.set_defaults(func=query_pipeline)

    water = sub.add_parser("water", help="Water/buoyancy diagnostics.")
    add_common(water)
    water.add_argument("--frames", default=None)
    water.set_defaults(func=query_water)

    replay = sub.add_parser("replay", help="Replay scrub probe diagnostics.")
    add_common(replay)
    replay.add_argument("--min-distance-sq", type=float, default=0.0001)
    replay.add_argument("--trace-tolerance-sq", type=float, default=0.000001)
    replay.set_defaults(func=query_replay)

    restore = sub.add_parser("restore", help="Replay restore hash probe diagnostics.")
    add_common(restore)
    restore.set_defaults(func=query_restore)

    questions = sub.add_parser("questions", help="List or expand pre-baked question query packs.")
    add_common(questions)
    questions.add_argument("name", nargs="?", default=None)
    questions.set_defaults(func=query_questions)

    compare = sub.add_parser("compare", help="Compare two diagnostic traces.")
    add_common(compare)
    compare.add_argument("other_trace")
    compare.add_argument("--frames", default=None)
    compare.set_defaults(func=query_compare)

    sql = sub.add_parser("sql", help="Read-only SQL escape hatch.")
    add_common(sql)
    sql.add_argument("statement")
    sql.set_defaults(func=query_sql)

    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    conn, cache = ensure_db(args.trace)
    try:
        payload = args.func(conn, cache, args)
        json_response(payload, pretty=args.pretty)
    finally:
        conn.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
