"""Validate that the prediction smoke produced a causal future tree in DX12.

The interaction report proves that the selected root published contact-derived
incoming/outgoing child lanes and enough submitted ribbon segments to exclude
the harness scene's root-only presentation. A same-run screenshot pair proves
that a connected path-sized change appeared in the world viewport rather than
only in UI chrome. Focused C++ tests separately prove retained child markers
populate the renderer's priority line and ribbon buffers. No golden image is
required.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw


MIN_PUBLISHED_POINTS = 2
MIN_SUBMITTED_SEGMENTS = 1
# The deterministic scene's selected root produces 17 sampled segments. Its
# collision children lift the submission well above this boundary (currently
# 305), so a semantic-only tree or a root-only renderer cannot pass.
MIN_CAUSAL_TREE_SEGMENTS = 32
MIN_NEW_PATH_PIXELS = 8
MIN_PATH_SPAN_PIXELS = 24


class CheckFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class RasterEvidence:
    new_path_pixels: int
    horizontal_span: int
    vertical_span: int


def validate_report(report: dict[str, Any]) -> tuple[int, int, int, int]:
    if report.get("ok") is not True:
        raise CheckFailure("interaction report did not finish with ok=true")

    final = report.get("finalState")
    if not isinstance(final, dict):
        raise CheckFailure("interaction report has no finalState object")

    target_id = final.get("replayPathTargetId")
    if not isinstance(target_id, int) or target_id <= 0:
        raise CheckFailure("finalState has no valid replayPathTargetId")

    show_all_future_paths = final.get("predictionDrawListShowAllFuturePaths")
    if show_all_future_paths is not False:
        raise CheckFailure(
            "prediction smoke must exercise selected-causal presentation: "
            f"predictionDrawListShowAllFuturePaths={show_all_future_paths!r}"
        )

    records = final.get("predictionTrajectoryRecords")
    if not isinstance(records, list):
        raise CheckFailure("finalState has no predictionTrajectoryRecords array")

    future_points = 0
    unexpected_future_roots: list[int] = []
    contact_child_lanes: set[str] = set()
    for record in records:
        if not isinstance(record, dict):
            continue

        lane = record.get("lane")
        if lane in {"FutureChildIncoming", "FutureChildOutgoing"}:
            child_points = record.get("publishedPointCount")
            if record.get("contactDerived") is True and isinstance(child_points, int) and child_points >= 2:
                contact_child_lanes.add(lane)

        if lane != "FutureRoot":
            continue

        body_id = record.get("bodyId")
        if body_id != target_id:
            if isinstance(body_id, int):
                unexpected_future_roots.append(body_id)
            continue

        if body_id == target_id:
            points = record.get("publishedPointCount")
            if isinstance(points, int):
                future_points = max(future_points, points)

    if unexpected_future_roots:
        raise CheckFailure(
            "selected-causal presentation published unrelated FutureRoot trajectories: "
            f"targetId={target_id} otherBodyIds={sorted(set(unexpected_future_roots))}"
        )

    if future_points < MIN_PUBLISHED_POINTS:
        lane_counts: dict[str, int] = {}
        for record in records:
            if isinstance(record, dict) and isinstance(record.get("lane"), str):
                lane = record["lane"]
                lane_counts[lane] = lane_counts.get(lane, 0) + 1
        raise CheckFailure(
            "selected target has no drawable FutureRoot trajectory: "
            f"targetId={target_id} futurePoints={future_points} lanes={lane_counts}"
        )

    required_child_lanes = {"FutureChildIncoming", "FutureChildOutgoing"}
    if contact_child_lanes != required_child_lanes:
        raise CheckFailure(
            "selected future did not publish its contact-derived child pair: "
            f"present={sorted(contact_child_lanes)} required={sorted(required_child_lanes)}"
        )

    segment_count = final.get("predictionTrajectorySubmissionSegmentCount")
    vertex_count = final.get("predictionTrajectorySubmissionVertexCount")
    if not isinstance(segment_count, int) or segment_count < MIN_SUBMITTED_SEGMENTS:
        raise CheckFailure(f"no replay ribbon segments reached submission: segments={segment_count!r}")
    if not isinstance(vertex_count, int) or vertex_count < segment_count * 6:
        raise CheckFailure(
            "submitted replay ribbon vertex count is inconsistent: "
            f"segments={segment_count} vertices={vertex_count!r}"
        )

    if segment_count < MIN_CAUSAL_TREE_SEGMENTS:
        raise CheckFailure(
            "contact-derived child paths did not reach DX12 ribbon submission: "
            f"segments={segment_count} required={MIN_CAUSAL_TREE_SEGMENTS}"
        )

    entry_marker_count = final.get("predictionRetainedEntryMarkerCount")
    if not isinstance(entry_marker_count, int) or entry_marker_count < 1:
        raise CheckFailure(f"no collided-child entry wireframe was retained: markers={entry_marker_count!r}")

    return target_id, future_points, segment_count, len(contact_child_lanes)


def longest_connected_axis_run(coordinates: set[tuple[int, int]], horizontal: bool) -> int:
    grouped: dict[int, list[int]] = {}
    for x, y in coordinates:
        primary, secondary = (x, y) if horizontal else (y, x)
        grouped.setdefault(primary, []).append(secondary)

    previous_primary: int | None = None
    previous_runs: dict[int, int] = {}
    longest = 0
    for primary in sorted(grouped):
        if previous_primary is None or primary != previous_primary + 1:
            previous_runs = {}

        current_runs: dict[int, int] = {}
        for secondary in grouped[primary]:
            run = 1 + max((previous_runs.get(neighbor, 0) for neighbor in range(secondary - 2, secondary + 3)), default=0)
            current_runs[secondary] = run
            longest = max(longest, run)

        previous_primary = primary
        previous_runs = current_runs

    return longest


def validate_raster(before: Image.Image, after: Image.Image) -> RasterEvidence:
    before_rgb = before.convert("RGB")
    after_rgb = after.convert("RGB")
    if before_rgb.size != after_rgb.size:
        raise CheckFailure(f"screenshot dimensions differ: before={before_rgb.size} after={after_rgb.size}")

    width, height = after_rgb.size
    # The rightmost 24% and bottom 8% contain UI chrome. The remaining viewport
    # contains the world-space path and no operator-panel borders.
    left = 0
    top = int(height * 0.08)
    right = int(width * 0.76)
    bottom = int(height * 0.92)
    before_pixels = before_rgb.load()
    after_pixels = after_rgb.load()
    coordinates: set[tuple[int, int]] = set()

    for y in range(top, bottom):
        for x in range(left, right):
            old = before_pixels[x, y]
            new = after_pixels[x, y]
            changed = max(abs(new[channel] - old[channel]) for channel in range(3)) >= 8
            if changed:
                coordinates.add((x, y))

    if len(coordinates) < MIN_NEW_PATH_PIXELS:
        raise CheckFailure(
            "enough changed pixels did not appear in the world viewport: "
            f"newPixels={len(coordinates)} required={MIN_NEW_PATH_PIXELS}"
        )

    # Palette is presentation-owned and changes with lighting. A future ribbon
    # is instead distinguished from a moving object's before/after blobs by a
    # continuous thin run through adjacent columns or rows.
    horizontal_span = longest_connected_axis_run(coordinates, horizontal=True)
    vertical_span = longest_connected_axis_run(coordinates, horizontal=False)
    if max(horizontal_span, vertical_span) < MIN_PATH_SPAN_PIXELS:
        raise CheckFailure(
            "new future-colour pixels do not form a path-sized feature: "
            f"horizontalSpan={horizontal_span} verticalSpan={vertical_span} "
            f"required={MIN_PATH_SPAN_PIXELS}"
        )

    return RasterEvidence(len(coordinates), horizontal_span, vertical_span)


def valid_report_fixture() -> dict[str, Any]:
    return {
        "ok": True,
        "finalState": {
            "replayPathTargetId": 7,
            "predictionDrawListShowAllFuturePaths": False,
            "predictionTrajectoryRecords": [
                {"lane": "PastRoot", "bodyId": 7, "publishedPointCount": 3},
                {"lane": "FutureRoot", "bodyId": 7, "publishedPointCount": 12},
                {
                    "lane": "FutureChildIncoming",
                    "bodyId": 9,
                    "publishedPointCount": 4,
                    "contactDerived": True,
                },
                {
                    "lane": "FutureChildOutgoing",
                    "bodyId": 9,
                    "publishedPointCount": 8,
                    "contactDerived": True,
                },
            ],
            "predictionTrajectorySubmissionSegmentCount": 40,
            "predictionTrajectorySubmissionVertexCount": 240,
            "predictionRetainedEntryMarkerCount": 1,
        },
    }


def expect_failure(operation: object, label: str) -> None:
    try:
        operation()  # type: ignore[operator]
    except CheckFailure:
        return
    raise AssertionError(f"negative control unexpectedly passed: {label}")


def run_self_test() -> None:
    validate_report(valid_report_fixture())

    past_only = valid_report_fixture()
    past_only["finalState"]["predictionTrajectoryRecords"] = [
        {"lane": "PastRoot", "bodyId": 7, "publishedPointCount": 12}
    ]
    expect_failure(lambda: validate_report(past_only), "past-only report")

    root_only = valid_report_fixture()
    root_only["finalState"]["predictionTrajectoryRecords"] = [
        {"lane": "FutureRoot", "bodyId": 7, "publishedPointCount": 12}
    ]
    expect_failure(lambda: validate_report(root_only), "root without contact-derived children")

    show_all = valid_report_fixture()
    show_all["finalState"]["predictionDrawListShowAllFuturePaths"] = True
    expect_failure(lambda: validate_report(show_all), "show-all future-path mode")

    unrelated_root = valid_report_fixture()
    unrelated_root["finalState"]["predictionTrajectoryRecords"].append(
        {"lane": "FutureRoot", "bodyId": 11, "publishedPointCount": 8}
    )
    expect_failure(lambda: validate_report(unrelated_root), "unrelated FutureRoot")

    root_only_submission = valid_report_fixture()
    root_only_submission["finalState"]["predictionTrajectorySubmissionSegmentCount"] = 17
    root_only_submission["finalState"]["predictionTrajectorySubmissionVertexCount"] = 102
    expect_failure(lambda: validate_report(root_only_submission), "semantic children without submitted causal paths")

    before = Image.new("RGB", (320, 180), (55, 12, 65))
    after = before.copy()
    ImageDraw.Draw(after).line((30, 120, 210, 65), fill=(82, 178, 163), width=2)
    validate_raster(before, after)
    expect_failure(lambda: validate_raster(before, before), "identical screenshots")

    ui_only = before.copy()
    ImageDraw.Draw(ui_only).line((275, 20, 275, 155), fill=(82, 178, 163), width=2)
    expect_failure(lambda: validate_raster(before, ui_only), "UI-only cyan change")

    moving_blob = before.copy()
    moving_blob_draw = ImageDraw.Draw(moving_blob)
    moving_blob_draw.rectangle((30, 90, 39, 99), fill=(210, 80, 20))
    moving_blob_draw.rectangle((180, 90, 189, 99), fill=(210, 80, 20))
    expect_failure(lambda: validate_raster(before, moving_blob), "disconnected moving-object blobs")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--before", type=Path)
    parser.add_argument("--after", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.self_test:
            run_self_test()
            print("PASS: prediction future-render checker self-tests")
            return 0

        if args.report is None or args.before is None or args.after is None:
            raise CheckFailure("--report, --before, and --after are required")
        for path in (args.report, args.before, args.after):
            if not path.is_file():
                raise CheckFailure(f"required evidence is missing: {path}")

        report = json.loads(args.report.read_text(encoding="utf-8"))
        target_id, future_points, segments, child_lanes = validate_report(report)
        with Image.open(args.before) as before, Image.open(args.after) as after:
            raster = validate_raster(before, after)

        print(
            "PASS: selected FutureRoot reached DX12 raster "
            f"targetId={target_id} futurePoints={future_points} contactChildLanes={child_lanes} "
            f"submittedSegments={segments} "
            f"newPathPixels={raster.new_path_pixels} "
            f"span={raster.horizontal_span}x{raster.vertical_span}"
        )
        return 0
    except (CheckFailure, json.JSONDecodeError, OSError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
