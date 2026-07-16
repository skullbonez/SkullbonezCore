"""
File: tools/check_physics_simd_ab.py
Purpose:
  Compare scalar-OFF and SIMD-ON deterministic physics CSV artifacts without
  loading their potentially million-row contents into agent context.

Summary:
  The comparator streams corresponding rows, proves identity fields and row
  order match, rejects non-finite values, requires sleep/contact outcomes to
  remain identical, and reports the maximum absolute divergence for every
  continuous physics field. A caller-supplied tolerance is an explicit oracle,
  not a hidden baseline refresh.

Glossary:
  Identity field: Frame/body coordinate that must match exactly between runs.
  Outcome field: Discrete gameplay state whose equivalence is required.
  Continuous field: Floating-point state allowed to differ only within the
    declared A/B tolerance.

Invariants:
  - Inputs are consumed in lockstep; missing, reordered, or extra rows fail.
  - NaN and infinity always fail, even if both artifacts contain the same bit.
  - The script never rewrites either evidence artifact.

Related:
  - tools/validate_physics.bat
  - Agentic/Plans/TODO/physics-soa-simd-1000-bodies.md
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import tempfile
from pathlib import Path
from typing import TextIO


IDENTITY_FIELDS = ("frame", "idx", "name")
OUTCOME_FIELDS = ("grounded", "sleeping", "sleepInhibited")


class ComparisonFailure(RuntimeError):
    """Raised when the two artifacts violate the declared A/B contract."""


def _open_reader(path: Path) -> tuple[TextIO, csv.DictReader]:
    stream = path.open("r", encoding="utf-8", newline="")
    reader = csv.DictReader(stream)
    if reader.fieldnames is None:
        stream.close()
        raise ComparisonFailure(f"{path}: missing CSV header")
    return stream, reader


def compare(
    off_path: Path,
    on_path: Path,
    max_abs: float,
    expected_ticks: int | None,
    field_tolerances: dict[str, float] | None = None,
    max_final_outcome_count_delta: int = 0,
) -> dict[str, object]:
    off_stream, off_reader = _open_reader(off_path)
    on_stream, on_reader = _open_reader(on_path)
    try:
        if off_reader.fieldnames != on_reader.fieldnames:
            raise ComparisonFailure("CSV headers differ")

        fields = off_reader.fieldnames
        for required in (*IDENTITY_FIELDS, *OUTCOME_FIELDS):
            if required not in fields:
                raise ComparisonFailure(f"missing required field: {required}")
        continuous_fields = [field for field in fields if field not in (*IDENTITY_FIELDS, *OUTCOME_FIELDS)]
        maxima = {field: 0.0 for field in continuous_fields}
        maximum_rows = {field: 0 for field in continuous_fields}
        outcome_mismatches = {field: 0 for field in OUTCOME_FIELDS}
        final_outcome_mismatches = {field: 0 for field in OUTCOME_FIELDS}
        current_frame_outcome_mismatches = {field: 0 for field in OUTCOME_FIELDS}
        current_frame_off_counts = {field: 0 for field in OUTCOME_FIELDS}
        current_frame_on_counts = {field: 0 for field in OUTCOME_FIELDS}
        final_outcome_counts: list[dict[str, dict[str, int]]] = []
        first_outcome_mismatch: dict[str, dict[str, int]] = {}
        first_over_tolerance: dict[str, dict[str, int | float]] = {}
        row_count = 0
        session_count = 1
        final_frame = -1
        current_frame = -1

        while True:
            off_row = next(off_reader, None)
            on_row = next(on_reader, None)
            if off_row is None or on_row is None:
                if off_row is not None or on_row is not None:
                    raise ComparisonFailure(f"row counts differ after {row_count} matched rows")
                break
            off_repeated_header = all(off_row[field] == field for field in fields)
            on_repeated_header = all(on_row[field] == field for field in fields)
            if off_repeated_header or on_repeated_header:
                if off_repeated_header != on_repeated_header:
                    raise ComparisonFailure("repeated CSV headers are not synchronized")
                if expected_ticks is not None and final_frame + 1 != expected_ticks:
                    raise ComparisonFailure(
                        f"session {session_count} expected {expected_ticks} ticks but ended at frame {final_frame}"
                    )
                for field in OUTCOME_FIELDS:
                    final_outcome_mismatches[field] += current_frame_outcome_mismatches[field]
                    current_frame_outcome_mismatches[field] = 0
                final_outcome_counts.append(
                    {"off": dict(current_frame_off_counts), "on": dict(current_frame_on_counts)}
                )
                session_count += 1
                current_frame = -1
                final_frame = -1
                continue
            row_count += 1

            for field in IDENTITY_FIELDS:
                if off_row[field] != on_row[field]:
                    raise ComparisonFailure(
                        f"identity mismatch at row {row_count}: {field} {off_row[field]!r} != {on_row[field]!r}"
                    )
            final_frame = int(off_row["frame"])
            if final_frame != current_frame:
                current_frame = final_frame
                for field in OUTCOME_FIELDS:
                    current_frame_outcome_mismatches[field] = 0
                    current_frame_off_counts[field] = 0
                    current_frame_on_counts[field] = 0
            for field in OUTCOME_FIELDS:
                current_frame_off_counts[field] += int(off_row[field])
                current_frame_on_counts[field] += int(on_row[field])
                if off_row[field] != on_row[field]:
                    outcome_mismatches[field] += 1
                    current_frame_outcome_mismatches[field] += 1
                    first_outcome_mismatch.setdefault(
                        field,
                        {"row": row_count, "frame": final_frame, "idx": int(off_row["idx"])},
                    )

            for field in continuous_fields:
                off_value = float(off_row[field])
                on_value = float(on_row[field])
                if not math.isfinite(off_value) or not math.isfinite(on_value):
                    raise ComparisonFailure(f"non-finite {field} at row {row_count}")
                divergence = abs(off_value - on_value)
                if divergence > maxima[field]:
                    maxima[field] = divergence
                    maximum_rows[field] = row_count
                if divergence > (field_tolerances or {}).get(field, max_abs):
                    first_over_tolerance.setdefault(
                        field,
                        {"row": row_count, "frame": final_frame, "idx": int(off_row["idx"]), "value": divergence},
                    )

        if row_count == 0:
            raise ComparisonFailure("artifacts contain no data rows")
        if expected_ticks is not None and final_frame + 1 != expected_ticks:
            raise ComparisonFailure(
                f"expected {expected_ticks} ticks but final zero-based frame was {final_frame}"
            )
        for field in OUTCOME_FIELDS:
            final_outcome_mismatches[field] += current_frame_outcome_mismatches[field]
        final_outcome_counts.append(
            {"off": dict(current_frame_off_counts), "on": dict(current_frame_on_counts)}
        )
        mismatched_outcomes = {field: count for field, count in outcome_mismatches.items() if count != 0}
        effective_tolerances = {
            field: (field_tolerances or {}).get(field, max_abs) for field in continuous_fields
        }
        over_tolerance = {
            field: value for field, value in maxima.items() if value > effective_tolerances[field]
        }
        final_outcome_count_deltas = [
            {
                field: abs(session["off"][field] - session["on"][field])
                for field in OUTCOME_FIELDS
            }
            for session in final_outcome_counts
        ]
        outcome_count_over_tolerance = [
            {
                field: delta
                for field, delta in session.items()
                if delta > max_final_outcome_count_delta
            }
            for session in final_outcome_count_deltas
        ]
        result: dict[str, object] = {
            "ok": not over_tolerance and not any(outcome_count_over_tolerance),
            "rows": row_count,
            "ticks": final_frame + 1,
            "sessions": session_count,
            "max_abs_tolerance": max_abs,
            "field_tolerances": effective_tolerances,
            "max_abs_by_field": maxima,
            "max_abs_row_by_field": maximum_rows,
            "outcome_mismatches": outcome_mismatches,
            "final_frame_outcome_mismatches": final_outcome_mismatches,
            "final_frame_outcome_counts": final_outcome_counts,
            "final_frame_outcome_count_deltas": final_outcome_count_deltas,
            "max_final_outcome_count_delta": max_final_outcome_count_delta,
            "outcome_count_over_tolerance": outcome_count_over_tolerance,
            "first_outcome_mismatch": first_outcome_mismatch,
            "first_over_tolerance": first_over_tolerance,
            "over_tolerance": over_tolerance,
            "non_finite_values": 0,
        }
        return result
    finally:
        off_stream.close()
        on_stream.close()


def _write_fixture(path: Path, position: str, sleeping: str = "0") -> None:
    path.write_text(
        "frame,idx,name,posX,grounded,sleeping,sleepInhibited\n"
        f"0,0,body,{position},1,{sleeping},0\n",
        encoding="utf-8",
    )


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="physics-simd-ab-") as directory:
        root = Path(directory)
        off_path = root / "off.csv"
        on_path = root / "on.csv"
        _write_fixture(off_path, "1.0")
        _write_fixture(on_path, "1.0005")
        result = compare(off_path, on_path, 0.001, 1)
        assert result["ok"] is True
        assert result["max_abs_by_field"] == {"posX": 0.0004999999999999449}

        _write_fixture(on_path, "1.1")
        assert compare(off_path, on_path, 0.001, 1)["ok"] is False
        _write_fixture(on_path, "1.0", sleeping="1")
        assert compare(off_path, on_path, 0.001, 1)["ok"] is False
        _write_fixture(on_path, "nan")
        try:
            compare(off_path, on_path, 0.001, 1)
        except ComparisonFailure:
            pass
        else:
            raise AssertionError("non-finite fixture did not fail")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--off", type=Path)
    parser.add_argument("--on", type=Path)
    parser.add_argument("--max-abs", type=float, default=0.001)
    parser.add_argument(
        "--field-tolerance",
        action="append",
        default=[],
        metavar="FIELD=VALUE",
        help="override --max-abs for one continuous field; repeat as needed",
    )
    parser.add_argument("--max-final-outcome-count-delta", type=int, default=0)
    parser.add_argument("--expected-ticks", type=int)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        print("PASS: physics SIMD A/B comparator self-test")
        return 0
    if args.off is None or args.on is None:
        parser.error("--off and --on are required unless --self-test is used")
    if not math.isfinite(args.max_abs) or args.max_abs < 0.0:
        parser.error("--max-abs must be finite and non-negative")
    if args.max_final_outcome_count_delta < 0:
        parser.error("--max-final-outcome-count-delta must be non-negative")
    field_tolerances: dict[str, float] = {}
    for assignment in args.field_tolerance:
        try:
            field, rendered_value = assignment.split("=", 1)
            value = float(rendered_value)
        except ValueError:
            parser.error(f"invalid --field-tolerance {assignment!r}; expected FIELD=VALUE")
        if not field or not math.isfinite(value) or value < 0.0:
            parser.error(f"invalid --field-tolerance {assignment!r}")
        field_tolerances[field] = value

    try:
        result = compare(
            args.off,
            args.on,
            args.max_abs,
            args.expected_ticks,
            field_tolerances,
            args.max_final_outcome_count_delta,
        )
    except (ComparisonFailure, OSError, ValueError) as error:
        print(json.dumps({"ok": False, "error": str(error)}, indent=2))
        return 1
    rendered = json.dumps(result, indent=2, sort_keys=True)
    print(rendered)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
