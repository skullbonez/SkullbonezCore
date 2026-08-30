#!/usr/bin/env python3
"""Validate complete fixed-step evidence for persistent-island sleeping."""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import sys
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


FLOAT_FIELDS = (
    "posX",
    "posY",
    "posZ",
    "velX",
    "velY",
    "velZ",
    "speed",
    "omegaX",
    "omegaY",
    "omegaZ",
    "omegaMag",
    "qX",
    "qY",
    "qZ",
    "qW",
    "maxPenetration",
    "maxSeparationBias",
    "maxClosingSpeed",
    "maxSlipSpeed",
    "maxImpulse",
)
INT_FIELDS = (
    "frame",
    "idx",
    "grounded",
    "sleeping",
    "sleepInhibited",
    "sleepCounter",
    "islandRoot",
    "bodyEligible",
    "islandEligible",
    "topologyStable",
    "islandCanSleep",
    "contactRows",
    "resetReason",
)
REQUIRED_FIELDS = ("name",) + FLOAT_FIELDS + INT_FIELDS
BOOLEAN_FIELDS = (
    "grounded",
    "sleeping",
    "sleepInhibited",
    "bodyEligible",
    "islandEligible",
    "topologyStable",
    "islandCanSleep",
)


@dataclass(frozen=True)
class Contract:
    workload: str
    expected_names: frozenset[str]
    expected_frames: int
    minimum_frames: int
    final_sleep_tail: int
    require_final_sleep: bool
    forbid_wake: bool = False
    visual_review_required: bool = False


def contract_for(workload: str) -> Contract:
    if workload == "corner":
        return Contract(
            "corner",
            frozenset(("sleep_ground_brick", "sleep_leaning_corner_brick")),
            expected_frames=3600,
            minimum_frames=3600,
            final_sleep_tail=600,
            require_final_sleep=True,
            forbid_wake=False,
        )
    if workload == "edge":
        return Contract(
            "edge",
            frozenset(("sleep_ground_brick", "sleep_leaning_edge_brick")),
            expected_frames=3600,
            minimum_frames=3600,
            final_sleep_tail=0,
            require_final_sleep=False,
            forbid_wake=False,
            visual_review_required=True,
        )
    if workload == "wall200":
        names = frozenset(
            f"prediction_wall_brick_r{row:02d}_c{column:02d}"
            for row in range(10)
            for column in range(20)
        )
        return Contract(
            "wall200", names, expected_frames=2000, minimum_frames=2000, final_sleep_tail=0, require_final_sleep=False
        )
    if workload == "at_rest":
        return Contract(
            "at_rest",
            frozenset(("ball_a", "ball_b", "ball_c")),
            expected_frames=0,
            # The authored sleeping-body gate ends this scene as soon as all
            # three balls sleep, so physics-frame count is policy-dependent.
            # Continuity, a complete deactivation window, final sleep, and the
            # basin-position oracle below are the completeness witnesses.
            minimum_frames=30,
            final_sleep_tail=1,
            require_final_sleep=True,
        )
    raise ValueError(f"unknown workload: {workload}")


@dataclass(frozen=True)
class Sample:
    frame: int
    index: int
    name: str
    values: dict[str, float]
    flags: dict[str, int]
    pose: tuple[str, ...]


@dataclass
class BodyState:
    index: int = -1
    last_sleeping: int = 0
    sleep_pose: tuple[str, ...] | None = None
    transitions: int = 0
    wakes: int = 0
    final_sleep_run: int = 0
    history: deque[Sample] = field(default_factory=deque)
    first_sample: Sample | None = None
    last_sample: Sample | None = None
    max_impulse_span: float = 0.0


class Failures:
    def __init__(self) -> None:
        self._items: dict[str, str] = {}

    def add(self, category: str, detail: str) -> None:
        self._items.setdefault(category, detail)

    def categories(self) -> set[str]:
        return set(self._items)

    def rendered(self) -> list[str]:
        return [f"{category}: {self._items[category]}" for category in sorted(self._items)]

    def __bool__(self) -> bool:
        return bool(self._items)


