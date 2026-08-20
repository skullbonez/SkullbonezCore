#!/usr/bin/env python3
"""
File: tools/analyze_at_rest_stability.py
Purpose:
  Evaluate bounded semantic at-rest stability from a SkullScope cache.

Summary:
  The analyzer reads indexed body/contact values through the existing
  physics_query cache, derives the RS0 per-ball measures over each body's
  supported quiet run, retains boxes as controls, and publishes a compact JSON
  ruling without using a golden hash.

Glossary:
  Material impact: Contact whose pre-solve closing speed reaches the existing
    2 m/s restitution boundary.
  Significant reversal: Horizontal velocity sign change outside the Physics
    0.5 m/s sleep-speed deadband.
  Post-impact quiet run: The suffix beginning when the body first has support
    and a positive quiet counter after its final material impact, through the
    terminal sleep transition.
  Solver-active contact: Contact carrying solved normal or tangent impulse; a
    speculative row with no impulse has no authority over the slip-quality
    ruling.
  Final sleep: The first frame of the terminal uninterrupted sleeping suffix.

Invariants:
  - Analysis never changes the trace, scene, SkullScope cache schema, or Physics
    policy; the shared physics_query helper may rebuild its generic SQLite cache
    when stale.
  - Completion and motion quality are separate: sleeping cannot hide excessive
    solver-active slip, vertical motion, reversals, or a reset during the
    post-impact quiet run. Earlier resets remain valid responses to later
    motion or support loss.
  - Output contains summaries and first witnesses only; no raw timeline or
    unbounded contact packet is printed.

Related:
  - tools/physics_query.py
  - tools/check_at_rest_stability_analyzer.py
"""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import sys
from typing import Any, Iterable

import physics_query


BALL_NAMES = ("ball_a", "ball_b", "ball_c")
BOX_NAMES = ("box_a", "box_b", "box_c")
SEMANTIC_SCHEMA_VERSION = 3

# Invariant: RS0 recorded these control maxima before any ball-specific repair.
# A later task may ratify new control envelopes, but RS1 must not silently let
# the boxes become more energetic while declaring the ball experiment fixed.
BOX_CONTROL_MAXIMA = {
    "box_a": {"speed": 21.091541, "omega": 3.419009},
    "box_b": {"speed": 26.6506, "omega": 5.031497},
    "box_c": {"speed": 31.146898, "omega": 1.975156},
}


@dataclass(frozen=True)
class StabilityThresholds:
    material_impact_speed: float = 2.0
    maximum_separation_bias: float = 2.0
    tail_audit_frame: int = 7200
    completion_deadline_frame: int = 14400
    maximum_sleep_latency_frames: int = 1200
    maximum_vertical_speed: float = 0.5
    maximum_slip_speed: float = 0.5
    maximum_slip_radius_fraction: float = 0.25
    reversal_deadband: float = 0.5
    maximum_reversals_per_axis: int = 1
    maximum_counter_resets_after_last_impact: int = 0
    maximum_wakes_after_tail: int = 0
    box_control_deadline_frame: int = 7200


DEFAULT_THRESHOLDS = StabilityThresholds()


def _row_value(row: Any, key: str, default: Any = None) -> Any:
    if isinstance(row, dict):
        return row.get(key, default)
    try:
        value = row[key]
    except (IndexError, KeyError, TypeError):
        return default
    return default if value is None else value


def _terminal_sleep_start(frames: list[Any]) -> int | None:
    if not frames or not bool(_row_value(frames[-1], "sleeping", 0)):
        return None

    index = len(frames) - 1
    while index > 0 and bool(_row_value(frames[index - 1], "sleeping", 0)):
        index -= 1
    return int(_row_value(frames[index], "frame"))


def _timeline_is_contiguous(frames: list[Any]) -> bool:
    if not frames:
        return False
    first = int(_row_value(frames[0], "frame", -1))
    return all(int(_row_value(row, "frame", -1)) == first + index for index, row in enumerate(frames))


