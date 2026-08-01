#!/usr/bin/env python3
#
# File: tools/check_contact_energy_scenes.py
# Purpose:
#   Enforce semantic energy, launch, support, cache, and sleep outcomes for the
#   contact-energy tower, four-brick reproduction, and 200-box topple.
#
# Summary:
#   SkullScope owns trace indexing and bounded SQL answers. This checker consumes
#   only those compact JSON answer packets, merges their one-row metrics, and
#   applies workload-specific physical contracts without freezing chaotic poses
#   or accepting a replacement trace merely because it repeats.
#
# Glossary:
#   Metric packet: Bounded JSON answer emitted by `physics_query.bat sql`; summary
#     packets contain one row, while a final-body packet contains one row per
#     expected dynamic body.
#   Launch reversal: A meaningful downward-to-upward velocity crossing measured
#     with the workload's locked dead band.
#
# Invariants:
#   - A truncated packet is never accepted as complete evidence.
#   - Scene energy tolerance is derived from ES0 float precision, not observed
#     post-change divergence.
#   - Tower and four-brick acceptance constrain settling; wall acceptance leaves
#     the chaotic final pose free while requiring retained, supported bodies.
#   - `--expect-current-failure` requires the known authoritative defect codes;
#     it cannot turn an arbitrary failure into a passing corrected gate.
#
# Related:
#   - SkullbonezData/scenes/contact_energy_tower_64.scene.json
#   - SkullbonezData/scenes/box_vibration_t0.scene.json
#   - SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json
#   - tools/physics_query.bat
#   - Agentic/Reports/2026-08-02/contact-energy-and-warm-start-integrity-es0.md
#   - Agentic/Reference/engine-glossary.md
#
"""Check compact SkullScope contact-energy metric packets."""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


FLOAT_EPSILON = 2.0**-23
SCENE_ENERGY_ABSOLUTE_FLOOR = 0.05
SCENE_ENERGY_EPSILON_SCALE = 128.0
TOWER_LAST_FRAME = 2399
FOUR_BRICK_LAST_FRAME = 1199
WALL_LAST_FRAME = 6799
MAX_SOLVER_ITERATIONS = 12
MAX_SETTLED_UPWARD_SPEED = 0.25
MAX_SETTLED_PENETRATION = 0.05
MIN_TERRAIN_CLEARANCE = -0.05


@dataclass(frozen=True)
class WorkloadContract:
    dynamic_count: int
    last_frame: int
    current_failure_codes: frozenset[str]


CONTRACTS = {
    "tower64": WorkloadContract(
        dynamic_count=64,
        last_frame=TOWER_LAST_FRAME,
        current_failure_codes=frozenset(
            {
                "incomplete",
                "penetration",
                "cache_tail",
                "support",
                "sleep",
            }
        ),
    ),
    "four_brick": WorkloadContract(
        dynamic_count=4,
        last_frame=FOUR_BRICK_LAST_FRAME,
        current_failure_codes=frozenset(
            {
                "launch_reversal",
                "launch_speed",
                "cache_tail",
                "sleep",
            }
        ),
    ),
    "wall200": WorkloadContract(
        dynamic_count=211,
        last_frame=WALL_LAST_FRAME,
        current_failure_codes=frozenset(
            {
                "energy_gain",
                "body_below_support",
                "sleep",
            }
        ),
    ),
}


WORKLOAD_GRAVITY_MAGNITUDE = {
    "tower64": 50.0,
    "four_brick": 50.0,
    "wall200": 32.0,
}