def parse_sample(row: dict[str, str], failures: Failures) -> Sample | None:
    name = row.get("name", "")
    try:
        values = {key: float(row[key]) for key in FLOAT_FIELDS}
        flags = {key: int(row[key]) for key in INT_FIELDS}
    except (KeyError, TypeError, ValueError) as error:
        failures.add("malformed_row", f"body={name!r}: {error}")
        return None

    for key, value in values.items():
        if not math.isfinite(value):
            failures.add("non_finite", f"body={name} frame={flags['frame']} field={key}")

    for key in BOOLEAN_FIELDS:
        if flags[key] not in (0, 1):
            failures.add("invalid_boolean", f"body={name} frame={flags['frame']} field={key} value={flags[key]}")

    if flags["sleepCounter"] < 0 or flags["contactRows"] < 0 or values["maxImpulse"] < 0.0:
        failures.add("invalid_metric", f"body={name} frame={flags['frame']}")

    pose = tuple(row[key] for key in ("posX", "posY", "posZ", "qX", "qY", "qZ", "qW"))
    return Sample(flags["frame"], flags["idx"], name, values, flags, pose)


def is_stable(sample: Sample, linear_speed: float, angular_speed: float, penetration: float) -> bool:
    # Pre-solve closing speed includes the gravity step on supported bodies; the
    # post-solve velocity, penetration, and impulse history carry stability.
    return (
        sample.flags["bodyEligible"] == 1
        # CSV speeds are rounded to four decimal places while bodyEligible was
        # decided from full-precision values. Equality here admits only that
        # diagnostic rounding at the authored threshold.
        and sample.values["speed"] <= linear_speed
        and sample.values["omegaMag"] <= angular_speed
        and sample.values["maxPenetration"] <= penetration
        and sample.values["maxSeparationBias"] <= linear_speed
    )


def sleeping_velocity_is_zero(sample: Sample) -> bool:
    return all(
        sample.values[key] == 0.0
        for key in ("velX", "velY", "velZ", "speed", "omegaX", "omegaY", "omegaZ", "omegaMag")
    )


def validate_frame(
    frame: int,
    samples: dict[str, Sample],
    contract: Contract,
    states: dict[str, BodyState],
    failures: Failures,
    linear_speed: float,
    angular_speed: float,
    penetration: float,
) -> None:
    missing = contract.expected_names.difference(samples)
    if missing:
        failures.add("missing_body_row", f"frame={frame} first={sorted(missing)[0]}")

    island_states: dict[int, set[int]] = {}
    for sample in samples.values():
        island_states.setdefault(sample.flags["islandRoot"], set()).add(sample.flags["sleeping"])
    for root, sleep_values in island_states.items():
        if len(sleep_values) > 1:
            failures.add("split_island_state", f"frame={frame} islandRoot={root}")

    for name in sorted(contract.expected_names.intersection(samples)):
        sample = samples[name]
        state = states[name]

        first_sample = state.index < 0
        if first_sample:
            state.index = sample.index
            state.first_sample = sample
            if sample.flags["sleeping"] != 0 and contract.workload != "wall200":
                failures.add("initial_sleep", f"body={name}")
        elif state.index != sample.index:
            failures.add("body_index_changed", f"body={name} frame={frame} {state.index}->{sample.index}")

        if (
            state.last_sample is not None
            and sample.flags["sleepCounter"] > state.last_sample.flags["sleepCounter"]
            and not is_stable(sample, linear_speed, angular_speed, penetration)
        ):
            failures.add(
                "unstable_deactivation_advance",
                f"body={name} frame={frame} counter={sample.flags['sleepCounter']}",
            )

        state.history.append(sample)
        sleeping = sample.flags["sleeping"]
        if sleeping and not state.last_sleeping and not first_sample:
            state.transitions += 1
            history = list(state.history)
            if (
                sample.flags["sleepCounter"] < state.history.maxlen
                or not is_stable(sample, linear_speed, angular_speed, penetration)
            ):
                failures.add("incomplete_pre_sleep_window", f"body={name} frame={frame}")
            if sample.flags["islandCanSleep"] != 1:
                failures.add("transition_without_island_eligibility", f"body={name} frame={frame}")
            impulses = [item.values["maxImpulse"] for item in history]
            if impulses:
                impulse_span = max(impulses) - min(impulses)
                state.max_impulse_span = max(state.max_impulse_span, impulse_span)
            state.sleep_pose = sample.pose
        elif not sleeping and state.last_sleeping:
            state.wakes += 1
            if contract.forbid_wake:
                failures.add("late_wake", f"body={name} frame={frame}")
            state.sleep_pose = None
        elif sleeping and first_sample:
            state.sleep_pose = sample.pose

        if sleeping:
            state.final_sleep_run += 1
            if not sleeping_velocity_is_zero(sample):
                failures.add("hidden_sleep_velocity", f"body={name} frame={frame}")
            if state.sleep_pose is not None and sample.pose != state.sleep_pose:
                failures.add("sleep_pose_drift", f"body={name} frame={frame}")
        else:
            state.final_sleep_run = 0

        state.last_sleeping = sleeping
        state.last_sample = sample