def _significant_reversals(frames: Iterable[Any], axis: str, thresholds: StabilityThresholds) -> int:
    previous_sign = 0
    reversals = 0

    for frame in frames:
        value = float(_row_value(frame, axis, 0.0))
        if value > thresholds.reversal_deadband:
            sign = 1
        elif value < -thresholds.reversal_deadband:
            sign = -1
        else:
            continue

        if previous_sign != 0 and sign != previous_sign:
            reversals += 1
        previous_sign = sign

    return reversals


def _first_resting_reimpact(contacts: list[Any], thresholds: StabilityThresholds) -> dict[str, Any] | None:
    ordered = sorted(contacts, key=lambda row: (int(_row_value(row, "frame", 0)), str(_row_value(row, "contact_id", ""))))

    by_frame: dict[int, list[Any]] = {}
    for contact in ordered:
        by_frame.setdefault(int(_row_value(contact, "frame", 0)), []).append(contact)

    for contact in ordered:
        if int(_row_value(contact, "body_b", -2)) != -1:
            continue

        closing = float(_row_value(contact, "pre_solve_closing_speed", 0.0))
        bias = float(_row_value(contact, "separation_bias", 0.0))
        if closing >= thresholds.material_impact_speed or bias + 1.0e-6 < thresholds.maximum_separation_bias:
            continue

        contact_id = str(_row_value(contact, "contact_id", ""))
        start_frame = int(_row_value(contact, "frame", 0))

        # Hazard: contact-id ordering is not causal ordering. Any material body
        # contact in a frame wins over terrain evidence from that same frame.
        if any(
            int(_row_value(peer, "body_b", -2)) >= 0
            and float(_row_value(peer, "pre_solve_closing_speed", 0.0)) >= thresholds.material_impact_speed
            for peer in by_frame[start_frame]
        ):
            continue

        for later_frame in sorted(frame for frame in by_frame if frame > start_frame):
            peers = by_frame[later_frame]
            if any(
                int(_row_value(peer, "body_b", -2)) >= 0
                and float(_row_value(peer, "pre_solve_closing_speed", 0.0)) >= thresholds.material_impact_speed
                for peer in peers
            ):
                break

            reimpact = next(
                (
                    peer
                    for peer in peers
                    if int(_row_value(peer, "body_b", -2)) == -1
                    and str(_row_value(peer, "contact_id", "")) == contact_id
                    and float(_row_value(peer, "pre_solve_closing_speed", 0.0)) >= thresholds.material_impact_speed
                ),
                None,
            )
            if reimpact is not None:
                return {
                    "contact_id": contact_id,
                    "resting_frame": start_frame,
                    "resting_closing_speed": round(closing, 6),
                    "resting_separation_bias": round(bias, 6),
                    "reimpact_frame": later_frame,
                    "reimpact_closing_speed": round(
                        float(_row_value(reimpact, "pre_solve_closing_speed", 0.0)), 6
                    ),
                }

    return None


def _counter_resets(frames: Iterable[Any], audit_start_frame: int) -> tuple[int, int | None]:
    previous_counter: int | None = None
    resets = 0
    first_frame: int | None = None

    for frame in frames:
        frame_number = int(_row_value(frame, "frame", 0))
        counter = int(_row_value(frame, "sleep_counter", 0))
        if frame_number <= audit_start_frame:
            # The boundary row seeds the first post-impact comparison, but a
            # reset caused by the impact itself remains outside the quiet run.
            previous_counter = counter
            continue

        sleeping = bool(_row_value(frame, "sleeping", 0))
        if previous_counter is not None and counter < previous_counter and not sleeping:
            resets += 1
            if first_frame is None:
                first_frame = frame_number
        previous_counter = counter

    return resets, first_frame


def _wake_transitions(frames: Iterable[Any], thresholds: StabilityThresholds) -> tuple[int, int | None]:
    previous_sleeping = False
    wakes = 0
    first_frame: int | None = None

    for frame in frames:
        frame_number = int(_row_value(frame, "frame", 0))
        sleeping = bool(_row_value(frame, "sleeping", 0))
        if frame_number >= thresholds.tail_audit_frame and previous_sleeping and not sleeping:
            wakes += 1
            if first_frame is None:
                first_frame = frame_number
        previous_sleeping = sleeping

    return wakes, first_frame