def summary_question(workload: str) -> str:
    # Why: one aggregate row keeps model-facing evidence bounded while SQLite
    # still evaluates every frame/body row inside the read-only cache.
    gravity = WORKLOAD_GRAVITY_MAGNITUDE[workload]
    return f"""
WITH bounds AS (
    SELECT MIN(frame) first_frame, MAX(frame) last_frame FROM frames
), dynamic_frames AS (
    SELECT frame,
           COUNT(*) dynamic_count,
           SUM(linear_energy + angular_energy + mass * {gravity:.1f} * pos_y) mechanical,
           MAX(vel_y) max_up_vel,
           MAX(speed) max_speed,
           SUM(sleeping) sleeping_count,
           SUM(sleep_supported) supported_count
    FROM bodies WHERE inv_mass > 0 GROUP BY frame
), initial AS (
    SELECT mechanical initial_mechanical FROM dynamic_frames, bounds WHERE frame = first_frame
), velocities AS (
    SELECT body_id, frame, vel_y,
           LAG(vel_y) OVER (PARTITION BY body_id ORDER BY frame) previous_vel_y
    FROM bodies WHERE inv_mass > 0
)
SELECT bounds.last_frame,
       (SELECT dynamic_count FROM dynamic_frames WHERE frame = bounds.first_frame) dynamic_count,
       COALESCE((SELECT MAX(solver_iterations) FROM solver_stats), 0) max_iterations,
       initial.initial_mechanical,
       (SELECT MAX(mechanical) FROM dynamic_frames) - initial.initial_mechanical peak_gain_over_initial,
       (SELECT MAX(max_up_vel) FROM dynamic_frames WHERE frame >= 300) max_up_vel_after_300,
       (SELECT MAX(max_speed) FROM dynamic_frames) max_speed,
       COALESCE((SELECT COUNT(*) FROM velocities
                 WHERE frame >= 300 AND previous_vel_y < -0.01 AND vel_y > 0.01), 0)
           downward_to_upward_flips,
       COALESCE((SELECT COUNT(*) FROM velocities
                 WHERE frame > bounds.last_frame - 300 AND previous_vel_y < -0.01 AND vel_y > 0.25), 0)
           launch_reversals_final_300,
       COALESCE((SELECT MAX(penetration) FROM contacts), 0.0) max_penetration,
       COALESCE((SELECT SUM(cache_misses) FROM solver_stats
                 WHERE frame > bounds.last_frame - 300), 0) cache_misses_final_300,
       (SELECT sleeping_count FROM dynamic_frames WHERE frame = bounds.last_frame) final_sleeping,
       (SELECT supported_count FROM dynamic_frames WHERE frame = bounds.last_frame) final_supported,
       (SELECT MIN(candidate.frame) FROM dynamic_frames candidate
        WHERE candidate.sleeping_count = (SELECT dynamic_count FROM dynamic_frames WHERE frame = bounds.first_frame)
          AND NOT EXISTS (
              SELECT 1 FROM dynamic_frames later
              WHERE later.frame >= candidate.frame
                AND later.sleeping_count < (SELECT dynamic_count FROM dynamic_frames WHERE frame = bounds.first_frame)
          )) permanent_all_sleep_frame
FROM bounds, initial
""".strip().replace("\n", " ")


FINAL_BODY_QUESTION = """
WITH bounds AS (SELECT MAX(frame) last_frame FROM frames)
SELECT body_id, shape, pos_y, q_x, q_y, q_z, q_w, radius,
       half_x, half_y, half_z
FROM bodies, bounds
WHERE frame = last_frame AND inv_mass > 0
ORDER BY body_id
""".strip().replace("\n", " ")


def print_questions(workload: str, trace: str) -> None:
    questions = [
        {
            "name": f"{workload}_summary",
            "command": f'tools\\physics_query.bat "{trace}" sql "{summary_question(workload)}" --limit 5',
        }
    ]
    if workload == "wall200":
        questions.append(
            {
                "name": "wall200_final_bodies",
                "command": f'tools\\physics_query.bat "{trace}" sql "{FINAL_BODY_QUESTION}" --limit 300',
            }
        )
    print(json.dumps({"workload": workload, "questions": questions}, indent=2))


def scene_energy_tolerance(initial_mechanical: float) -> float:
    return max(
        SCENE_ENERGY_ABSOLUTE_FLOOR,
        SCENE_ENERGY_EPSILON_SCALE * FLOAT_EPSILON * abs(initial_mechanical),
    )