def validate_scene_outcome(workload: str, states: dict[str, BodyState], failures: Failures) -> dict[str, object]:
    facts: dict[str, object] = {}

    if workload == "corner":
        state = states["sleep_leaning_corner_brick"]
        if state.first_sample is None or state.last_sample is None:
            return facts
        first = state.first_sample.values
        final = state.last_sample.values
        orientation_dot = abs(
            first["qX"] * final["qX"]
            + first["qY"] * final["qY"]
            + first["qZ"] * final["qZ"]
            + first["qW"] * final["qW"]
        )
        orientation_delta_degrees = math.degrees(2.0 * math.acos(max(0.0, min(1.0, orientation_dot))))
        translation = math.dist(
            (first["posX"], first["posY"], first["posZ"]),
            (final["posX"], final["posY"], final["posZ"]),
        )
        facts.update(
            {
                "cornerOrientationDeltaDegrees": orientation_delta_degrees,
                "cornerTranslation": translation,
                "cornerFinalQuaternionW": final["qW"],
            }
        )
        if orientation_delta_degrees < 10.0 or translation < 0.5:
            failures.add(
                "corner_did_not_topple",
                f"orientation_delta={orientation_delta_degrees:.3f} translation={translation:.3f}",
            )
        if abs(final["qW"]) < 0.99 or state.last_sleeping == 0:
            failures.add("corner_did_not_settle_flat", f"qW={final['qW']:.6f} sleeping={state.last_sleeping}")

    elif workload == "at_rest":
        final_samples = {name: state.last_sample for name, state in states.items()}
        if any(sample is None for sample in final_samples.values()):
            return facts
        ball_b_state = states["ball_b"]
        ball_b = final_samples["ball_b"]
        assert ball_b is not None and ball_b_state.first_sample is not None
        positions = {
            name: (sample.values["posX"], sample.values["posY"], sample.values["posZ"])
            for name, sample in final_samples.items()
            if sample is not None
        }
        distance_a = math.dist(positions["ball_b"], positions["ball_a"])
        distance_c = math.dist(positions["ball_b"], positions["ball_c"])
        initial_b = ball_b_state.first_sample.values
        rolled_distance = math.dist(
            (initial_b["posX"], initial_b["posY"], initial_b["posZ"]), positions["ball_b"]
        )
        vertical_span = max(position[1] for position in positions.values()) - min(
            position[1] for position in positions.values()
        )
        facts.update(
            {
                "ballBDistanceToA": distance_a,
                "ballBDistanceToC": distance_c,
                "ballBRolledDistance": rolled_distance,
                "finalVerticalSpan": vertical_span,
            }
        )
        if distance_a > 40.0 or distance_c > 40.0 or vertical_span > 2.0:
            failures.add(
                "balls_not_clustered_in_basin",
                f"distance_a={distance_a:.3f} distance_c={distance_c:.3f} vertical_span={vertical_span:.3f}",
            )
        if rolled_distance < 25.0:
            failures.add("ball_b_did_not_roll_off_slope", f"distance={rolled_distance:.3f}")
        if ball_b.flags["sleepInhibited"] != 0 or ball_b_state.last_sleeping == 0:
            failures.add(
                "ball_b_slept_on_slope",
                f"inhibited={ball_b.flags['sleepInhibited']} sleeping={ball_b_state.last_sleeping}",
            )

    elif workload == "wall200":
        final_asleep = [state for state in states.values() if state.last_sleeping != 0]
        final_awake = [state for state in states.values() if state.last_sleeping == 0 and state.last_sample is not None]
        if len(final_asleep) < 195:
            failures.add("wall_survivors_not_asleep", f"final_asleep={len(final_asleep)} required=195")
        for state in final_awake:
            assert state.last_sample is not None
            if state.last_sample.flags["sleepInhibited"] == 0:
                failures.add("noninhibited_wall_brick_awake", state.last_sample.name)

        stable_states = [
            state
            for state in final_asleep
            if state.last_sample is not None
            and state.final_sleep_run >= 300
            and abs(state.last_sample.values["qW"]) >= 0.95
            and state.wakes > 0
            and state.transitions > 0
        ]
        stable = [state.last_sample for state in stable_states if state.last_sample is not None]
        stable.sort(key=lambda sample: (sample.values["posY"], sample.name))
        chain_length: dict[str, int] = {}
        for upper in stable:
            best = 1
            for lower in stable:
                vertical = upper.values["posY"] - lower.values["posY"]
                if vertical > 3.5:
                    continue
                if vertical < 2.5:
                    break
                horizontal = math.hypot(
                    upper.values["posX"] - lower.values["posX"],
                    upper.values["posZ"] - lower.values["posZ"],
                )
                if horizontal <= 3.5:
                    best = max(best, chain_length.get(lower.name, 1) + 1)
            chain_length[upper.name] = best
        tallest_stack = max(chain_length.values(), default=0)
        facts.update(
            {
                "wallFinalAsleep": len(final_asleep),
                "wallFinalAwake": len(final_awake),
                "wallTallestStableStack": tallest_stack,
            }
        )
        if tallest_stack < 4:
            failures.add("wall_missing_four_high_stack", f"tallest={tallest_stack}")

    return facts