def _first_post_impact_quiet_support_frame(
    frames: Iterable[Any], post_impact_start: int, post_impact_end: int
) -> int | None:
    for frame in frames:
        frame_number = int(_row_value(frame, "frame", 0))
        if not post_impact_start < frame_number < post_impact_end:
            continue
        if bool(_row_value(frame, "sleep_supported", 0)) and int(
            _row_value(frame, "sleep_counter", 0)
        ) > 0:
            return frame_number
    return None


def _contact_is_solver_active(contact: Any) -> bool:
    # Why: a speculative manifold row can report large relative surface speed
    # while solving no impulse. Tangent-only authority still counts: ignoring it
    # would let a stale friction row manufacture motion while the oracle passes.
    return float(_row_value(contact, "normal_impulse", 0.0)) > 0.0 or abs(
        float(_row_value(contact, "tangent_impulse", 0.0))
    ) > 0.0


def analyze_body_records(
    name: str,
    frames: Iterable[Any],
    contacts: Iterable[Any],
    thresholds: StabilityThresholds = DEFAULT_THRESHOLDS,
) -> dict[str, Any]:
    """Return one bounded semantic ruling from already detached records."""
    frame_rows = sorted(list(frames), key=lambda row: int(_row_value(row, "frame", 0)))
    contact_rows = sorted(list(contacts), key=lambda row: int(_row_value(row, "frame", 0)))
    if not frame_rows:
        return {"name": name, "passed": False, "failures": ["missing_body_timeline"]}

    radius = float(_row_value(frame_rows[0], "radius", 0.0))
    end_frame = int(_row_value(frame_rows[-1], "frame", 0))
    final_sleep_frame = _terminal_sleep_start(frame_rows)
    final_sleeping = bool(_row_value(frame_rows[-1], "sleeping", 0))
    material_frames = [
        int(_row_value(contact, "frame", 0))
        for contact in contact_rows
        if float(_row_value(contact, "pre_solve_closing_speed", 0.0)) >= thresholds.material_impact_speed
    ]
    last_material_impact = max(material_frames) if material_frames else None
    post_impact_start = last_material_impact if last_material_impact is not None else -1
    post_impact_end = final_sleep_frame if final_sleep_frame is not None else end_frame + 1
    quiet_support_start = _first_post_impact_quiet_support_frame(
        frame_rows, post_impact_start, post_impact_end
    )
    quiet_support_frames = [
        frame
        for frame in frame_rows
        if quiet_support_start is not None
        and quiet_support_start <= int(_row_value(frame, "frame", 0)) < post_impact_end
    ]
    quiet_support_contacts = [
        contact
        for contact in contact_rows
        if quiet_support_start is not None
        and quiet_support_start <= int(_row_value(contact, "frame", 0)) < post_impact_end
        and _contact_is_solver_active(contact)
    ]

    maximum_vertical_speed = max(
        (abs(float(_row_value(frame, "vel_y", 0.0))) for frame in quiet_support_frames),
        default=0.0,
    )
    maximum_slip_speed = max(
        (float(_row_value(contact, "slip_speed", 0.0)) for contact in quiet_support_contacts),
        default=0.0,
    )
    slip_by_frame: dict[int, float] = {}
    for contact in quiet_support_contacts:
        frame_number = int(_row_value(contact, "frame", 0))
        slip_by_frame[frame_number] = slip_by_frame.get(frame_number, 0.0) + float(
            _row_value(contact, "slip_speed", 0.0)
        )

    dt_by_frame = {
        int(_row_value(frame, "frame", 0)): float(_row_value(frame, "dt", 0.0))
        for frame in quiet_support_frames
    }
    slip_distance = sum(slip * dt_by_frame.get(frame, 0.0) for frame, slip in slip_by_frame.items())
    slip_radius_fraction = slip_distance / radius if radius > 0.0 else math.inf
    late_frames = [
        frame
        for frame in frame_rows
        if thresholds.tail_audit_frame <= int(_row_value(frame, "frame", 0)) < post_impact_end
    ]
    reversals_x = _significant_reversals(late_frames, "vel_x", thresholds)
    reversals_z = _significant_reversals(late_frames, "vel_z", thresholds)
    # Why: a counter drop before the final material impact is evidence that the
    # body correctly abandoned an earlier quiet attempt. RS5 judges only the
    # uninterrupted supported quiet run that follows the last genuine impact.
    counter_audit_start = max(thresholds.tail_audit_frame, post_impact_start)
    counter_resets, first_counter_reset = _counter_resets(frame_rows, counter_audit_start)
    wake_count, first_wake = _wake_transitions(frame_rows, thresholds)
    resting_reimpact = _first_resting_reimpact(contact_rows, thresholds)
    sleep_latency = (
        final_sleep_frame - last_material_impact
        if final_sleep_frame is not None and last_material_impact is not None
        else None
    )

    failures: list[str] = []
    if not _timeline_is_contiguous(frame_rows):
        failures.append("incomplete_timeline")
    if not final_sleeping or final_sleep_frame is None:
        failures.append("not_finally_sleeping")
    elif final_sleep_frame > thresholds.completion_deadline_frame:
        failures.append("completion_deadline")
    if sleep_latency is None or sleep_latency > thresholds.maximum_sleep_latency_frames:
        failures.append("sleep_latency")
    if quiet_support_start is None:
        failures.append("missing_quiet_support_run")
    if maximum_vertical_speed > thresholds.maximum_vertical_speed:
        failures.append("vertical_speed")
    if maximum_slip_speed > thresholds.maximum_slip_speed:
        failures.append("slip_speed")
    if slip_radius_fraction > thresholds.maximum_slip_radius_fraction:
        failures.append("slip_distance")
    if reversals_x > thresholds.maximum_reversals_per_axis:
        failures.append("x_reversals")
    if reversals_z > thresholds.maximum_reversals_per_axis:
        failures.append("z_reversals")
    if resting_reimpact is not None:
        failures.append("resting_reimpact")
    if counter_resets > thresholds.maximum_counter_resets_after_last_impact:
        failures.append("sleep_counter_reset")
    if wake_count > thresholds.maximum_wakes_after_tail:
        failures.append("wake_after_tail")

    return {
        "name": name,
        "passed": not failures,
        "body_id": int(_row_value(frame_rows[0], "body_id", -1)),
        "radius": round(radius, 6),
        "start_frame": int(_row_value(frame_rows[0], "frame", -1)),
        "end_frame": end_frame,
        "timeline_contiguous": _timeline_is_contiguous(frame_rows),
        "final_sleep_frame": final_sleep_frame,
        "last_material_impact_frame": last_material_impact,
        "sleep_latency_frames": sleep_latency,
        "first_post_impact_quiet_support_frame": quiet_support_start,
        "maximum_quiet_support_abs_vy": round(maximum_vertical_speed, 6),
        "maximum_quiet_support_slip_speed": round(maximum_slip_speed, 6),
        "quiet_support_slip_distance": round(slip_distance, 6),
        "quiet_support_slip_radius_fraction": round(slip_radius_fraction, 6),
        "late_x_reversals": reversals_x,
        "late_z_reversals": reversals_z,
        "sleep_counter_resets_after_last_impact": counter_resets,
        "first_sleep_counter_reset_after_last_impact_frame": first_counter_reset,
        "wakes_after_tail": wake_count,
        "first_wake_after_tail_frame": first_wake,
        "resting_reimpact": resting_reimpact,
        "failures": failures,
    }


