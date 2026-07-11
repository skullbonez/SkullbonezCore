#
# File: tools/check_physics_regression.py
# Purpose:
#   Documents and runs the check_physics_regression.py developer/validation helper script.
#
# Mental model:
#   Tools are command-line guardrails around builds, validation, screenshots,
#   diagnostics, and artifact handling. They make the safe path repeatable and
#   keep output bounded for humans and agents.
#
# Glossary:
#   CSV (Comma-Separated Values): Text table format used for byte-exact physics
#   regression output.
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
Compare physics CSV output against committed baselines.

By default this checks the authored varied-scene baseline used by the cheap physics gate.
Pass --deep to include the opt-in bullet sweep and shooting CSV baselines.
Physics scenes use fixed_step + deterministic authored state, so output is
exactly deterministic. Any single differing byte is a real regression.

Exit 0 = all match, Exit 1 = regression detected or files missing.
"""
import os
import sys

REPO = os.environ.get("SKORE_REPO", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASELINE_DIR = os.path.join(REPO, "TestOutput", "baselines")

CORE_TESTS = [
    (os.path.join(REPO, "Debug", "physics_regression_varied.csv"), "physics_regression_varied.csv"),
]

DEEP_TESTS = [
    *CORE_TESTS,
    (os.path.join(REPO, "Debug", "bullet_sweep_wall.csv"), "bullet_sweep_wall.csv"),
    (os.path.join(REPO, "Debug", "bullet_sweep_object.csv"), "bullet_sweep_object.csv"),
    (os.path.join(REPO, "Debug", "bullet_sweep_terrain.csv"), "bullet_sweep_terrain.csv"),
    (os.path.join(REPO, "Debug", "shooting_reaction_volley.csv"), "shooting_reaction_volley.csv"),
    (os.path.join(REPO, "Debug", "space_three_body_chaos.csv"), "space_three_body_chaos.csv"),
]


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


def main():
    update = False
    deep = False
    for arg in sys.argv[1:]:
        if arg == "--update":
            update = True
        elif arg == "--deep":
            deep = True
        else:
            print("usage: check_physics_regression.py [--update] [--deep]")
            return 2

    tests = DEEP_TESTS if deep else CORE_TESTS

    if deep:
        print("  Checking deep physics regression baselines...")
    else:
        print("  Checking core physics regression baseline...")

    if update and not deep:
        print("  NOTE: updating only the core physics baseline. Use --deep to update the opt-in deep set.")

    if len(sys.argv) > 3:
        print("usage: check_physics_regression.py [--update] [--deep]")
        return 2

    all_pass = True

    for output_path, baseline_name in tests:
        baseline_path = os.path.join(BASELINE_DIR, baseline_name)

        if not os.path.exists(output_path):
            print(f"  FAIL: {os.path.basename(output_path)} not produced")
            all_pass = False
            continue

        if update:
            with open(output_path, "rb") as f:
                current, run_count = canonical_complete_run(f.read(), baseline_name)
            with open(baseline_path, "wb") as f:
                f.write(current)
            line_count = current.count(b"\n")
            print(f"  BASELINE UPDATED: {baseline_name} ({line_count} lines from {run_count} byte-identical run(s))")
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
            else:
                baseline_lines = baseline.splitlines()
                current_lines = current.splitlines()
                diffs = [(i + 1, b, c) for i, (b, c) in enumerate(zip(baseline_lines, current_lines)) if b != c]
                if not diffs:
                    print(f"  FAIL: {baseline_name} byte mismatch with identical text lines; check newline encoding.")
                    continue

                print(f"  FAIL: {baseline_name} - {len(diffs)} lines differ (first at line {diffs[0][0]}):")
                for lineno, b, c in diffs[:5]:
                    print(f"    line {lineno}:")
                    print(f"      baseline: {b.decode(errors='replace')}")
                    print(f"      current:  {c.decode(errors='replace')}")

    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