def evaluate_rows(
    rows: Iterable[dict[str, str]],
    contract: Contract,
    *,
    linear_speed: float = 0.5,
    angular_speed: float = 0.3,
    penetration: float = 0.055,
    deactivation_frames: int = 30,
) -> tuple[Failures, dict[str, object]]:
    failures = Failures()
    if deactivation_frames < 1:
        failures.add("invalid_deactivation_frames", str(deactivation_frames))
        deactivation_frames = 1
    states = {
        name: BodyState(history=deque(maxlen=deactivation_frames))
        for name in contract.expected_names
    }
    current_frame = -1
    frame_samples: dict[str, Sample] = {}

    for row in rows:
        try:
            frame = int(row["frame"])
        except (KeyError, TypeError, ValueError):
            failures.add("malformed_row", "row has no integer frame")
            continue

        if frame != current_frame:
            if current_frame >= 0:
                validate_frame(
                    current_frame,
                    frame_samples,
                    contract,
                    states,
                    failures,
                    linear_speed,
                    angular_speed,
                    penetration,
                )
            expected_frame = current_frame + 1
            if frame != expected_frame:
                failures.add("frame_gap", f"expected={expected_frame} actual={frame}")
            current_frame = frame
            frame_samples = {}

        name = row.get("name", "")
        if name not in contract.expected_names:
            continue
        if name in frame_samples:
            failures.add("duplicate_body_row", f"body={name} frame={frame}")
            continue
        sample = parse_sample(row, failures)
        if sample is not None:
            frame_samples[name] = sample

    if current_frame >= 0:
        validate_frame(
            current_frame,
            frame_samples,
            contract,
            states,
            failures,
            linear_speed,
            angular_speed,
            penetration,
        )
    observed_frames = current_frame + 1
    if contract.expected_frames > 0 and current_frame != contract.expected_frames - 1:
        failures.add("truncated_run", f"last_frame={current_frame} expected={contract.expected_frames - 1}")
    elif observed_frames < contract.minimum_frames:
        failures.add("truncated_run", f"frames={observed_frames} minimum={contract.minimum_frames}")

    final_awake: list[dict[str, object]] = []
    max_impulse_span = 0.0
    for name, state in sorted(states.items()):
        if state.last_sample is None:
            failures.add("missing_body", name)
            continue
        max_impulse_span = max(max_impulse_span, state.max_impulse_span)
        if contract.require_final_sleep and state.last_sleeping == 0:
            failures.add("final_awake", f"body={name}")
        if state.final_sleep_run < contract.final_sleep_tail:
            failures.add(
                "short_sleep_tail",
                f"body={name} tail={state.final_sleep_run} required={contract.final_sleep_tail}",
            )
        if state.last_sleeping == 0:
            sample = state.last_sample
            final_awake.append(
                {
                    "name": name,
                    "speed": sample.values["speed"],
                    "omega": sample.values["omegaMag"],
                    "inhibited": sample.flags["sleepInhibited"],
                    "resetReason": sample.flags["resetReason"],
                }
            )

    final_awake.sort(key=lambda item: (float(item["speed"]), str(item["name"])), reverse=True)
    facts: dict[str, object] = {
        "expectedBodies": len(contract.expected_names),
        "expectedFrames": contract.expected_frames if contract.expected_frames > 0 else f">={contract.minimum_frames}",
        "observedFrames": observed_frames,
        "finalAsleep": len(contract.expected_names) - len(final_awake),
        "finalAwake": final_awake[:10],
        "maxPreSleepImpulseSpan": max_impulse_span,
        "visualReviewRequired": contract.visual_review_required,
    }
    facts.update(validate_scene_outcome(contract.workload, states, failures))
    if contract.visual_review_required:
        failures.add("visual_review_pending", "fixed-camera scene shots and owner review are required")
    return failures, facts


