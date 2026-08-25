#!/usr/bin/env python3
"""
File: tools/check_at_rest_stability_analyzer.py
Purpose:
  Prove the semantic at-rest analyzer catches planted negative controls.

Summary:
  Synthetic detached body/contact fixtures exercise the same pure evaluator as
  real SkullScope traces. Clean body, control, and completion fixtures pass;
  independent mutations pin every review-reopened missed-failure boundary and the
  RS1/RS5 product controls by exact diagnostic code. RS6 additionally proves
  that motion quality begins at supported quiet progress and uses only contacts
  carrying solved normal or tangent impulse.

Invariants:
  - Each planted control changes one intended semantic and names its expected
    failure; a generic aggregate failure is not sufficient.
  - The tests never launch the runtime or update a baseline.
  - External material impact interrupts the resting re-impact sequence, so
    legitimate post-collision restitution is not mislabeled solver vibration.
  - A quiet-counter reset before a later material impact is a valid abandoned
    attempt; the same reset after the final impact remains a planted failure.
  - Pre-support impact response and zero-impulse speculative contacts cannot
    manufacture a quiet-run failure; tangent-only authority remains visible.

Related:
  - tools/analyze_at_rest_stability.py
  - tools/physics_query.py
"""

from __future__ import annotations

import json
import sys

from analyze_at_rest_stability import (
    DEFAULT_THRESHOLDS,
    SEMANTIC_SCHEMA_VERSION,
    analyze_body_records,
    analyze_box_control_records,
    analyze_completion_records,
)


def clean_fixture() -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    frames: list[dict[str, object]] = []
    for frame in range(7199, 7230):
        sleeping = frame == 7229
        frames.append(
            {
                "frame": frame,
                "body_id": 0,
                "radius": 8.0,
                "vel_x": 0.6 if frame == 7200 else 0.1,
                "vel_y": 0.1,
                "vel_z": 0.0,
                "speed": 0.1,
                "omega_mag": 0.0,
                "sleep_counter": 30 if sleeping else max(0, frame - 7199),
                "sleep_supported": 1,
                "sleeping": 1 if sleeping else 0,
                "dt": 1.0 / 120.0,
            }
        )

    contacts = [
        {
            "frame": 7199,
            "contact_id": "0:-1:0",
            "body_a": 0,
            "body_b": -1,
            "pre_solve_closing_speed": 2.0,
            "separation_bias": 0.0,
            "slip_speed": 0.1,
            "normal_impulse": 1.0,
        },
        {
            "frame": 7200,
            "contact_id": "0:-1:0",
            "body_a": 0,
            "body_b": -1,
            "pre_solve_closing_speed": 0.1,
            "separation_bias": 0.0,
            "slip_speed": 0.1,
            "normal_impulse": 1.0,
        },
    ]
    return frames, contacts


def require_exact_failure(name: str, frames: list[dict[str, object]], contacts: list[dict[str, object]], code: str) -> dict[str, object]:
    result = analyze_body_records(name, frames, contacts)
    failures = result["failures"]
    if failures != [code]:
        raise AssertionError(f"{name}: expected only {code}, observed {failures}")
    return {"name": name, "expected_failure": code, "observed_failures": failures, "passed": True}


def require_result_failure(name: str, result: dict[str, object], expected: list[str]) -> dict[str, object]:
    failures = result["failures"]
    if failures != expected:
        raise AssertionError(f"{name}: expected {expected}, observed {failures}")
    return {"name": name, "expected_failures": expected, "observed_failures": failures, "passed": True}


def check_clean_fixture() -> dict[str, object]:
    frames, contacts = clean_fixture()
    result = analyze_body_records("clean", frames, contacts)
    if not result["passed"]:
        raise AssertionError(f"clean fixture failed: {result['failures']}")
    return {"name": "clean", "passed": True}


