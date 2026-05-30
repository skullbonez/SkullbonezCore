"""
Compare physics CSV output against committed baselines.

Physics scenes use fixed_step + seed 42, so output is exactly deterministic.
Any single differing byte is a real regression.

Exit 0 = all match, Exit 1 = regression detected or files missing.
"""
import os
import sys

REPO = os.environ.get("SKORE_REPO", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASELINE_DIR = os.path.join(REPO, "TestOutput", "baselines")

TESTS = [
    (os.path.join(REPO, "Debug", "physics_regression_solver.csv"), "physics_regression_solver.csv"),
]


def main():
    all_pass = True

    for output_path, baseline_name in TESTS:
        baseline_path = os.path.join(BASELINE_DIR, baseline_name)

        if not os.path.exists(output_path):
            print(f"  FAIL: {os.path.basename(output_path)} not produced")
            all_pass = False
            continue

        if not os.path.exists(baseline_path):
            import shutil

            shutil.copy(output_path, baseline_path)
            with open(output_path) as f:
                lines = f.readlines()
            print(f"  BASELINE CREATED: {baseline_name} ({len(lines)} lines)")
            continue

        with open(output_path) as f:
            current = f.readlines()
        with open(baseline_path) as f:
            baseline = f.readlines()

        if current == baseline:
            print(f"  PASS: {baseline_name} ({len(current)} lines, exact match)")
        else:
            all_pass = False
            if len(current) != len(baseline):
                print(f"  FAIL: {baseline_name} row count {len(current)} vs baseline {len(baseline)}")
            else:
                diffs = [
                    (i + 1, b.rstrip(), c.rstrip())
                    for i, (b, c) in enumerate(zip(baseline, current))
                    if b != c
                ]
                print(f"  FAIL: {baseline_name} - {len(diffs)} lines differ (first at line {diffs[0][0]}):")
                for lineno, b, c in diffs[:5]:
                    print(f"    line {lineno}:")
                    print(f"      baseline: {b}")
                    print(f"      current:  {c}")

    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