def close_number(value: Any, expected: float) -> bool:
    return finite_number(value) and math.isclose(float(value), expected, rel_tol=0.0, abs_tol=1.0e-6)


def validate_tower_scene(path: Path) -> set[str]:
    payload = read_packet(path)
    failures: set[str] = set()
    if payload.get("format") != "skullbonez.scene.json" or payload.get("version") != 3:
        failures.add("scene_schema")

    world = payload.get("simulation", {}).get("world", {})
    playback = payload.get("playback", {})
    if not close_number(world.get("gravity"), -50.0):
        failures.add("scene_gravity")
    if not close_number(world.get("fluidDensity"), 0.0):
        failures.add("scene_external_work")
    if playback.get("frames") != 2400 or playback.get("fixedStep") is not True:
        failures.add("scene_timestep")

    objects = payload.get("objects")
    if not isinstance(objects, list) or len(objects) != 65:
        failures.add("scene_body_count")
        return failures

    foundation = objects[0]
    if (
        foundation.get("name") != "foundation"
        or foundation.get("type") != "box"
        or foundation.get("fixed") is not True
        or foundation.get("position") != [492.0, 0.0, 492.0]
        or foundation.get("halfExtents") != [12.0, 2.9, 12.0]
        or not close_number(foundation.get("restitution"), 0.08)
    ):
        failures.add("scene_foundation")

    forbidden_disturbance_fields = {
        "velocity",
        "angularVelocity",
        "euler",
        "impulse",
        "impulseWorldOffsetFromCenter",
    }
    for level, slab in enumerate(objects[1:], start=1):
        expected_y = round(5.88 * level, 6)
        position = slab.get("position")
        if (
            slab.get("name") != f"slab_{level:03d}"
            or slab.get("type") != "box"
            or slab.get("fixed") is not False
            or not isinstance(position, list)
            or len(position) != 3
            or not close_number(position[0], 492.0)
            or not close_number(position[1], expected_y)
            or not close_number(position[2], 492.0)
            or slab.get("halfExtents") != [10.0, 2.9, 10.0]
            or not close_number(slab.get("mass"), 30.0)
            or not close_number(slab.get("restitution"), 0.08)
        ):
            failures.add("scene_slab_geometry")
            break
        if forbidden_disturbance_fields.intersection(slab):
            failures.add("scene_disturbance")
            break
    return failures


def finite_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(float(value))