def check_vertical_reimpact() -> dict[str, object]:
    frames, contacts = clean_fixture()
    contacts.extend(
        [
            {
                "frame": 7201,
                "contact_id": "0:-1:0",
                "body_a": 0,
                "body_b": -1,
                "pre_solve_closing_speed": 1.0,
                "separation_bias": DEFAULT_THRESHOLDS.maximum_separation_bias,
                "slip_speed": 0.1,
            },
            {
                "frame": 7202,
                "contact_id": "0:-1:0",
                "body_a": 0,
                "body_b": -1,
                "pre_solve_closing_speed": 2.1,
                "separation_bias": 0.0,
                "slip_speed": 0.1,
            },
        ]
    )
    return require_exact_failure("vertical_reimpact", frames, contacts, "resting_reimpact")


def check_external_impact_breaks_reimpact_chain() -> dict[str, object]:
    frames, contacts = clean_fixture()
    contacts.extend(
        [
            {
                "frame": 7201,
                "contact_id": "0:-1:0",
                "body_a": 0,
                "body_b": -1,
                "pre_solve_closing_speed": 1.0,
                "separation_bias": DEFAULT_THRESHOLDS.maximum_separation_bias,
                "slip_speed": 0.1,
            },
            {
                "frame": 7202,
                "contact_id": "0:4:0",
                "body_a": 0,
                "body_b": 4,
                "pre_solve_closing_speed": 3.0,
                "separation_bias": 0.0,
                "slip_speed": 0.1,
            },
            {
                "frame": 7203,
                "contact_id": "0:-1:0",
                "body_a": 0,
                "body_b": -1,
                "pre_solve_closing_speed": 2.1,
                "separation_bias": 0.0,
                "slip_speed": 0.1,
            },
        ]
    )
    result = analyze_body_records("external_impact", frames, contacts)
    if "resting_reimpact" in result["failures"]:
        raise AssertionError("external material impact did not interrupt the resting re-impact chain")
    return {"name": "external_impact_break", "passed": True}


def check_same_frame_external_impact_breaks_reimpact_chain() -> dict[str, object]:
    frames, contacts = clean_fixture()
    contacts.extend(
        [
            {
                "frame": 7201,
                "contact_id": "0:-1:0",
                "body_a": 0,
                "body_b": -1,
                "pre_solve_closing_speed": 1.0,
                "separation_bias": DEFAULT_THRESHOLDS.maximum_separation_bias,
                "slip_speed": 0.1,
            },
            {
                "frame": 7201,
                "contact_id": "0:4:0",
                "body_a": 0,
                "body_b": 4,
                "pre_solve_closing_speed": 3.0,
                "separation_bias": 0.0,
                "slip_speed": 0.1,
            },
            {
                "frame": 7202,
                "contact_id": "0:-1:0",
                "body_a": 0,
                "body_b": -1,
                "pre_solve_closing_speed": 2.1,
                "separation_bias": 0.0,
                "slip_speed": 0.1,
            },
        ]
    )
    result = analyze_body_records("same_frame_external_impact", frames, contacts)
    if "resting_reimpact" in result["failures"]:
        raise AssertionError("same-frame external material impact did not interrupt the resting re-impact chain")
    return {"name": "same_frame_external_impact_break", "passed": True}


def check_excessive_slip() -> dict[str, object]:
    frames, contacts = clean_fixture()
    contacts.append(
        {
            "frame": 7201,
            "contact_id": "0:-1:0",
            "body_a": 0,
            "body_b": -1,
            "pre_solve_closing_speed": 0.1,
            "separation_bias": 0.0,
            "slip_speed": 0.6,
            "normal_impulse": 1.0,
        }
    )
    return require_exact_failure("excessive_slip", frames, contacts, "slip_speed")


def check_accumulated_slip() -> dict[str, object]:
    frames, contacts = clean_fixture()
    for frame in range(7200, 7229):
        for contact_index in range(20):
            contacts.append(
                {
                    "frame": frame,
                    "contact_id": f"0:-1:{contact_index + 1}",
                    "body_a": 0,
                    "body_b": -1,
                    "pre_solve_closing_speed": 0.1,
                    "separation_bias": 0.0,
                    "slip_speed": 0.49,
                    "normal_impulse": 1.0,
                }
            )
    return require_exact_failure("accumulated_slip", frames, contacts, "slip_distance")