def analyze_box_control_records(
    name: str,
    frames: Iterable[Any],
    thresholds: StabilityThresholds = DEFAULT_THRESHOLDS,
) -> dict[str, Any]:
    frame_rows = sorted(list(frames), key=lambda row: int(_row_value(row, "frame", 0)))
    if not frame_rows:
        return {"name": name, "passed": False, "failures": ["missing_body_timeline"]}

    final_sleep_frame = _terminal_sleep_start(frame_rows)
    final_sleeping = bool(_row_value(frame_rows[-1], "sleeping", 0))
    maximum_speed = max(float(_row_value(frame, "speed", 0.0)) for frame in frame_rows)
    maximum_omega = max(float(_row_value(frame, "omega_mag", 0.0)) for frame in frame_rows)
    limits = BOX_CONTROL_MAXIMA.get(name)
    failures: list[str] = []
    if not _timeline_is_contiguous(frame_rows):
        failures.append("incomplete_timeline")
    if not final_sleeping or final_sleep_frame is None:
        failures.append("not_finally_sleeping")
    elif final_sleep_frame > thresholds.box_control_deadline_frame:
        failures.append("box_control_deadline")
    if limits is None:
        failures.append("missing_box_control_limits")
    else:
        if maximum_speed > limits["speed"] + 1.0e-6:
            failures.append("box_speed_regression")
        if maximum_omega > limits["omega"] + 1.0e-6:
            failures.append("box_omega_regression")

    return {
        "name": name,
        "passed": not failures,
        "body_id": int(_row_value(frame_rows[0], "body_id", -1)),
        "start_frame": int(_row_value(frame_rows[0], "frame", -1)),
        "end_frame": int(_row_value(frame_rows[-1], "frame", -1)),
        "timeline_contiguous": _timeline_is_contiguous(frame_rows),
        "final_sleep_frame": final_sleep_frame,
        "maximum_speed": round(maximum_speed, 6),
        "maximum_omega": round(maximum_omega, 6),
        "maximum_allowed_speed": None if limits is None else limits["speed"],
        "maximum_allowed_omega": None if limits is None else limits["omega"],
        "failures": failures,
    }