def read_packet(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    encoding = "utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
    return json.loads(data.decode(encoding))


def metric_float(metrics: dict[str, Any], name: str, failures: set[str]) -> float | None:
    value = metrics.get(name)
    if not finite_number(value):
        failures.add(f"missing_or_nonfinite:{name}")
        return None
    return float(value)


def metric_int(metrics: dict[str, Any], name: str, failures: set[str]) -> int | None:
    value = metrics.get(name)
    if not finite_number(value) or int(value) != value:
        failures.add(f"missing_or_nonfinite:{name}")
        return None
    return int(value)


def load_summary_packets(paths: Iterable[Path]) -> dict[str, Any]:
    metrics: dict[str, Any] = {}
    for path in paths:
        payload = read_packet(path)
        if payload.get("truncated"):
            raise ValueError(f"{path}: truncated SkullScope packet is not admissible")
        rows = payload.get("rows")
        if not isinstance(rows, list) or len(rows) != 1 or not isinstance(rows[0], dict):
            raise ValueError(f"{path}: summary packet must contain exactly one object row")
        for name, value in rows[0].items():
            if name in metrics and metrics[name] != value:
                raise ValueError(f"{path}: conflicting metric {name!r}")
            metrics[name] = value
    return metrics


def load_body_packet(path: Path | None) -> list[dict[str, Any]]:
    if path is None:
        return []
    payload = read_packet(path)
    if payload.get("truncated"):
        raise ValueError(f"{path}: truncated final-body packet is not admissible")
    rows = payload.get("rows")
    if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
        raise ValueError(f"{path}: final-body packet rows must be objects")
    return rows


def quaternion_support_extent_y(row: dict[str, Any], failures: set[str]) -> float | None:
    # Concept: body retention is judged at the physical lower support point.
    # Spheres use their radius; boxes rotate local half-extents into the world-Y
    # support extent using the same active quaternion convention as Physics.
    shape = str(row.get("shape", "")).lower()
    if "sphere" in shape or "ball" in shape:
        return metric_float(row, "radius", failures)

    half_x = metric_float(row, "half_x", failures)
    half_y = metric_float(row, "half_y", failures)
    half_z = metric_float(row, "half_z", failures)
    q_x = metric_float(row, "q_x", failures)
    q_y = metric_float(row, "q_y", failures)
    q_z = metric_float(row, "q_z", failures)
    q_w = metric_float(row, "q_w", failures)
    values = (half_x, half_y, half_z, q_x, q_y, q_z, q_w)
    if any(value is None for value in values):
        return None

    magnitude_sq = q_x * q_x + q_y * q_y + q_z * q_z + q_w * q_w
    if magnitude_sq <= 1.0e-12:
        failures.add("invalid_orientation")
        return None
    inverse_magnitude = 1.0 / math.sqrt(magnitude_sq)
    x = q_x * inverse_magnitude
    y = q_y * inverse_magnitude
    z = q_z * inverse_magnitude
    w = q_w * inverse_magnitude
    row_y_x = 2.0 * x * y + 2.0 * w * z
    row_y_y = 1.0 - 2.0 * x * x - 2.0 * z * z
    row_y_z = 2.0 * y * z - 2.0 * w * x
    return abs(row_y_x) * half_x + abs(row_y_y) * half_y + abs(row_y_z) * half_z


def body_support_failures(body_rows: list[dict[str, Any]], expected_count: int) -> set[str]:
    failures: set[str] = set()
    if len(body_rows) != expected_count:
        failures.add("body_count")
        return failures

    seen_ids: set[int] = set()
    for row in body_rows:
        body_id = metric_int(row, "body_id", failures)
        pos_y = metric_float(row, "pos_y", failures)
        if body_id is not None:
            if body_id in seen_ids:
                failures.add("body_identity")
            seen_ids.add(body_id)
        extent_y = quaternion_support_extent_y(row, failures)
        if pos_y is not None and extent_y is not None and pos_y - extent_y < MIN_TERRAIN_CLEARANCE:
            failures.add("body_below_support")
    return failures


def evaluate_common(metrics: dict[str, Any], contract: WorkloadContract) -> tuple[set[str], dict[str, float]]:
    failures: set[str] = set()
    facts: dict[str, float] = {}
    last_frame = metric_int(metrics, "last_frame", failures)
    dynamic_count = metric_int(metrics, "dynamic_count", failures)
    max_iterations = metric_int(metrics, "max_iterations", failures)
    initial_mechanical = metric_float(metrics, "initial_mechanical", failures)
    peak_gain = metric_float(metrics, "peak_gain_over_initial", failures)

    if last_frame != contract.last_frame:
        failures.add("incomplete")
    if dynamic_count != contract.dynamic_count:
        failures.add("body_count")
    if max_iterations is not None and max_iterations > MAX_SOLVER_ITERATIONS:
        failures.add("iteration_policy")
    if initial_mechanical is not None and peak_gain is not None:
        tolerance = scene_energy_tolerance(initial_mechanical)
        facts["energy_tolerance"] = tolerance
        if peak_gain > tolerance:
            failures.add("energy_gain")
    return failures, facts


def evaluate_settling(
    metrics: dict[str, Any],
    contract: WorkloadContract,
    *,
    require_penetration: bool,
) -> tuple[set[str], dict[str, float]]:
    # Invariant: settled fixtures must stay quiet over the final evidence window;
    # total cache statistics cannot hide late misses or repeated relaunches.
    failures, facts = evaluate_common(metrics, contract)
    reversals = metric_int(metrics, "downward_to_upward_flips", failures)
    max_upward_speed = metric_float(metrics, "max_up_vel_after_300", failures)
    cache_tail_misses = metric_int(metrics, "cache_misses_final_300", failures)
    final_supported = metric_int(metrics, "final_supported", failures)
    final_sleeping = metric_int(metrics, "final_sleeping", failures)
    permanent_sleep = metrics.get("permanent_all_sleep_frame")

    if reversals is not None and reversals != 0:
        failures.add("launch_reversal")
    if max_upward_speed is not None and max_upward_speed > MAX_SETTLED_UPWARD_SPEED:
        failures.add("launch_speed")
    if cache_tail_misses is not None and cache_tail_misses != 0:
        failures.add("cache_tail")
    if final_supported != contract.dynamic_count:
        failures.add("support")
    if final_sleeping != contract.dynamic_count:
        failures.add("sleep")
    if not finite_number(permanent_sleep) or int(permanent_sleep) > contract.last_frame:
        failures.add("sleep")

    if require_penetration:
        max_penetration = metric_float(metrics, "max_penetration", failures)
        if max_penetration is not None and max_penetration > MAX_SETTLED_PENETRATION:
            failures.add("penetration")
    return failures, facts


def evaluate_wall(
    metrics: dict[str, Any],
    body_rows: list[dict[str, Any]],
    contract: WorkloadContract,
) -> tuple[set[str], dict[str, float]]:
    failures, facts = evaluate_common(metrics, contract)
    final_sleeping = metric_int(metrics, "final_sleeping", failures)
    permanent_sleep = metrics.get("permanent_all_sleep_frame")
    launch_tail = metric_int(metrics, "launch_reversals_final_300", failures)
    max_speed = metric_float(metrics, "max_speed", failures)

    if final_sleeping != contract.dynamic_count:
        failures.add("sleep")
    if not finite_number(permanent_sleep) or int(permanent_sleep) > contract.last_frame:
        failures.add("sleep")
    if launch_tail is not None and launch_tail != 0:
        failures.add("launch_reversal")
    if max_speed is None:
        failures.add("nonfinite_state")
    failures.update(body_support_failures(body_rows, contract.dynamic_count))
    return failures, facts


def evaluate(
    workload: str,
    metrics: dict[str, Any],
    body_rows: list[dict[str, Any]],
) -> tuple[set[str], dict[str, float]]:
    contract = CONTRACTS[workload]
    if workload == "tower64":
        return evaluate_settling(metrics, contract, require_penetration=True)
    if workload == "four_brick":
        return evaluate_settling(metrics, contract, require_penetration=True)
    return evaluate_wall(metrics, body_rows, contract)


def passing_metrics(workload: str) -> dict[str, Any]:
    contract = CONTRACTS[workload]
    metrics: dict[str, Any] = {
        "last_frame": contract.last_frame,
        "dynamic_count": contract.dynamic_count,
        "max_iterations": MAX_SOLVER_ITERATIONS,
        "initial_mechanical": 1_000_000.0,
        "peak_gain_over_initial": 0.0,
        "final_sleeping": contract.dynamic_count,
        "permanent_all_sleep_frame": contract.last_frame - 300,
    }
    if workload in {"tower64", "four_brick"}:
        metrics.update(
            {
                "downward_to_upward_flips": 0,
                "max_up_vel_after_300": 0.0,
                "cache_misses_final_300": 0,
                "final_supported": contract.dynamic_count,
                "max_penetration": 0.0,
            }
        )
    else:
        metrics.update({"launch_reversals_final_300": 0, "max_speed": 100.0})
    return metrics


def passing_body_rows(count: int) -> list[dict[str, Any]]:
    return [
        {
            "body_id": index,
            "shape": "sphere",
            "pos_y": 1.0,
            "radius": 0.5,
        }
        for index in range(count)
    ]


def run_self_test() -> None:
    for workload, contract in CONTRACTS.items():
        metrics = passing_metrics(workload)
        bodies = passing_body_rows(contract.dynamic_count) if workload == "wall200" else []
        failures, _ = evaluate(workload, metrics, bodies)
        assert not failures, (workload, failures)

    tower = passing_metrics("tower64")
    tower["peak_gain_over_initial"] = scene_energy_tolerance(tower["initial_mechanical"]) * 2.0
    tower["downward_to_upward_flips"] = 1
    tower["max_up_vel_after_300"] = MAX_SETTLED_UPWARD_SPEED + 0.01
    tower["max_penetration"] = MAX_SETTLED_PENETRATION + 0.001
    tower["cache_misses_final_300"] = 1
    tower["final_supported"] = 63
    tower["final_sleeping"] = 63
    failures, _ = evaluate("tower64", tower, [])
    assert {
        "energy_gain",
        "launch_reversal",
        "launch_speed",
        "penetration",
        "cache_tail",
        "support",
        "sleep",
    }.issubset(failures)

    wall = passing_metrics("wall200")
    wall_bodies = passing_body_rows(CONTRACTS["wall200"].dynamic_count)
    wall_bodies[0]["pos_y"] = -1.0
    wall["launch_reversals_final_300"] = 1
    failures, _ = evaluate("wall200", wall, wall_bodies)
    assert {"body_below_support", "launch_reversal"}.issubset(failures)

    exact_float = struct.unpack("f", struct.pack("f", 1.0))[0]
    assert scene_energy_tolerance(exact_float) == SCENE_ENERGY_ABSOLUTE_FLOOR
    print("SELF_TEST_PASS: contact-energy semantic checks reject every planted failure")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--workload", choices=sorted(CONTRACTS))
    parser.add_argument("--packet", action="append", type=Path, default=[])
    parser.add_argument("--bodies-packet", type=Path)
    parser.add_argument("--print-questions", action="store_true")
    parser.add_argument("--trace", default="<trace.physicsdiag.ndjson>")
    parser.add_argument("--scene", type=Path)
    expectation = parser.add_mutually_exclusive_group()
    expectation.add_argument("--expect-pass", action="store_true")
    expectation.add_argument("--expect-current-failure", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        run_self_test()
        if not args.workload:
            return 0
    if args.print_questions:
        if not args.workload:
            print("ERROR: --print-questions requires --workload", file=sys.stderr)
            return 2
        print_questions(args.workload, args.trace)
        return 0
    if args.scene:
        try:
            scene_failures = validate_tower_scene(args.scene)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            print(f"ERROR: {error}", file=sys.stderr)
            return 2
        print(
            json.dumps(
                {
                    "scene": str(args.scene),
                    "status": "pass" if not scene_failures else "fail",
                    "failures": sorted(scene_failures),
                },
                sort_keys=True,
                separators=(",", ":"),
            )
        )
        if scene_failures:
            return 1
        if not args.workload:
            return 0
    if not args.workload or not args.packet:
        print("ERROR: --workload and at least one --packet are required", file=sys.stderr)
        return 2
    if not args.expect_pass and not args.expect_current_failure:
        print("ERROR: choose --expect-pass or --expect-current-failure", file=sys.stderr)
        return 2

    try:
        metrics = load_summary_packets(args.packet)
        body_rows = load_body_packet(args.bodies_packet)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    failures, facts = evaluate(args.workload, metrics, body_rows)
    result = {
        "workload": args.workload,
        "status": "pass" if not failures else "fail",
        "failures": sorted(failures),
        "facts": facts,
    }
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))

    if args.expect_pass:
        return 0 if not failures else 1
    required = CONTRACTS[args.workload].current_failure_codes
    missing_current_failures = sorted(required - failures)
    if missing_current_failures:
        print(
            "ERROR: current-state packet did not reproduce required failures: "
            + ", ".join(missing_current_failures),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