def check_excessive_quiet_vertical_speed() -> dict[str, object]:
    frames, contacts = clean_fixture()
    by_frame = {int(frame["frame"]): frame for frame in frames}
    by_frame[7201]["vel_y"] = 0.6
    return require_exact_failure("excessive_quiet_vertical", frames, contacts, "vertical_speed")


def check_pre_quiet_motion_is_excluded_from_the_quiet_run() -> dict[str, object]:
    frames, contacts = clean_fixture()
    by_frame = {int(frame["frame"]): frame for frame in frames}
    by_frame[7200]["sleep_counter"] = 0
    by_frame[7200]["sleep_supported"] = 0
    by_frame[7200]["vel_y"] = 9.0
    contacts[1]["slip_speed"] = 9.0

    result = analyze_body_records("pre_quiet_motion", frames, contacts)
    if (
        not result["passed"]
        or result["first_post_impact_quiet_support_frame"] != 7201
        or result["maximum_quiet_support_abs_vy"] > 0.5
    ):
        raise AssertionError(f"impact response polluted the supported quiet-run audit: {result}")
    return {
        "name": "pre_quiet_motion_is_excluded_from_the_quiet_run",
        "passed": True,
    }


def check_unloaded_speculative_slip_is_ignored() -> dict[str, object]:
    frames, contacts = clean_fixture()
    contacts.append(
        {
            "frame": 7201,
            "contact_id": "0:2:0",
            "body_a": 0,
            "body_b": 2,
            "pre_solve_closing_speed": 0.0,
            "separation_bias": 0.0,
            "slip_speed": 99.0,
            "normal_impulse": 0.0,
            "tangent_impulse": 0.0,
        }
    )

    result = analyze_body_records("unloaded_speculative_slip", frames, contacts)
    if not result["passed"] or result["maximum_quiet_support_slip_speed"] != 0.1:
        raise AssertionError(f"zero-load speculative row polluted the slip ruling: {result}")
    return {"name": "unloaded_speculative_slip_is_ignored", "passed": True}


def check_tangent_only_slip_is_not_ignored() -> dict[str, object]:
    frames, contacts = clean_fixture()
    contacts.append(
        {
            "frame": 7201,
            "contact_id": "0:2:0",
            "body_a": 0,
            "body_b": 2,
            "pre_solve_closing_speed": 0.0,
            "separation_bias": 0.0,
            "slip_speed": 99.0,
            "normal_impulse": 0.0,
            "tangent_impulse": 1.0,
        }
    )
    return require_exact_failure("tangent_only_slip", frames, contacts, "slip_speed")


def check_missing_quiet_support_run() -> dict[str, object]:
    frames, contacts = clean_fixture()
    for frame in frames[:-1]:
        frame["sleep_counter"] = 0
        frame["sleep_supported"] = 0
    return require_exact_failure(
        "missing_quiet_support_run", frames, contacts, "missing_quiet_support_run"
    )


def check_rolling_reversal() -> dict[str, object]:
    frames, contacts = clean_fixture()
    by_frame = {int(frame["frame"]): frame for frame in frames}
    by_frame[7200]["vel_x"] = 0.6
    by_frame[7201]["vel_x"] = -0.6
    by_frame[7202]["vel_x"] = 0.6
    return require_exact_failure("rolling_reversal", frames, contacts, "x_reversals")


def check_sleep_counter_reset() -> dict[str, object]:
    frames, contacts = clean_fixture()
    by_frame = {int(frame["frame"]): frame for frame in frames}
    by_frame[7204]["sleep_counter"] = 5
    by_frame[7205]["sleep_counter"] = 0
    return require_exact_failure("sleep_counter_reset", frames, contacts, "sleep_counter_reset")