def load_and_evaluate(path: Path, contract: Contract, args: argparse.Namespace) -> tuple[Failures, dict[str, object]]:
    failures = Failures()
    if not path.is_file():
        failures.add("missing_input", str(path))
        return failures, {}
    if args.min_mtime_ns is not None and path.stat().st_mtime_ns < args.min_mtime_ns:
        failures.add("stale_input", f"mtime_ns={path.stat().st_mtime_ns} required={args.min_mtime_ns}")
        return failures, {}

    with path.open("r", newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        missing_columns = sorted(set(REQUIRED_FIELDS).difference(reader.fieldnames or ()))
        if missing_columns:
            failures.add("missing_columns", ",".join(missing_columns))
            return failures, {}
        evaluated, facts = evaluate_rows(
            reader,
            contract,
            linear_speed=args.linear_speed,
            angular_speed=args.angular_speed,
            penetration=args.max_penetration,
            deactivation_frames=args.deactivation_frames,
        )
        return evaluated, facts


def sample_row(name: str, frame: int, *, sleeping: int, body_index: int) -> dict[str, str]:
    row = {key: "0" for key in REQUIRED_FIELDS}
    row.update(
        {
            "frame": str(frame),
            "idx": str(body_index),
            "name": name,
            "qW": "1.000000",
            "sleeping": str(sleeping),
            "sleepCounter": str(min(frame + 1, 30)),
            "islandRoot": "0",
            "bodyEligible": "1",
            "islandEligible": "1",
            "topologyStable": "1",
            "islandCanSleep": "1",
        }
    )
    return row


def planted_rows(contract: Contract) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for frame in range(contract.expected_frames):
        sleeping = int(frame >= 29)
        for body_index, name in enumerate(sorted(contract.expected_names)):
            rows.append(sample_row(name, frame, sleeping=sleeping, body_index=body_index))
    return rows


def run_self_test() -> None:
    small = Contract(
        "self-test", frozenset(("a", "b")), expected_frames=40, minimum_frames=40,
        final_sleep_tail=10, require_final_sleep=True
    )
    baseline = planted_rows(small)
    failures, _ = evaluate_rows(baseline, small)
    assert not failures, failures.rendered()

    stale_failures = Failures()
    observed_mtime_ns = 100
    required_mtime_ns = 101
    if observed_mtime_ns < required_mtime_ns:
        stale_failures.add("stale_input", "planted stale timestamp")
    assert "stale_input" in stale_failures.categories()

    controls: list[tuple[str, list[dict[str, str]], str]] = []
    initial_sleep = [dict(row) for row in baseline]
    initial_sleep[0]["sleeping"] = "1"
    controls.append(("initial", initial_sleep, "initial_sleep"))

    gap = [dict(row) for row in baseline if row["frame"] != "8"]
    controls.append(("gap", gap, "frame_gap"))

    truncated = [dict(row) for row in baseline if int(row["frame"]) < 35]
    controls.append(("truncated", truncated, "truncated_run"))

    drift = [dict(row) for row in baseline]
    next(row for row in drift if row["name"] == "a" and row["frame"] == "35")["posX"] = "0.0010"
    controls.append(("drift", drift, "sleep_pose_drift"))

    ineligible = [dict(row) for row in baseline]
    next(row for row in ineligible if row["name"] == "a" and row["frame"] == "29")["bodyEligible"] = "0"
    controls.append(("ineligible", ineligible, "incomplete_pre_sleep_window"))

    non_finite = [dict(row) for row in baseline]
    non_finite[10]["speed"] = "nan"
    controls.append(("nonfinite", non_finite, "non_finite"))

    penetration_residual = [dict(row) for row in baseline]
    next(row for row in penetration_residual if row["name"] == "a" and row["frame"] == "28")["maxPenetration"] = "0.055001"
    controls.append(("penetration-residual", penetration_residual, "unstable_deactivation_advance"))

    island_blocked = [dict(row) for row in baseline]
    next(row for row in island_blocked if row["name"] == "a" and row["frame"] == "29")["islandCanSleep"] = "0"
    controls.append(("island-blocked", island_blocked, "transition_without_island_eligibility"))

    wall = contract_for("wall200")
    short_wall = Contract(
        "self-test", wall.expected_names, expected_frames=40, minimum_frames=40,
        final_sleep_tail=10, require_final_sleep=True
    )
    one_awake = planted_rows(short_wall)
    awake_name = sorted(short_wall.expected_names)[-1]
    for row in one_awake:
        if row["name"] == awake_name:
            row["sleeping"] = "0"
            row["islandCanSleep"] = "0"
    controls.append(("199-of-200", one_awake, "final_awake"))

    for label, rows, expected_failure in controls:
        planted_failures, _ = evaluate_rows(rows, small if label != "199-of-200" else short_wall)
        assert expected_failure in planted_failures.categories(), (label, planted_failures.rendered())

    def planted_state(name: str, index: int, position: tuple[float, float, float]) -> BodyState:
        row = sample_row(name, 0, sleeping=1, body_index=index)
        row["posX"], row["posY"], row["posZ"] = (str(value) for value in position)
        sample_failures = Failures()
        sample = parse_sample(row, sample_failures)
        assert sample is not None and not sample_failures
        return BodyState(
            index=index,
            last_sleeping=1,
            first_sample=sample,
            last_sample=sample,
            final_sleep_run=600,
        )

    corner_states = {
        "sleep_ground_brick": planted_state("sleep_ground_brick", 0, (0.0, 0.0, 0.0)),
        "sleep_leaning_corner_brick": planted_state("sleep_leaning_corner_brick", 1, (0.0, 5.0, 0.0)),
    }
    corner_failures = Failures()
    validate_scene_outcome("corner", corner_states, corner_failures)
    assert "corner_did_not_topple" in corner_failures.categories()

    at_rest_states = {
        "ball_a": planted_state("ball_a", 0, (0.0, 0.0, 0.0)),
        "ball_b": planted_state("ball_b", 1, (100.0, 20.0, 100.0)),
        "ball_c": planted_state("ball_c", 2, (0.0, 0.0, 1.0)),
    }
    at_rest_failures = Failures()
    validate_scene_outcome("at_rest", at_rest_states, at_rest_failures)
    assert "balls_not_clustered_in_basin" in at_rest_failures.categories()

    flat_wall_states = {
        name: planted_state(name, index, (float(index), 1.5, 0.0))
        for index, name in enumerate(sorted(wall.expected_names))
    }
    wall_failures = Failures()
    validate_scene_outcome("wall200", flat_wall_states, wall_failures)
    assert "wall_missing_four_high_stack" in wall_failures.categories()

    print(
        "SELF_TEST_PASS: evidence rejects stale, gap, drift, excessive residual, false corner topple, "
        "unclustered balls, missing four-high wall, and 199/200 controls"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workload", choices=("corner", "edge", "wall200", "at_rest"))
    parser.add_argument("--input", type=Path)
    parser.add_argument("--linear-speed", type=float, default=0.5)
    parser.add_argument("--angular-speed", type=float, default=0.3)
    parser.add_argument("--max-penetration", type=float, default=0.055)
    parser.add_argument("--deactivation-frames", type=int, default=30)
    parser.add_argument("--min-mtime-ns", type=int)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        run_self_test()
        return 0
    if not args.workload or args.input is None:
        print("ERROR: --workload and --input are required unless --self-test is used", file=sys.stderr)
        return 2

    failures, facts = load_and_evaluate(args.input, contract_for(args.workload), args)
    result = {
        "workload": args.workload,
        "input": str(args.input),
        "passed": not failures,
        "failures": failures.rendered(),
        "facts": facts,
    }
    rendered = json.dumps(result, indent=2, sort_keys=True)
    print(rendered)
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(rendered + os.linesep, encoding="utf-8")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
