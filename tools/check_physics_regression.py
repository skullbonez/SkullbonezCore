# Compare generated Physics CSVs byte-for-byte and report one bounded first
# divergence. Baseline updates belong to update_baselines.py, never this tool.
"""
Compare physics CSV output against committed baselines.

By default this checks the authored varied-scene baseline used by the cheap physics gate.
Pass --deep to include the opt-in bullet sweep and shooting CSV baselines.
Physics scenes use fixed_step + deterministic authored state, so output is
exactly deterministic. Any single differing byte is a real regression.

Exit 0 = all match, Exit 1 = regression detected or files missing.
"""
import csv
import hashlib
import io
import os
import sys

REPO = os.environ.get("SKORE_REPO", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASELINE_DIR = os.path.join(REPO, "TestOutput", "baselines")

CORE_TESTS = [
    (os.path.join(REPO, "Debug", "physics_regression_varied.csv"), "physics_regression_varied.csv"),
]

WORKER_MATRIX_TESTS = [
    (os.path.join(REPO, "Debug", "physics_regression_varied.csv"), "workers=0 primary"),
    (os.path.join(REPO, "Debug", "physics_regression_varied_workers_0_repeat.csv"), "workers=0 repeat"),
    (os.path.join(REPO, "Debug", "physics_regression_varied_workers_1.csv"), "workers=1"),
    (os.path.join(REPO, "Debug", "physics_regression_varied_workers_4.csv"), "workers=4"),
]

DEEP_TESTS = [
    *CORE_TESTS,
    (os.path.join(REPO, "Debug", "bullet_sweep_wall.csv"), "bullet_sweep_wall.csv"),
    (os.path.join(REPO, "Debug", "bullet_sweep_object.csv"), "bullet_sweep_object.csv"),
    (os.path.join(REPO, "Debug", "bullet_sweep_terrain.csv"), "bullet_sweep_terrain.csv"),
    (os.path.join(REPO, "Debug", "shooting_reaction_volley.csv"), "shooting_reaction_volley.csv"),
    (os.path.join(REPO, "Debug", "space_three_body_chaos.csv"), "space_three_body_chaos.csv"),
]


def _first_csv_difference(baseline, current):
    """Return the first row's frame, body, and changed fields without a full diff."""
    baseline_rows = csv.DictReader(io.StringIO(baseline.decode("utf-8")))
    current_rows = csv.DictReader(io.StringIO(current.decode("utf-8")))
    if baseline_rows.fieldnames != current_rows.fieldnames or not baseline_rows.fieldnames:
        return None

    row_number = 1
    while True:
        baseline_row = next(baseline_rows, None)
        current_row = next(current_rows, None)
        if baseline_row is None or current_row is None:
            if baseline_row == current_row:
                return None
            return {
                "row": row_number,
                "frame": (baseline_row or current_row or {}).get("frame", "missing"),
                "body": (baseline_row or current_row or {}).get("idx", "missing"),
                "name": (baseline_row or current_row or {}).get("name", "missing"),
                "fields": [("row", "present" if baseline_row else "missing", "present" if current_row else "missing", None)],
            }
        if baseline_row != current_row:
            fields = []
            ordered_fields = list(baseline_rows.fieldnames)
            if None in baseline_row or None in current_row:
                ordered_fields.append(None)
            for field in ordered_fields:
                old = baseline_row.get(field, "")
                new = current_row.get(field, "")
                if old == new:
                    continue
                delta = None
                try:
                    delta = float(new) - float(old)
                except (TypeError, ValueError):
                    pass
                fields.append((field or "extra_columns", old, new, delta))
            return {
                "row": row_number,
                "frame": current_row.get("frame", baseline_row.get("frame", "unknown")),
                "body": current_row.get("idx", baseline_row.get("idx", "unknown")),
                "name": current_row.get("name", baseline_row.get("name", "unknown")),
                "fields": fields,
            }
        row_number += 1


def first_csv_difference(baseline, current):
    try:
        return _first_csv_difference(baseline, current)
    except (UnicodeDecodeError, csv.Error) as exc:
        return {"parse_error": bounded_value(str(exc))}


def bounded_value(value, limit=120):
    escaped = repr(value)
    if len(escaped) <= limit:
        return escaped
    return f"{escaped[: limit - 18]}...<{len(escaped)} chars>"


def print_first_csv_difference(baseline_name, baseline, current):
    difference = first_csv_difference(baseline, current)
    if difference is None:
        print(f"  FAIL: {baseline_name} byte mismatch outside parsed CSV rows; check header or newline encoding.")
        return
    if "parse_error" in difference:
        print(f"  FAIL: {baseline_name} CSV parse error: {difference['parse_error']}")
        return
    print(
        f"  FAIL: {baseline_name} first difference: frame={bounded_value(difference['frame'])} "
        f"body_id={bounded_value(difference['body'])} name={bounded_value(difference['name'])} "
        f"csv_row={difference['row']}"
    )
    for field, old, new, delta in difference["fields"][:8]:
        delta_text = f" delta={delta:+.9g}" if delta is not None else ""
        print(
            f"    {bounded_value(field)}: baseline={bounded_value(old)} "
            f"current={bounded_value(new)}{delta_text}"
        )


def canonical_complete_run(data, artifact_name):
    """Collapse repeated byte-identical CSV runs while rejecting divergent passes."""
    # Invariant: one committed baseline represents one complete deterministic
    # playback. A runtime may reopen the log and append another complete pass,
    # but validation accepts that only when every byte of every pass agrees.
    first_newline = data.find(b"\n")
    if first_newline < 0:
        return data, 1

    header = data[: first_newline + 1]
    starts = [0]
    next_start = data.find(header, len(header))
    while next_start >= 0:
        starts.append(next_start)
        next_start = data.find(header, next_start + len(header))

    if len(starts) == 1:
        return data, 1

    runs = [data[start:end] for start, end in zip(starts, starts[1:] + [len(data)])]
    if any(run != runs[0] for run in runs[1:]):
        raise ValueError(f"{artifact_name} emitted {len(runs)} complete CSV runs that are not byte-identical")
    return runs[0], len(runs)


def compare_worker_matrix_payloads(outputs, baseline_data):
    """Return canonical worker rows and bounded byte-exact comparison failures."""
    failures = []
    rows = []

    try:
        baseline, baseline_run_count = canonical_complete_run(baseline_data, "physics_regression_varied.csv")
    except ValueError as exc:
        return rows, 0, [f"committed baseline is invalid: {exc}"]

    for label, data in outputs:
        try:
            current, run_count = canonical_complete_run(data, label)
        except ValueError as exc:
            failures.append(str(exc))
            continue
        rows.append((label, current, run_count))

    if len(rows) != len(outputs):
        return rows, baseline_run_count, failures

    reference_label, reference, _ = rows[0]
    for label, current, _ in rows:
        if current != reference:
            failures.append(f"{label} differs byte-for-byte from {reference_label}")
        if current != baseline:
            failures.append(f"{label} differs byte-for-byte from the committed baseline")

    return rows, baseline_run_count, failures


def first_worker_matrix_difference(rows, baseline_data):
    try:
        baseline, _ = canonical_complete_run(baseline_data, "physics_regression_varied.csv")
    except ValueError:
        return None
    for label, current, _ in rows:
        if current != baseline:
            return f"committed baseline vs {label}", baseline, current
    if rows:
        reference_label, reference, _ = rows[0]
        for label, current, _ in rows[1:]:
            if current != reference:
                return f"{reference_label} vs {label}", reference, current
    return None


def run_worker_matrix():
    baseline_path = os.path.join(BASELINE_DIR, "physics_regression_varied.csv")
    if not os.path.exists(baseline_path):
        print("  FAIL: missing committed baseline physics_regression_varied.csv")
        return 1

    outputs = []
    missing = False
    for output_path, label in WORKER_MATRIX_TESTS:
        if not os.path.exists(output_path):
            print(f"  FAIL: {os.path.basename(output_path)} not produced ({label})")
            missing = True
            continue
        with open(output_path, "rb") as stream:
            outputs.append((label, stream.read()))

    if missing:
        return 1

    with open(baseline_path, "rb") as stream:
        baseline_data = stream.read()

    rows, baseline_run_count, failures = compare_worker_matrix_payloads(outputs, baseline_data)
    if failures:
        for failure in failures[:8]:
            print(f"  FAIL: {failure}")
        diagnostic = first_worker_matrix_difference(rows, baseline_data)
        if diagnostic:
            label, expected, current = diagnostic
            print_first_csv_difference(label, expected, current)
        return 1

    canonical = rows[0][1]
    digest = hashlib.sha256(canonical).hexdigest()
    line_count = canonical.count(b"\n")
    run_summary = ", ".join(f"{label}:runs={run_count}" for label, _, run_count in rows)
    print(
        f"  PASS: clean-process worker matrix ({line_count} lines, sha256={digest}; "
        f"baseline runs={baseline_run_count}; {run_summary})"
    )
    return 0


def run_self_test():
    complete = b"frame,value\n0,alpha\n1,beta\n"
    identical_outputs = [
        ("workers=0 primary", complete),
        ("workers=0 repeat", complete + complete),
        ("workers=1", complete),
        ("workers=4", complete),
    ]
    rows, _, failures = compare_worker_matrix_payloads(identical_outputs, complete)
    if failures or len(rows) != len(identical_outputs):
        raise RuntimeError("self-test rejected an identical clean-process worker matrix")

    mutated_outputs = list(identical_outputs)
    mutated_outputs[-1] = ("workers=4", b"frame,value\n0,alpha\n1,gamma\n")
    _, _, failures = compare_worker_matrix_payloads(mutated_outputs, complete)
    if not any("workers=4 differs byte-for-byte" in failure for failure in failures):
        raise RuntimeError("self-test accepted a participating worker payload mutation")
    mutated_rows, _, _ = compare_worker_matrix_payloads(mutated_outputs, complete)
    worker_diagnostic = first_worker_matrix_difference(mutated_rows, complete)
    if not worker_diagnostic or "workers=4" not in worker_diagnostic[0]:
        raise RuntimeError("self-test did not select the divergent worker payload")
    worker_difference = first_csv_difference(worker_diagnostic[1], worker_diagnostic[2])
    if not worker_difference or worker_difference["frame"] != "1":
        raise RuntimeError("self-test worker diagnostic did not report the first divergent frame")

    divergent_repeat = complete + b"frame,value\n0,alpha\n1,gamma\n"
    divergent_outputs = list(identical_outputs)
    divergent_outputs[1] = ("workers=0 repeat", divergent_repeat)
    _, _, failures = compare_worker_matrix_payloads(divergent_outputs, complete)
    if not any("emitted 2 complete CSV runs that are not byte-identical" in failure for failure in failures):
        raise RuntimeError("self-test accepted divergent appended runs")

    baseline_csv = b"frame,idx,name,posX,sleeping\n7,12,ball,1.25,0\n"
    current_csv = b"frame,idx,name,posX,sleeping\n7,12,ball,1.50,1\n"
    difference = first_csv_difference(baseline_csv, current_csv)
    if not difference or difference["frame"] != "7" or difference["body"] != "12":
        raise RuntimeError("self-test did not identify the first differing frame and body")
    fields = {field: (old, new, delta) for field, old, new, delta in difference["fields"]}
    if fields.get("posX") != ("1.25", "1.50", 0.25) or fields.get("sleeping") != ("0", "1", 1.0):
        raise RuntimeError("self-test did not report exact metric deltas")

    malformed_csv = b"frame,idx,name,posX,sleeping\n7,12,ball,1.50\n"
    malformed_difference = first_csv_difference(baseline_csv, malformed_csv)
    if not malformed_difference or not any(
        field == "sleeping" and new is None
        for field, _, new, _ in malformed_difference["fields"]
    ):
        raise RuntimeError("self-test did not bound a truncated CSV row")

    oversized = "x" * 10_000
    if len(bounded_value(oversized)) > 120:
        raise RuntimeError("self-test emitted an unbounded CSV cell")
    original_limit = csv.field_size_limit()
    try:
        csv.field_size_limit(3)
        parse_failure = first_csv_difference(baseline_csv, current_csv)
    finally:
        csv.field_size_limit(original_limit)
    if not parse_failure or "parse_error" not in parse_failure:
        raise RuntimeError("self-test did not bound a CSV parser failure")

    print("PASS: physics regression comparator self-tests")
    return 0


def main():
    args = sys.argv[1:]
    if args == ["--self-test"]:
        return run_self_test()
    if args == ["--worker-matrix"]:
        return run_worker_matrix()
    if args not in ([], ["--deep"]):
        print("usage: check_physics_regression.py [--deep | --worker-matrix | --self-test]")
        if "--update" in args:
            print(
                "Physics-plan baseline updates use the single guarded workflow: "
                "python tools/update_baselines.py --physics"
            )
        return 2

    deep = args == ["--deep"]

    tests = DEEP_TESTS if deep else CORE_TESTS

    if deep:
        print("  Checking deep physics regression baselines...")
    else:
        print("  Checking core physics regression baseline...")

    all_pass = True

    for output_path, baseline_name in tests:
        baseline_path = os.path.join(BASELINE_DIR, baseline_name)

        if not os.path.exists(output_path):
            print(f"  FAIL: {os.path.basename(output_path)} not produced")
            all_pass = False
            continue

        if not os.path.exists(baseline_path):
            print(f"  FAIL: missing committed baseline {baseline_name}")
            all_pass = False
            continue

        with open(output_path, "rb") as f:
            try:
                current, run_count = canonical_complete_run(f.read(), baseline_name)
            except ValueError as exc:
                print(f"  FAIL: {exc}")
                all_pass = False
                continue
        with open(baseline_path, "rb") as f:
            try:
                baseline, baseline_run_count = canonical_complete_run(f.read(), baseline_name)
            except ValueError as exc:
                print(f"  FAIL: committed baseline is invalid: {exc}")
                all_pass = False
                continue

        if current == baseline:
            line_count = current.count(b"\n")
            print(
                f"  PASS: {baseline_name} ({line_count} lines, byte-exact match; "
                f"output runs={run_count}, baseline runs={baseline_run_count})"
            )
        else:
            all_pass = False
            current_line_count = current.count(b"\n")
            baseline_line_count = baseline.count(b"\n")
            if current_line_count != baseline_line_count:
                print(f"  FAIL: {baseline_name} row count {current_line_count} vs baseline {baseline_line_count}")
            print_first_csv_difference(baseline_name, baseline, current)

    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