def check_first_post_impact_counter_reset() -> dict[str, object]:
    frames, contacts = clean_fixture()
    by_frame = {int(frame["frame"]): frame for frame in frames}
    by_frame[7204]["sleep_counter"] = 5
    by_frame[7205]["sleep_counter"] = 0
    contacts.append(
        {
            "frame": 7204,
            "contact_id": "0:-1:1",
            "body_a": 0,
            "body_b": -1,
            "pre_solve_closing_speed": 2.0,
            "separation_bias": 0.0,
            "slip_speed": 0.1,
        }
    )
    return require_exact_failure(
        "first_post_impact_counter_reset", frames, contacts, "sleep_counter_reset"
    )


def check_pre_impact_counter_reset_is_valid() -> dict[str, object]:
    frames, contacts = clean_fixture()
    by_frame = {int(frame["frame"]): frame for frame in frames}
    by_frame[7204]["sleep_counter"] = 5
    by_frame[7205]["sleep_counter"] = 0
    contacts.append(
        {
            "frame": 7210,
            "contact_id": "0:-1:0",
            "body_a": 0,
            "body_b": -1,
            "pre_solve_closing_speed": 2.0,
            "separation_bias": 0.0,
            "slip_speed": 0.1,
        }
    )

    result = analyze_body_records("pre_impact_counter_reset_is_valid", frames, contacts)
    if not result["passed"] or result["sleep_counter_resets_after_last_impact"] != 0:
        raise AssertionError(f"pre-impact counter reset was treated as post-impact sleep failure: {result}")
    return {"name": "pre_impact_counter_reset_is_valid", "passed": True}


def check_incomplete_body_timeline() -> dict[str, object]:
    frames, contacts = clean_fixture()
    frames = [frame for frame in frames if frame["frame"] != 7210]
    return require_exact_failure("incomplete_body_timeline", frames, contacts, "incomplete_timeline")


def check_terminal_sleep_suffix() -> dict[str, object]:
    frames, contacts = clean_fixture()
    frames[-1]["sleeping"] = 0
    result = analyze_body_records("terminal_sleep_suffix", frames, contacts)
    if "not_finally_sleeping" not in result["failures"]:
        raise AssertionError(f"terminal sleep suffix was not required: {result['failures']}")
    return {"name": "terminal_sleep_suffix", "passed": True}


def check_wake_after_tail() -> dict[str, object]:
    frames, contacts = clean_fixture()
    by_frame = {int(frame["frame"]): frame for frame in frames}
    by_frame[7205]["sleeping"] = 1
    by_frame[7206]["sleeping"] = 0
    return require_exact_failure("wake_after_tail", frames, contacts, "wake_after_tail")


def box_fixture() -> list[dict[str, object]]:
    return [
        {"frame": 0, "body_id": 3, "speed": 1.0, "omega_mag": 1.0, "sleeping": 0},
        {"frame": 1, "body_id": 3, "speed": 0.0, "omega_mag": 0.0, "sleeping": 1},
    ]


def check_box_controls() -> list[dict[str, object]]:
    clean = analyze_box_control_records("box_a", box_fixture())
    if not clean["passed"]:
        raise AssertionError(f"clean box control failed: {clean['failures']}")

    speed_frames = box_fixture()
    speed_frames[0]["speed"] = 1.0e9
    speed = require_result_failure(
        "box_speed_regression",
        analyze_box_control_records("box_a", speed_frames),
        ["box_speed_regression"],
    )

    omega_frames = box_fixture()
    omega_frames[0]["omega_mag"] = 1.0e9
    omega = require_result_failure(
        "box_omega_regression",
        analyze_box_control_records("box_a", omega_frames),
        ["box_omega_regression"],
    )
    return [{"name": "clean_box_control", "passed": True}, speed, omega]


