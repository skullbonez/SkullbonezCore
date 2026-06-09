"""
Compare physics CSV output against committed baselines.

Physics scenes use fixed_step + deterministic authored state, so output is exactly deterministic.
Any single differing byte is a real regression.

Exit 0 = all match, Exit 1 = regression detected or files missing.
"""
import os
import sys

REPO = os.environ.get("SKORE_REPO", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASELINE_DIR = os.path.join(REPO, "TestOutput", "baselines")

TESTS = [
    (os.path.join(REPO, "Debug", "physics_regression_solver.csv"), "physics_regression_solver.csv"),
    (os.path.join(REPO, "Debug", "bullet_sweep_wall.csv"), "bullet_sweep_wall.csv"),
    (os.path.join(REPO, "Debug", "bullet_sweep_object.csv"), "bullet_sweep_object.csv"),
    (os.path.join(REPO, "Debug", "bullet_sweep_terrain.csv"), "bullet_sweep_terrain.csv"),
    (os.path.join(REPO, "Debug", "shooting_reaction_volley.csv"), "shooting_reaction_volley.csv"),
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

        with open(output_path, "rb") as f:
            current = f.read()
        with open(baseline_path, "rb") as f:
            baseline = f.read()

        if current == baseline:
            line_count = current.count(b"\n")
            print(f"  PASS: {baseline_name} ({line_count} lines, byte-exact match)")
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
