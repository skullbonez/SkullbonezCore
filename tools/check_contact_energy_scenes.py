#!/usr/bin/env python3
#
# File: tools/check_contact_energy_scenes.py
# Purpose:
#   Enforce semantic energy, launch, support, cache, sleep, and impact outcomes
#   for the contact-energy tower, four-brick reproduction, and 200-box topple.
#
# Summary:
#   SkullScope owns trace indexing and bounded SQL answers. This checker consumes
#   only those compact JSON answer packets. It applies workload-specific
#   physical contracts and can compare a three-frame, named-body impact witness
#   without freezing the wall's later chaotic poses.
#
# Glossary:
#   Metric packet: Bounded JSON answer emitted by `physics_query.bat sql`; summary
#     packets contain one row, while a final-body packet contains one row per
#     expected dynamic body.
#   Launch reversal: A meaningful downward-to-upward velocity crossing measured
#     with the workload's locked dead band.
#   Impact witness: The striker's exact state and ordered contacts for the three
#     frames centered on its first ragdoll contact, plus semantic wall activity.
#
# Invariants:
#   - A truncated packet is never accepted as complete evidence.
#   - Scene energy tolerance is derived from SkullScope's six-decimal JSON precision, not observed
#     post-change divergence. After the first object contact, a running minimum
#     exposes later recovery and deducts only explicit separation-bias work, so
#     the wall's large impact loss cannot hide a later injection.
#   - Tower and four-brick acceptance constrain settling; wall acceptance leaves
#     the chaotic final pose free while requiring retained, terrain-clear bodies
#     and rejecting repeated full-body-height relaunch cycles.
#   - The FP1/FP2 impact comparison gets its frame and state values from the
#     retained FP1 packet. Fixed acceptance facts are only authored identities,
#     the three-frame window, brick activation, and rebound direction.
#   - `sleep_supported` is frame-local solver evidence. A sleeping stack retains
#     its root support, but sleeping children need not repeat that flag forever.
#   - `--expect-current-failure` requires the known authoritative defect codes;
#     it cannot turn an arbitrary failure into a passing corrected gate.
#
# Related:
#   - SkullbonezData/scenes/contact_energy_tower_64.scene.json
#   - SkullbonezData/scenes/box_vibration_t0.scene.json
#   - SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json
#   - tools/physics_query.bat
#   - tools/check_replay_visual_fidelity.py
#   - Agentic/Reference/engine-glossary.md
#
"""Check compact SkullScope contact-energy metric packets."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sqlite3
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
WALL_BRICK_COUNT = 200
WALL_RAGDOLL_PART_COUNT = 10
MAX_SOLVER_ITERATIONS = 12
MAX_SETTLED_UPWARD_SPEED = 0.25
MAX_SETTLED_PENETRATION = 0.05
MIN_TERRAIN_CLEARANCE = -0.05
MAX_STATE_MAGNITUDE = 1.0e30
WALL_SCENE_BASE_SHA256 = "ee97d7acded2e90cc63a4e32a3cccf183f55f06b53e1bcf42ead9202544025dc"


# Concept: these CTEs are shared by the production question and the planted SQL
# negative controls. A running minimum prevents the wall's large first-impact
# loss from masking energy injected later; only solver-reported separation work
# is deducted. Restitution is deliberately excluded from that allowance.
UNEXPLAINED_RECOVERY_CTES = """
unexplained_path AS (
    SELECT frame,
           SUM(net_delta) OVER (ORDER BY frame ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) path
    FROM bounded_energy_steps
), unexplained_path_with_floor AS (
    SELECT frame, path,
           MIN(path) OVER (ORDER BY frame ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING) prior_floor
    FROM unexplained_path
)
""".strip()


# A single early ballistic rise is part of the authored wall impact. Popcorn is
# the same body being relaunched through more than its own nominal height twice.
# The ascent-group window ends each cycle at its first non-upward sample.
WALL_LAUNCH_CTES = """
velocity_samples AS (
    SELECT body_id, frame, pos_y, vel_y, shape, radius, half_y,
           LAG(vel_y) OVER (PARTITION BY body_id ORDER BY frame) previous_vel_y
    FROM bodies WHERE inv_mass > 0
), ascent_samples AS (
    SELECT *,
           SUM(CASE WHEN vel_y <= 0.0 THEN 1 ELSE 0 END)
               OVER (PARTITION BY body_id ORDER BY frame
                     ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) ascent_group
    FROM velocity_samples
), launch_events AS (
    SELECT body_id, frame, pos_y, ascent_group,
           CASE WHEN shape = 'sphere' THEN MAX(2.0 * radius, 0.000001)
                ELSE MAX(2.0 * half_y, 0.000001) END nominal_height
    FROM ascent_samples
    WHERE frame >= 300 AND previous_vel_y < -0.25 AND vel_y > 0.25
), launch_rises AS (
    SELECT launch.body_id, launch.frame,
           (MAX(ascent.pos_y) - launch.pos_y) / launch.nominal_height launch_height_ratio
    FROM launch_events launch
    JOIN ascent_samples ascent
      ON ascent.body_id = launch.body_id AND ascent.ascent_group = launch.ascent_group
    GROUP BY launch.body_id, launch.frame, launch.pos_y, launch.nominal_height
), repeated_popcorn AS (
    SELECT body_id, COUNT(*) large_cycle_count
    FROM launch_rises
    WHERE launch_height_ratio > 1.0
    GROUP BY body_id
    HAVING COUNT(*) > 1
)
""".strip()


@dataclass(frozen=True)
class WorkloadContract:
    dynamic_count: int
    last_frame: int
    use_local_energy_recovery: bool
    current_failure_codes: frozenset[str]


CONTRACTS = {
    "tower64": WorkloadContract(
        dynamic_count=64,
        last_frame=TOWER_LAST_FRAME,
        use_local_energy_recovery=False,
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
        use_local_energy_recovery=False,
        current_failure_codes=frozenset(),
    ),
    "wall200": WorkloadContract(
        dynamic_count=211,
        last_frame=WALL_LAST_FRAME,
        use_local_energy_recovery=True,
        current_failure_codes=frozenset(),
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
    if workload == "wall200":
        # Why: the ragdoll's point joints can exchange energy before the striker
        # arrives. Contact acceptance starts one frame before the first dynamic
        # object pair touches, keeping that independent work out of the result.
        energy_ctes = """
), first_object_contact AS (
    SELECT MIN(frame) contact_frame
    FROM contacts
    WHERE body_a >= 0 AND body_b >= 0
), energy_reference AS (
    SELECT first_object_contact.contact_frame energy_start_frame,
           dynamic_frames.mechanical energy_reference_mechanical
    FROM first_object_contact
    JOIN dynamic_frames ON dynamic_frames.frame = first_object_contact.contact_frame - 1
), energy_window AS (
    SELECT dynamic_frames.mechanical
    FROM dynamic_frames, energy_reference
    WHERE dynamic_frames.frame >= energy_reference.energy_start_frame
"""
    else:
        energy_ctes = """
), energy_reference AS (
    SELECT bounds.first_frame energy_start_frame,
           initial.initial_mechanical energy_reference_mechanical
    FROM bounds, initial
), energy_window AS (
    SELECT mechanical FROM dynamic_frames
"""

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
           SUM(sleep_supported) supported_count,
           SUM(CASE WHEN pos_x IS NULL OR pos_y IS NULL OR pos_z IS NULL
                     OR vel_x IS NULL OR vel_y IS NULL OR vel_z IS NULL
                     OR omega_x IS NULL OR omega_y IS NULL OR omega_z IS NULL
                     OR q_x IS NULL OR q_y IS NULL OR q_z IS NULL OR q_w IS NULL
                     OR speed IS NULL OR omega_mag IS NULL OR mass IS NULL OR inv_mass IS NULL
                     OR inertia_x IS NULL OR inertia_y IS NULL OR inertia_z IS NULL
                     OR radius IS NULL OR half_x IS NULL OR half_y IS NULL OR half_z IS NULL
                     OR linear_energy IS NULL OR angular_energy IS NULL
                     OR ABS(pos_x) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(pos_y) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(pos_z) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(vel_x) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(vel_y) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(vel_z) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(omega_x) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(omega_y) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(omega_z) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(q_x) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(q_y) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(q_z) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(q_w) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(linear_energy) > {MAX_STATE_MAGNITUDE:.1f}
                     OR ABS(angular_energy) > {MAX_STATE_MAGNITUDE:.1f}
                    THEN 1 ELSE 0 END) invalid_count
    FROM bodies WHERE inv_mass > 0 GROUP BY frame
), initial AS (
    SELECT mechanical initial_mechanical FROM dynamic_frames, bounds WHERE frame = first_frame
{energy_ctes}), separation_work_frames AS (
    SELECT frame,
           SUM(MAX(COALESCE(separation_bias, 0.0), 0.0)
               * MAX(COALESCE(normal_impulse, 0.0), 0.0)) separation_work
    FROM contacts
    GROUP BY frame
), energy_steps AS (
    SELECT dynamic_frames.frame,
           dynamic_frames.mechanical
             - LAG(dynamic_frames.mechanical) OVER (ORDER BY dynamic_frames.frame)
             - COALESCE(separation_work_frames.separation_work, 0.0) net_delta
    FROM dynamic_frames
    LEFT JOIN separation_work_frames ON separation_work_frames.frame = dynamic_frames.frame
), bounded_energy_steps AS (
    SELECT energy_steps.frame, energy_steps.net_delta
    FROM energy_steps, energy_reference
    WHERE energy_steps.frame >= energy_reference.energy_start_frame
      AND energy_steps.net_delta IS NOT NULL
), {UNEXPLAINED_RECOVERY_CTES}, {WALL_LAUNCH_CTES}
SELECT bounds.last_frame,
       (SELECT dynamic_count FROM dynamic_frames WHERE frame = bounds.first_frame) dynamic_count,
       COALESCE((SELECT MAX(solver_iterations) FROM solver_stats), 0) max_iterations,
       initial.initial_mechanical,
       energy_reference.energy_start_frame,
       energy_reference.energy_reference_mechanical,
       (SELECT MAX(mechanical) FROM energy_window)
           - energy_reference.energy_reference_mechanical peak_gain_over_reference,
       COALESCE((SELECT MAX(path - MIN(0.0, COALESCE(prior_floor, 0.0)))
                 FROM unexplained_path_with_floor), 0.0) max_unexplained_energy_recovery,
       COALESCE((SELECT SUM(invalid_count) FROM dynamic_frames), 0) invalid_body_samples,
       (SELECT MAX(max_up_vel) FROM dynamic_frames WHERE frame >= 300) max_up_vel_after_300,
       (SELECT MAX(max_speed) FROM dynamic_frames) max_speed,
       COALESCE((SELECT COUNT(*) FROM velocity_samples
                 WHERE frame >= 300 AND previous_vel_y < -0.01 AND vel_y > 0.01), 0)
           downward_to_upward_flips,
       COALESCE((SELECT COUNT(*) FROM velocity_samples
                 WHERE frame > bounds.last_frame - 300 AND previous_vel_y < -0.01 AND vel_y > 0.25), 0)
           launch_reversals_final_300,
       COALESCE((SELECT COUNT(*) FROM repeated_popcorn), 0) repeated_popcorn_bodies,
       COALESCE((SELECT MAX(launch_height_ratio) FROM launch_rises), 0.0) max_launch_height_ratio,
       COALESCE((SELECT MAX(frame) FROM launch_rises WHERE launch_height_ratio > 1.0), -1)
           last_large_launch_frame,
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
FROM bounds, initial, energy_reference
""".strip().replace("\n", " ")


FINAL_BODY_QUESTION = """
WITH bounds AS (SELECT MAX(frame) last_frame FROM frames)
SELECT body_id, shape, pos_y, q_x, q_y, q_z, q_w, radius,
       half_x, half_y, half_z
FROM bodies, bounds
WHERE frame = last_frame AND inv_mass > 0
ORDER BY body_id
""".strip().replace("\n", " ")


# Why: FP2 closure needs an executable witness for the promoted fast striker,
# but the post-wall cascade is intentionally chaotic. This query therefore
# returns only the first-ragdoll-impact neighborhood while computing the named
# post-impact wall activity over the complete trace inside SQLite.
WALL200_IMPACT_QUESTION = """
WITH bounds AS (
    SELECT MIN(frame) first_frame FROM frames
), named_bodies AS (
    SELECT body_id, name
    FROM bodies, bounds
    WHERE frame = first_frame
), striker AS (
    SELECT body_id FROM named_bodies WHERE name = 'prediction_striker_ball'
), ragdoll_bodies AS (
    SELECT body_id FROM named_bodies WHERE name GLOB 'prediction_impact_ragdoll_*'
), wall_bricks AS (
    SELECT body_id FROM named_bodies WHERE name GLOB 'prediction_wall_brick_*'
), ragdoll_impact AS (
    SELECT MIN(contact.frame) impact_frame
    FROM contacts contact, striker
    WHERE (contact.body_a = striker.body_id AND contact.body_b IN (SELECT body_id FROM ragdoll_bodies))
       OR (contact.body_b = striker.body_id AND contact.body_a IN (SELECT body_id FROM ragdoll_bodies))
), wall_impact AS (
    SELECT MIN(contact.frame) impact_frame
    FROM contacts contact, striker
    WHERE (contact.body_a = striker.body_id AND contact.body_b IN (SELECT body_id FROM wall_bricks))
       OR (contact.body_b = striker.body_id AND contact.body_a IN (SELECT body_id FROM wall_bricks))
), brick_activity AS (
    SELECT COUNT(DISTINCT CASE WHEN body.sleeping = 0 THEN body.body_id END) awake_brick_count,
           COUNT(DISTINCT CASE WHEN body.speed > 0.0 THEN body.body_id END) moving_brick_count
    FROM bodies body
    JOIN wall_bricks brick ON brick.body_id = body.body_id
    CROSS JOIN ragdoll_impact
    WHERE body.frame >= ragdoll_impact.impact_frame
), striker_contacts AS (
    SELECT contact.*,
           CASE WHEN contact.body_a = striker.body_id THEN contact.body_b ELSE contact.body_a END partner_body_id
    FROM contacts contact, striker, ragdoll_impact
    WHERE (contact.body_a = striker.body_id OR contact.body_b = striker.body_id)
      AND contact.frame BETWEEN ragdoll_impact.impact_frame - 1 AND ragdoll_impact.impact_frame + 1
), ordered_contact_rows AS (
    SELECT contact.frame,
           contact.contact_id,
           CASE WHEN ragdoll.body_id IS NOT NULL THEN 1 ELSE 0 END is_ragdoll,
           CASE WHEN brick.body_id IS NOT NULL THEN 1 ELSE 0 END is_wall_brick,
           json_object(
               'contact_id', contact.contact_id,
               'body_a', contact.body_a,
               'body_b', contact.body_b,
               'partner_body_id', contact.partner_body_id,
               'partner_name', COALESCE(partner.name, '<terrain>'),
               'contact_type', contact.contact_type,
               'feature_id', contact.feature_id,
               'point_count', contact.point_count,
               'normal_x', contact.normal_x,
               'normal_y', contact.normal_y,
               'normal_z', contact.normal_z,
               'penetration', contact.penetration,
               'normal_impulse', contact.normal_impulse,
               'separation_bias', contact.separation_bias,
               'pre_solve_normal_speed', contact.pre_solve_normal_speed,
               'pre_solve_closing_speed', contact.pre_solve_closing_speed,
               'pre_solve_slip_speed', contact.pre_solve_slip_speed,
               'tangent_impulse', contact.tangent_impulse,
               'slip_speed', contact.slip_speed,
               'rolling_residual', contact.rolling_residual,
               'warm_started', contact.warm_started,
               'supports_sleep', contact.supports_sleep
           ) contact_row
    FROM striker_contacts contact
    LEFT JOIN named_bodies partner ON partner.body_id = contact.partner_body_id
    LEFT JOIN ragdoll_bodies ragdoll ON ragdoll.body_id = contact.partner_body_id
    LEFT JOIN wall_bricks brick ON brick.body_id = contact.partner_body_id
    ORDER BY contact.frame, contact.contact_id
), contact_windows AS (
    SELECT frame,
           COUNT(*) striker_contact_count,
           SUM(is_ragdoll) striker_ragdoll_contact_count,
           SUM(is_wall_brick) striker_wall_contact_count,
           GROUP_CONCAT(contact_row, char(30) ORDER BY contact_id) contact_signature
    FROM ordered_contact_rows
    GROUP BY frame
)
SELECT (SELECT COUNT(*) FROM striker) named_striker_count,
       (SELECT COUNT(*) FROM ragdoll_bodies) named_ragdoll_count,
       (SELECT COUNT(*) FROM wall_bricks) named_wall_brick_count,
       brick_activity.awake_brick_count,
       brick_activity.moving_brick_count,
       ragdoll_impact.impact_frame first_ragdoll_contact_frame,
       wall_impact.impact_frame first_wall_contact_frame,
       body.frame - ragdoll_impact.impact_frame impact_offset,
       body.frame,
       body.body_id,
       body.name,
       body.shape,
       body.pos_x,
       body.pos_y,
       body.pos_z,
       body.vel_x,
       body.vel_y,
       body.vel_z,
       body.omega_x,
       body.omega_y,
       body.omega_z,
       body.q_x,
       body.q_y,
       body.q_z,
       body.q_w,
       body.speed,
       body.omega_mag,
       body.sleeping,
       body.sleep_supported,
       body.sleep_inhibited,
       body.sleep_counter,
       body.island_id,
       COALESCE(contact_windows.striker_contact_count, 0) striker_contact_count,
       COALESCE(contact_windows.striker_ragdoll_contact_count, 0) striker_ragdoll_contact_count,
       COALESCE(contact_windows.striker_wall_contact_count, 0) striker_wall_contact_count,
       COALESCE(contact_windows.contact_signature, '') contact_signature
FROM bodies body, striker, ragdoll_impact, wall_impact, brick_activity
LEFT JOIN contact_windows ON contact_windows.frame = body.frame
WHERE body.body_id = striker.body_id
  AND body.frame BETWEEN ragdoll_impact.impact_frame - 1 AND ragdoll_impact.impact_frame + 1
ORDER BY body.frame
""".strip().replace("\n", " ")


def print_questions(workload: str, trace: str) -> None:
    questions = [
        {
            "name": f"{workload}_summary",
            "command": f'tools\\physics_query.bat "{trace}" sql "{summary_question(workload)}" --limit 5',
        }
    ]
    if workload == "wall200":
        questions.extend(
            [
                {
                    "name": "wall200_final_bodies",
                    "command": f'tools\\physics_query.bat "{trace}" sql "{FINAL_BODY_QUESTION}" --limit 300',
                },
                {
                    "name": "wall200_impact_witness",
                    "command": f'tools\\physics_query.bat "{trace}" sql "{WALL200_IMPACT_QUESTION}" --limit 5',
                },
            ]
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


def canonical_payload_sha256(payload: dict[str, Any]) -> str:
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def validate_wall_scene_payload(payload: dict[str, Any]) -> set[str]:
    failures: set[str] = set()
    if payload.get("format") != "skullbonez.scene.json" or payload.get("version") != 3:
        failures.add("scene_schema")

    world = payload.get("simulation", {}).get("world", {})
    playback = payload.get("playback", {})
    if not close_number(world.get("gravity"), -32.0):
        failures.add("scene_gravity")
    if not close_number(world.get("fluidDensity"), 0.0):
        failures.add("scene_external_work")
    if playback.get("frames") != "unlimited" or playback.get("fixedStep") is not True:
        failures.add("scene_timestep")

    objects = payload.get("objects")
    if not isinstance(objects, list) or len(objects) != 203:
        failures.add("scene_body_count")
        return failures

    # Invariant: the catcher is the sole authorized scene delta. Hashing the
    # complete pre-catcher payload pins striker, ragdoll, bricks, materials,
    # world, playback, presentation, and cameras without freezing final physics.
    base_payload = dict(payload)
    base_payload["objects"] = objects[:-1]
    if canonical_payload_sha256(base_payload) != WALL_SCENE_BASE_SHA256:
        failures.add("scene_original_payload")

    # Owner-authorized scene boundary: this fixed catcher is beyond the impact
    # wall and changes no dynamic count. It only retains the post-demo striker.
    catcher = objects[-1]
    if (
        catcher.get("name") != "prediction_striker_catcher_wall"
        or catcher.get("type") != "boxState"
        or catcher.get("fixed") is not True
        or catcher.get("position") != [1008.0, 25.0, 500.0]
        or catcher.get("halfExtents") != [2.0, 25.0, 64.0]
        or not close_number(catcher.get("restitution"), 0.0)
    ):
        failures.add("scene_catcher")
    return failures


def validate_wall_scene(path: Path) -> set[str]:
    return validate_wall_scene_payload(read_packet(path))


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


WALL200_IMPACT_METADATA_FIELDS = (
    "named_striker_count",
    "named_ragdoll_count",
    "named_wall_brick_count",
    "awake_brick_count",
    "moving_brick_count",
    "first_ragdoll_contact_frame",
    "first_wall_contact_frame",
)
WALL200_IMPACT_INTEGER_STATE_FIELDS = (
    "impact_offset",
    "frame",
    "body_id",
    "sleeping",
    "sleep_supported",
    "sleep_inhibited",
    "sleep_counter",
    "island_id",
    "striker_contact_count",
    "striker_ragdoll_contact_count",
    "striker_wall_contact_count",
)
WALL200_IMPACT_FLOAT_STATE_FIELDS = (
    "pos_x",
    "pos_y",
    "pos_z",
    "vel_x",
    "vel_y",
    "vel_z",
    "omega_x",
    "omega_y",
    "omega_z",
    "q_x",
    "q_y",
    "q_z",
    "q_w",
    "speed",
    "omega_mag",
)
WALL200_IMPACT_EXACT_SLICE_FIELDS = (
    *WALL200_IMPACT_INTEGER_STATE_FIELDS,
    "name",
    "shape",
    *WALL200_IMPACT_FLOAT_STATE_FIELDS,
    "contact_signature",
)


def load_wall200_impact_packet(path: Path) -> list[dict[str, Any]]:
    payload = read_packet(path)
    if payload.get("truncated"):
        raise ValueError(f"{path}: truncated wall200 impact packet is not admissible")
    rows = payload.get("rows")
    if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
        raise ValueError(f"{path}: wall200 impact packet rows must be objects")
    return rows


def evaluate_wall200_impact(rows: list[dict[str, Any]]) -> tuple[set[str], dict[str, Any]]:
    failures: set[str] = set()
    facts: dict[str, Any] = {}
    if len(rows) != 3:
        failures.add("impact_window_row_count")
        return failures, facts

    offsets: list[int | None] = []
    for row in rows:
        offsets.append(metric_int(row, "impact_offset", failures))
        for field in WALL200_IMPACT_INTEGER_STATE_FIELDS[1:]:
            metric_int(row, field, failures)
        for field in WALL200_IMPACT_FLOAT_STATE_FIELDS:
            metric_float(row, field, failures)
        if row.get("name") != "prediction_striker_ball" or not isinstance(row.get("shape"), str):
            failures.add("striker_identity")
        if not isinstance(row.get("contact_signature"), str):
            failures.add("missing_contact_signature")

    # Invariant: row order is part of the compact packet contract. It prevents
    # an unordered result from accidentally pairing different state frames.
    if offsets != [-1, 0, 1]:
        failures.add("impact_window_order")
        return failures, facts

    first_row = rows[0]
    for field in WALL200_IMPACT_METADATA_FIELDS:
        expected = first_row.get(field)
        for row in rows:
            if row.get(field) != expected:
                failures.add(f"inconsistent_metadata:{field}")
                break

    named_striker_count = metric_int(first_row, "named_striker_count", failures)
    named_ragdoll_count = metric_int(first_row, "named_ragdoll_count", failures)
    named_wall_brick_count = metric_int(first_row, "named_wall_brick_count", failures)
    awake_brick_count = metric_int(first_row, "awake_brick_count", failures)
    moving_brick_count = metric_int(first_row, "moving_brick_count", failures)
    ragdoll_impact = metric_int(first_row, "first_ragdoll_contact_frame", failures)
    wall_impact = metric_int(first_row, "first_wall_contact_frame", failures)

    if named_striker_count != 1:
        failures.add("named_striker_count")
    if named_ragdoll_count != WALL_RAGDOLL_PART_COUNT:
        failures.add("named_ragdoll_count")
    if named_wall_brick_count != WALL_BRICK_COUNT:
        failures.add("named_wall_brick_count")
    if awake_brick_count != WALL_BRICK_COUNT:
        failures.add("awake_wall_bricks")
    if moving_brick_count != WALL_BRICK_COUNT:
        failures.add("moving_wall_bricks")

    if ragdoll_impact is not None:
        for row, offset in zip(rows, (-1, 0, 1), strict=True):
            if metric_int(row, "frame", failures) != ragdoll_impact + offset:
                failures.add("impact_window_frame")

    ragdoll_contacts_at_impact = metric_int(rows[1], "striker_ragdoll_contact_count", failures)
    if ragdoll_contacts_at_impact is None or ragdoll_contacts_at_impact < 1:
        failures.add("ragdoll_contact_at_impact")
    preimpact_velocity_y = metric_float(rows[0], "vel_y", failures)
    postimpact_velocity_y = metric_float(rows[2], "vel_y", failures)
    if preimpact_velocity_y is not None and preimpact_velocity_y >= 0.0:
        failures.add("preimpact_downward_velocity")
    if postimpact_velocity_y is not None and postimpact_velocity_y <= 0.0:
        failures.add("postimpact_rebound_velocity")

    facts.update(
        {
            "first_ragdoll_contact_frame": ragdoll_impact,
            "first_wall_contact_frame": wall_impact,
            "awake_brick_count": awake_brick_count,
            "moving_brick_count": moving_brick_count,
            "preimpact_velocity_y": preimpact_velocity_y,
            "postimpact_velocity_y": postimpact_velocity_y,
        }
    )
    return failures, facts


def compare_wall200_impact(
    reference_rows: list[dict[str, Any]], candidate_rows: list[dict[str, Any]]
) -> tuple[set[str], dict[str, Any]]:
    reference_failures, reference_facts = evaluate_wall200_impact(reference_rows)
    candidate_failures, candidate_facts = evaluate_wall200_impact(candidate_rows)
    failures = {f"reference:{failure}" for failure in reference_failures}
    failures.update(f"candidate:{failure}" for failure in candidate_failures)

    if len(reference_rows) == 3 and len(candidate_rows) == 3:
        for field in ("first_ragdoll_contact_frame", "first_wall_contact_frame"):
            if reference_rows[0].get(field) != candidate_rows[0].get(field):
                failures.add(f"exact_mismatch:{field}")
        for reference, candidate in zip(reference_rows, candidate_rows, strict=True):
            offset = reference.get("impact_offset")
            for field in WALL200_IMPACT_EXACT_SLICE_FIELDS:
                if reference.get(field) != candidate.get(field):
                    mismatch = (
                        f"exact_mismatch:impact{offset:+}:{field}"
                        if isinstance(offset, int)
                        else f"exact_mismatch:{field}"
                    )
                    failures.add(mismatch)

    return failures, {"reference": reference_facts, "candidate": candidate_facts}


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
    energy_reference = metric_float(metrics, "energy_reference_mechanical", failures)
    peak_gain = metric_float(metrics, "peak_gain_over_reference", failures)
    unexplained_recovery = metric_float(metrics, "max_unexplained_energy_recovery", failures)
    invalid_body_samples = metric_int(metrics, "invalid_body_samples", failures)

    if last_frame != contract.last_frame:
        failures.add("incomplete")
    if dynamic_count != contract.dynamic_count:
        failures.add("body_count")
    if max_iterations is not None and max_iterations > MAX_SOLVER_ITERATIONS:
        failures.add("iteration_policy")
    if invalid_body_samples is not None and invalid_body_samples != 0:
        failures.add("nonfinite_state")
    energy_gain = unexplained_recovery if contract.use_local_energy_recovery else peak_gain
    if energy_reference is not None and energy_gain is not None:
        tolerance = scene_energy_tolerance(energy_reference)
        facts["energy_tolerance"] = tolerance
        if energy_gain > tolerance:
            failures.add("energy_gain")
    return failures, facts


def evaluate_settling(
    metrics: dict[str, Any],
    contract: WorkloadContract,
    *,
    require_penetration: bool,
    require_quiet_cache_tail: bool,
    minimum_final_supports: int,
) -> tuple[set[str], dict[str, float]]:
    # Invariant: every settling fixture must stop relaunching and reach permanent
    # sleep. Tower diagnostics additionally retain their stricter cache, full
    # support, and penetration signals; the accepted four-brick lane needs one
    # terrain-root support because sleeping children no longer publish the flag.
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
    if require_quiet_cache_tail and cache_tail_misses is not None and cache_tail_misses != 0:
        failures.add("cache_tail")
    if final_supported is not None and final_supported < minimum_final_supports:
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
    repeated_popcorn = metric_int(metrics, "repeated_popcorn_bodies", failures)
    max_launch_height_ratio = metric_float(metrics, "max_launch_height_ratio", failures)
    max_speed = metric_float(metrics, "max_speed", failures)

    if final_sleeping != contract.dynamic_count:
        failures.add("sleep")
    if not finite_number(permanent_sleep) or int(permanent_sleep) > contract.last_frame:
        failures.add("sleep")
    if launch_tail is not None and launch_tail != 0:
        failures.add("launch_reversal")
    if repeated_popcorn is not None and repeated_popcorn != 0:
        failures.add("popcorn_cycle")
    if max_launch_height_ratio is not None:
        facts["max_launch_height_ratio"] = max_launch_height_ratio
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
        return evaluate_settling(
            metrics,
            contract,
            require_penetration=True,
            require_quiet_cache_tail=True,
            minimum_final_supports=contract.dynamic_count,
        )
    if workload == "four_brick":
        return evaluate_settling(
            metrics,
            contract,
            require_penetration=False,
            require_quiet_cache_tail=False,
            minimum_final_supports=1,
        )
    return evaluate_wall(metrics, body_rows, contract)


def passing_metrics(workload: str) -> dict[str, Any]:
    contract = CONTRACTS[workload]
    metrics: dict[str, Any] = {
        "last_frame": contract.last_frame,
        "dynamic_count": contract.dynamic_count,
        "max_iterations": MAX_SOLVER_ITERATIONS,
        "initial_mechanical": 1_000_000.0,
        "energy_start_frame": 0,
        "energy_reference_mechanical": 1_000_000.0,
        "peak_gain_over_reference": 0.0,
        "max_unexplained_energy_recovery": 0.0,
        "invalid_body_samples": 0,
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
        metrics.update(
            {
                "launch_reversals_final_300": 0,
                "repeated_popcorn_bodies": 0,
                "max_launch_height_ratio": 0.5,
                "max_speed": 100.0,
            }
        )
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


def passing_wall200_impact_rows(
    *, impact_frame: int = 40, wall_contact_frame: int = 80
) -> list[dict[str, Any]]:
    # Sensitivity fixture only: these values exercise relative comparison and
    # direction checks. Production acceptance never reads them as a golden.
    rows: list[dict[str, Any]] = []
    for offset in (-1, 0, 1):
        rows.append(
            {
                "named_striker_count": 1,
                "named_ragdoll_count": WALL_RAGDOLL_PART_COUNT,
                "named_wall_brick_count": WALL_BRICK_COUNT,
                "awake_brick_count": WALL_BRICK_COUNT,
                "moving_brick_count": WALL_BRICK_COUNT,
                "first_ragdoll_contact_frame": impact_frame,
                "first_wall_contact_frame": wall_contact_frame,
                "impact_offset": offset,
                "frame": impact_frame + offset,
                "body_id": 7,
                "name": "prediction_striker_ball",
                "shape": "sphere",
                "pos_x": 100.0 + offset,
                "pos_y": 20.0,
                "pos_z": 30.0,
                "vel_x": 50.0,
                "vel_y": {-1: -2.0, 0: -1.0, 1: 2.0}[offset],
                "vel_z": 0.0,
                "omega_x": 0.0,
                "omega_y": 0.0,
                "omega_z": 0.0,
                "q_x": 0.0,
                "q_y": 0.0,
                "q_z": 0.0,
                "q_w": 1.0,
                "speed": 50.0,
                "omega_mag": 0.0,
                "sleeping": 0,
                "sleep_supported": 0,
                "sleep_inhibited": 1,
                "sleep_counter": 0,
                "island_id": 3,
                "striker_contact_count": 1 if offset == 0 else 0,
                "striker_ragdoll_contact_count": 1 if offset == 0 else 0,
                "striker_wall_contact_count": 0,
                "contact_signature": '{"partner_name":"prediction_impact_ragdoll_torso"}'
                if offset == 0
                else "",
            }
        )
    return rows


def run_sql_negative_controls() -> None:
    # Sensitivity: a large first loss cannot buy headroom for two later injected
    # steps. This executes the same running-minimum CTE used by the real packet.
    connection = sqlite3.connect(":memory:")
    connection.execute("CREATE TABLE bounded_energy_steps(frame INTEGER, net_delta REAL)")
    connection.executemany(
        "INSERT INTO bounded_energy_steps VALUES(?, ?)",
        [(1, -700_000.0), (2, 0.0), (3, 40.0), (4, 40.0)],
    )
    recovery = connection.execute(
        f"""WITH {UNEXPLAINED_RECOVERY_CTES}
        SELECT COALESCE(MAX(path - MIN(0.0, COALESCE(prior_floor, 0.0))), 0.0)
        FROM unexplained_path_with_floor"""
    ).fetchone()[0]
    assert close_number(recovery, 80.0), recovery

    # Sensitivity: ordinary impact bounce is allowed once, but two full-height
    # relaunches of the same body are a repeated popcorn cycle.
    connection.execute(
        """CREATE TABLE bodies(
            body_id INTEGER, frame INTEGER, pos_y REAL, vel_y REAL, shape TEXT,
            radius REAL, half_y REAL, inv_mass REAL)"""
    )
    connection.executemany(
        "INSERT INTO bodies VALUES(?, ?, ?, ?, ?, ?, ?, ?)",
        [
            (0, 299, 0.0, -1.0, "box", 0.0, 1.0, 1.0),
            (0, 300, 0.0, 1.0, "box", 0.0, 1.0, 1.0),
            (0, 301, 3.0, 1.0, "box", 0.0, 1.0, 1.0),
            (0, 302, 3.0, 0.0, "box", 0.0, 1.0, 1.0),
            (0, 303, 0.0, -1.0, "box", 0.0, 1.0, 1.0),
            (0, 304, 0.0, 1.0, "box", 0.0, 1.0, 1.0),
            (0, 305, 3.0, 1.0, "box", 0.0, 1.0, 1.0),
            (0, 306, 3.0, 0.0, "box", 0.0, 1.0, 1.0),
        ],
    )
    repeated = connection.execute(
        f"WITH {WALL_LAUNCH_CTES} SELECT COUNT(*) FROM repeated_popcorn"
    ).fetchone()[0]
    assert repeated == 1, repeated
    connection.close()


def run_wall200_impact_sql_control() -> None:
    # Sensitivity: exercise the shipped SQL against a complete synthetic named
    # topology. The frame values live only in this self-test and never become
    # acceptance constants; the comparator obtains them from its FP1 packet.
    connection = sqlite3.connect(":memory:")
    connection.row_factory = sqlite3.Row
    connection.executescript(
        """
        CREATE TABLE frames(frame INTEGER);
        CREATE TABLE bodies(
            frame INTEGER, body_id INTEGER, name TEXT, shape TEXT DEFAULT 'box',
            pos_x REAL DEFAULT 0.0, pos_y REAL DEFAULT 0.0, pos_z REAL DEFAULT 0.0,
            vel_x REAL DEFAULT 0.0, vel_y REAL DEFAULT 0.0, vel_z REAL DEFAULT 0.0,
            omega_x REAL DEFAULT 0.0, omega_y REAL DEFAULT 0.0, omega_z REAL DEFAULT 0.0,
            q_x REAL DEFAULT 0.0, q_y REAL DEFAULT 0.0, q_z REAL DEFAULT 0.0, q_w REAL DEFAULT 1.0,
            speed REAL DEFAULT 0.0, omega_mag REAL DEFAULT 0.0,
            sleeping INTEGER DEFAULT 0, sleep_supported INTEGER DEFAULT 0,
            sleep_inhibited INTEGER DEFAULT 0, sleep_counter INTEGER DEFAULT 0,
            island_id INTEGER DEFAULT 1
        );
        CREATE TABLE contacts(
            frame INTEGER, contact_id TEXT, body_a INTEGER, body_b INTEGER,
            contact_type TEXT DEFAULT 'body', feature_id INTEGER DEFAULT 0,
            point_count INTEGER DEFAULT 1,
            normal_x REAL DEFAULT 0.0, normal_y REAL DEFAULT 1.0, normal_z REAL DEFAULT 0.0,
            penetration REAL DEFAULT 0.0, normal_impulse REAL DEFAULT 1.0,
            separation_bias REAL DEFAULT 0.0, pre_solve_normal_speed REAL DEFAULT 0.0,
            pre_solve_closing_speed REAL DEFAULT 0.0, pre_solve_slip_speed REAL DEFAULT 0.0,
            tangent_impulse REAL DEFAULT 0.0, slip_speed REAL DEFAULT 0.0,
            rolling_residual REAL DEFAULT 0.0, warm_started INTEGER DEFAULT 0,
            supports_sleep INTEGER DEFAULT 0
        );
        INSERT INTO frames VALUES(0);
        INSERT INTO bodies(frame, body_id, name, shape, sleeping)
            VALUES(0, 1, 'prediction_striker_ball', 'sphere', 0);
        """
    )
    connection.executemany(
        "INSERT INTO bodies(frame, body_id, name, sleeping) VALUES(0, ?, ?, 1)",
        [
            (part + 2, f"prediction_impact_ragdoll_part_{part:02d}")
            for part in range(WALL_RAGDOLL_PART_COUNT)
        ],
    )
    connection.executemany(
        "INSERT INTO bodies(frame, body_id, name, sleeping) VALUES(0, ?, ?, 1)",
        [
            (brick + 100, f"prediction_wall_brick_{brick:03d}")
            for brick in range(WALL_BRICK_COUNT)
        ],
    )
    connection.executemany(
        "INSERT INTO bodies(frame, body_id, name, speed, sleeping) VALUES(30, ?, ?, 1.0, 0)",
        [
            (brick + 100, f"prediction_wall_brick_{brick:03d}")
            for brick in range(WALL_BRICK_COUNT)
        ],
    )
    connection.executemany(
        """INSERT INTO bodies(
               frame, body_id, name, shape, pos_x, pos_y, vel_x, vel_y, speed, sleep_inhibited)
           VALUES(?, 1, 'prediction_striker_ball', 'sphere', ?, 20.0, 50.0, ?, 50.0, 1)""",
        [(9, 99.0, -2.0), (10, 100.0, -1.0), (11, 101.0, 2.0)],
    )
    connection.executemany(
        "INSERT INTO contacts(frame, contact_id, body_a, body_b) VALUES(?, ?, 1, ?)",
        [(10, "striker-ragdoll", 2), (20, "striker-wall", 100)],
    )

    rows = [dict(row) for row in connection.execute(WALL200_IMPACT_QUESTION).fetchall()]
    failures, facts = evaluate_wall200_impact(rows)
    assert not failures, failures
    assert facts["first_ragdoll_contact_frame"] == 10
    assert facts["first_wall_contact_frame"] == 20
    assert facts["awake_brick_count"] == WALL_BRICK_COUNT
    assert facts["moving_brick_count"] == WALL_BRICK_COUNT
    connection.close()


def run_self_test() -> None:
    run_sql_negative_controls()
    run_wall200_impact_sql_control()

    impact_reference = passing_wall200_impact_rows()
    failures, _ = compare_wall200_impact(impact_reference, json.loads(json.dumps(impact_reference)))
    assert not failures, failures

    shifted_impact = passing_wall200_impact_rows(impact_frame=41)
    failures, _ = compare_wall200_impact(impact_reference, shifted_impact)
    assert "exact_mismatch:first_ragdoll_contact_frame" in failures

    shifted_wall = passing_wall200_impact_rows(wall_contact_frame=81)
    failures, _ = compare_wall200_impact(impact_reference, shifted_wall)
    assert "exact_mismatch:first_wall_contact_frame" in failures

    changed_state = json.loads(json.dumps(impact_reference))
    changed_state[1]["pos_x"] += 0.125
    failures, _ = compare_wall200_impact(impact_reference, changed_state)
    assert "exact_mismatch:impact+0:pos_x" in failures

    changed_contacts = json.loads(json.dumps(impact_reference))
    changed_contacts[1]["contact_signature"] += "-changed"
    failures, _ = compare_wall200_impact(impact_reference, changed_contacts)
    assert "exact_mismatch:impact+0:contact_signature" in failures

    missing_cascade = json.loads(json.dumps(impact_reference))
    for row in missing_cascade:
        row["awake_brick_count"] = WALL_BRICK_COUNT - 1
        row["moving_brick_count"] = WALL_BRICK_COUNT - 1
    failures, _ = compare_wall200_impact(impact_reference, missing_cascade)
    assert {"candidate:awake_wall_bricks", "candidate:moving_wall_bricks"}.issubset(failures)

    wrong_approach = json.loads(json.dumps(impact_reference))
    wrong_approach[0]["vel_y"] = 0.0
    failures, _ = compare_wall200_impact(impact_reference, wrong_approach)
    assert "candidate:preimpact_downward_velocity" in failures

    missing_rebound = json.loads(json.dumps(impact_reference))
    missing_rebound[2]["vel_y"] = 0.0
    failures, _ = compare_wall200_impact(impact_reference, missing_rebound)
    assert "candidate:postimpact_rebound_velocity" in failures

    unordered_window = list(reversed(json.loads(json.dumps(impact_reference))))
    failures, _ = compare_wall200_impact(impact_reference, unordered_window)
    assert "candidate:impact_window_order" in failures

    wall_scene = read_packet(Path("SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json"))
    assert not validate_wall_scene_payload(wall_scene)
    retuned_wall = json.loads(json.dumps(wall_scene))
    retuned_wall["objects"][0]["velocity"][0] = 1.0
    retuned_wall["objects"][0]["restitution"] = 0.0
    assert "scene_original_payload" in validate_wall_scene_payload(retuned_wall)
    moved_catcher = json.loads(json.dumps(wall_scene))
    moved_catcher["objects"][-1]["position"][0] = 600.0
    assert "scene_catcher" in validate_wall_scene_payload(moved_catcher)

    for workload, contract in CONTRACTS.items():
        metrics = passing_metrics(workload)
        bodies = passing_body_rows(contract.dynamic_count) if workload == "wall200" else []
        failures, _ = evaluate(workload, metrics, bodies)
        assert not failures, (workload, failures)

    tower = passing_metrics("tower64")
    tower["peak_gain_over_reference"] = scene_energy_tolerance(tower["energy_reference_mechanical"]) * 2.0
    tower["max_unexplained_energy_recovery"] = scene_energy_tolerance(tower["energy_reference_mechanical"]) * 2.0
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

    four_brick = passing_metrics("four_brick")
    four_brick["cache_misses_final_300"] = 900
    four_brick["final_supported"] = 1
    failures, _ = evaluate("four_brick", four_brick, [])
    assert not failures
    four_brick["final_supported"] = 0
    failures, _ = evaluate("four_brick", four_brick, [])
    assert "support" in failures

    wall = passing_metrics("wall200")
    wall_bodies = passing_body_rows(CONTRACTS["wall200"].dynamic_count)
    wall_bodies[0]["pos_y"] = -1.0
    wall["launch_reversals_final_300"] = 1
    wall["peak_gain_over_reference"] = -700_000.0
    wall["max_unexplained_energy_recovery"] = scene_energy_tolerance(wall["energy_reference_mechanical"]) * 2.0
    wall["invalid_body_samples"] = 1
    wall["repeated_popcorn_bodies"] = 1
    failures, _ = evaluate("wall200", wall, wall_bodies)
    assert {
        "body_below_support",
        "energy_gain",
        "launch_reversal",
        "nonfinite_state",
        "popcorn_cycle",
    }.issubset(failures)

    exact_float = struct.unpack("f", struct.pack("f", 1.0))[0]
    assert scene_energy_tolerance(exact_float) == SCENE_ENERGY_ABSOLUTE_FLOOR
    print("SELF_TEST_PASS: contact-energy and wall200 impact checks reject every planted failure")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument(
        "--compare-wall200-impact",
        nargs=2,
        type=Path,
        metavar=("FP1_PACKET", "FP2_PACKET"),
        help="Compare retained-FP1 and candidate-FP2 wall200 impact witness packets.",
    )
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
        if not args.workload and not args.compare_wall200_impact:
            return 0
    if args.compare_wall200_impact:
        reference_path, candidate_path = args.compare_wall200_impact
        try:
            reference_rows = load_wall200_impact_packet(reference_path)
            candidate_rows = load_wall200_impact_packet(candidate_path)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            print(f"ERROR: {error}", file=sys.stderr)
            return 2
        failures, facts = compare_wall200_impact(reference_rows, candidate_rows)
        print(
            json.dumps(
                {
                    "workload": "wall200_impact",
                    "reference_packet": str(reference_path),
                    "candidate_packet": str(candidate_path),
                    "status": "pass" if not failures else "fail",
                    "failures": sorted(failures),
                    "facts": facts,
                },
                sort_keys=True,
                separators=(",", ":"),
            )
        )
        return 0 if not failures else 1
    if args.print_questions:
        if not args.workload:
            print("ERROR: --print-questions requires --workload", file=sys.stderr)
            return 2
        print_questions(args.workload, args.trace)
        return 0
    if args.scene:
        try:
            scene_failures = (
                validate_wall_scene(args.scene) if args.workload == "wall200" else validate_tower_scene(args.scene)
            )
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
    if not required:
        print(
            f"ERROR: {args.workload} has no authoritative current failure; use --expect-pass",
            file=sys.stderr,
        )
        return 1
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