def completion_fixture() -> tuple[list[dict[str, object]], list[dict[str, object]], list[dict[str, object]]]:
    frames = [
        {"frame": 0, "body_count": 6, "awake_count": 6, "sleeping_count": 0},
        {"frame": 1, "body_count": 6, "awake_count": 1, "sleeping_count": 5},
        {"frame": 2, "body_count": 6, "awake_count": 0, "sleeping_count": 6},
    ]
    balls = [
        {
            "name": name,
            "passed": True,
            "timeline_contiguous": True,
            "start_frame": 0,
            "end_frame": 2,
            "final_sleep_frame": 2,
        }
        for name in ("ball_a", "ball_b", "ball_c")
    ]
    boxes = [
        {"name": name, "passed": True, "timeline_contiguous": True, "start_frame": 0, "end_frame": 2}
        for name in ("box_a", "box_b", "box_c")
    ]
    return frames, balls, boxes


def check_completion_guards() -> list[dict[str, object]]:
    frames, balls, boxes = completion_fixture()
    clean = analyze_completion_records(frames, balls, boxes, -1, "process_end")
    if not clean["passed"] or clean["authored_sleep_gate_frame"] != 2:
        raise AssertionError(f"clean authored completion failed: {clean}")

    aborted = [dict(frame) for frame in frames]
    aborted[-1]["awake_count"] = 1
    aborted[-1]["sleeping_count"] = 5
    aborted_result = require_result_failure(
        "generic_shutdown_without_gate",
        analyze_completion_records(aborted, balls, boxes, -1, "process_end"),
        ["authored_sleep_gate_not_observed"],
    )

    gapped = [frames[0], frames[2]]
    gapped_result = require_result_failure(
        "gapped_completion_timeline",
        analyze_completion_records(gapped, balls, boxes, -1, "process_end"),
        ["incomplete_timeline"],
    )

    misaligned_balls = [dict(ball) for ball in balls]
    misaligned_balls[0]["end_frame"] = 1
    misaligned_result = require_result_failure(
        "end_misaligned_body_timeline",
        analyze_completion_records(frames, misaligned_balls, boxes, -1, "process_end"),
        ["incomplete_timeline"],
    )

    truncated_boxes = [dict(box) for box in boxes]
    truncated_boxes[0]["start_frame"] = 1
    truncated_result = require_result_failure(
        "prefix_truncated_body_timeline",
        analyze_completion_records(frames, balls, truncated_boxes, -1, "process_end"),
        ["incomplete_timeline"],
    )
    return [
        {"name": "clean_authored_completion", "passed": True},
        aborted_result,
        gapped_result,
        misaligned_result,
        truncated_result,
    ]


def main() -> int:
    try:
        if SEMANTIC_SCHEMA_VERSION != 3:
            raise AssertionError(f"expected semantic schema version 3, observed {SEMANTIC_SCHEMA_VERSION}")
        results = [
            check_clean_fixture(),
            check_vertical_reimpact(),
            check_external_impact_breaks_reimpact_chain(),
            check_same_frame_external_impact_breaks_reimpact_chain(),
            check_excessive_slip(),
            check_accumulated_slip(),
            check_excessive_quiet_vertical_speed(),
            check_pre_quiet_motion_is_excluded_from_the_quiet_run(),
            check_unloaded_speculative_slip_is_ignored(),
            check_tangent_only_slip_is_not_ignored(),
            check_missing_quiet_support_run(),
            check_rolling_reversal(),
            check_sleep_counter_reset(),
            check_first_post_impact_counter_reset(),
            check_pre_impact_counter_reset_is_valid(),
            check_incomplete_body_timeline(),
            check_terminal_sleep_suffix(),
            check_wake_after_tail(),
            *check_box_controls(),
            *check_completion_guards(),
        ]
        print(json.dumps({"passed": True, "checks": results}, indent=2, sort_keys=True))
        return 0
    except Exception as exc:
        print(json.dumps({"passed": False, "error": str(exc)}, indent=2, sort_keys=True))
        return 1


if __name__ == "__main__":
    sys.exit(main())