def analyze_completion_records(
    frames: Iterable[Any],
    balls: list[dict[str, Any]],
    boxes: list[dict[str, Any]],
    target_frames: int,
    end_status: str,
) -> dict[str, Any]:
    """Prove complete trace coverage and the authored all-dynamic sleep gate."""
    frame_rows = sorted(list(frames), key=lambda row: int(_row_value(row, "frame", -1)))
    physics_end_frame = int(_row_value(frame_rows[-1], "frame", -1)) if frame_rows else -1
    timeline_contiguous = bool(frame_rows) and int(_row_value(frame_rows[0], "frame", -1)) == 0
    timeline_contiguous = timeline_contiguous and _timeline_is_contiguous(frame_rows)
    required = [*balls, *boxes]
    required_aligned = bool(required) and all(
        bool(result.get("timeline_contiguous"))
        and int(result.get("start_frame", -1)) == 0
        and int(result.get("end_frame", -1)) == physics_end_frame
        for result in required
    )

    # Concept: the gate witness is the terminal suffix in which every dynamic
    # body is asleep. Generic process shutdown is necessary provenance, but it
    # cannot manufacture this authored scene condition.
    gate_frame: int | None = None
    for index in range(len(frame_rows) - 1, -1, -1):
        row = frame_rows[index]
        body_count = int(_row_value(row, "body_count", 0))
        all_asleep = (
            body_count > 0
            and int(_row_value(row, "awake_count", -1)) == 0
            and int(_row_value(row, "sleeping_count", -1)) == body_count
        )
        if not all_asleep:
            break
        gate_frame = int(_row_value(row, "frame", -1))

    failures: list[str] = []
    if target_frames != -1:
        failures.append("scene_not_unlimited")
    if end_status.lower() not in {"complete", "completed", "success", "process_end"}:
        failures.append("run_not_naturally_complete")
    if not timeline_contiguous or not required_aligned:
        failures.append("incomplete_timeline")
    if gate_frame is None:
        failures.append("authored_sleep_gate_not_observed")
    if any(not ball["passed"] for ball in balls):
        failures.append("ball_semantic_failure")
    if any(not box["passed"] for box in boxes):
        failures.append("box_control_failure")

    return {
        "passed": not failures,
        "physics_end_frame": physics_end_frame,
        "timeline_contiguous": timeline_contiguous,
        "required_timelines_aligned": required_aligned,
        "authored_sleep_gate_frame": gate_frame,
        "all_three_finally_sleeping": all(ball.get("final_sleep_frame") is not None for ball in balls),
        "failures": failures,
    }


def _load_named_body(conn: Any, run_id: str, name: str) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    body_row = conn.execute(
        "select body_id from bodies where run_id=? and name=? order by frame limit 1",
        (run_id, name),
    ).fetchone()
    if body_row is None:
        return [], []

    body_id = int(body_row["body_id"])
    frames = [
        dict(row)
        for row in conn.execute(
            """
            select b.*, f.dt
            from bodies b
            join frames f on f.run_id=b.run_id and f.frame=b.frame
            where b.run_id=? and b.body_id=?
            order by b.frame
            """,
            (run_id, body_id),
        ).fetchall()
    ]
    contacts = [
        dict(row)
        for row in conn.execute(
            """
            select * from contacts
            where run_id=? and (body_a=? or body_b=?)
            order by frame, contact_id
            """,
            (run_id, body_id, body_id),
        ).fetchall()
    ]
    return frames, contacts


def analyze_trace(
    trace_path: Path,
    run_id: str | None = None,
    thresholds: StabilityThresholds = DEFAULT_THRESHOLDS,
) -> dict[str, Any]:
    conn, cache = physics_query.ensure_db(str(trace_path))
    try:
        if run_id is None:
            row = conn.execute("select run_id from runs order by run_id limit 1").fetchone()
            if row is None:
                raise RuntimeError("SkullScope cache contains no run")
            run_id = str(row["run_id"])

        run = conn.execute("select * from runs where run_id=?", (run_id,)).fetchone()
        if run is None:
            raise RuntimeError(f"SkullScope run not found: {run_id}")

        balls = []
        for name in BALL_NAMES:
            frames, contacts = _load_named_body(conn, run_id, name)
            balls.append(analyze_body_records(name, frames, contacts, thresholds))

        boxes = []
        for name in BOX_NAMES:
            frames, _ = _load_named_body(conn, run_id, name)
            boxes.append(analyze_box_control_records(name, frames, thresholds))

        frame_summaries = [
            dict(row)
            for row in conn.execute(
                """
                select frame, body_count, awake_count, sleeping_count
                from frames where run_id=? order by frame
                """,
                (run_id,),
            ).fetchall()
        ]

        diagnostic_end_frame = int(run["end_frame"] if run["end_frame"] is not None else -1)
        target_frames = int(run["target_frames"] if run["target_frames"] is not None else -2)
        end_status = str(run["end_status"] or "")
        aggregate = analyze_completion_records(frame_summaries, balls, boxes, target_frames, end_status)

        return {
            "schema_version": SEMANTIC_SCHEMA_VERSION,
            "diagnostic_only": True,
            "uses_golden_hash": False,
            "trace": Path(cache["trace"]).name,
            "cache_rebuilt": bool(cache["rebuilt"]),
            "run_id": run_id,
            "scene": run["scene"],
            "diagnostic_end_frame": diagnostic_end_frame,
            "physics_end_frame": aggregate["physics_end_frame"],
            "target_frames": target_frames,
            "end_status": end_status,
            "thresholds": asdict(thresholds),
            "balls": balls,
            "box_controls": boxes,
            "aggregate": aggregate,
        }
    finally:
        conn.close()


def canonical_json(report: dict[str, Any]) -> str:
    return json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Evaluate semantic at-rest stability from a SkullScope trace.")
    parser.add_argument("trace", type=Path, help="SkullScope .physicsdiag.ndjson trace")
    parser.add_argument("--run", help="Run id when the trace contains more than one run")
    parser.add_argument("--output", type=Path, help="Optional compact JSON report path")
    outcome = parser.add_mutually_exclusive_group()
    outcome.add_argument("--require-pass", action="store_true", help="Exit nonzero unless the semantic oracle passes")
    outcome.add_argument("--expect-fail", action="store_true", help="Exit nonzero unless the semantic oracle fails")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        report = analyze_trace(args.trace, args.run)
        output = canonical_json(report)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(output, encoding="utf-8")
        print(output, end="")

        passed = bool(report["aggregate"]["passed"])
        if args.require_pass and not passed:
            return 1
        if args.expect_fail and passed:
            return 1
        return 0
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
