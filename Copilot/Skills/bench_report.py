"""
Physics benchmark report — legacy solver vs impulse solver (4 modes).

Usage:
    py Copilot/Skills/bench_report.py [--out-dir <archive_dir>] [--previous <physics_bench.json>]

Reads from Profile/:
    legacy_bench_perf_log.csv         — legacy:  300 spheres
    solver_balls_bench_perf_log.csv   — solver:  300 balls
    solver_bench_perf_log.csv         — solver:  150 balls + 150 boxes
    solver_boxes_bench_perf_log.csv   — solver:  300 boxes

If --out-dir is given, writes physics_bench.json to that directory.
If --previous is given, appends a delta column showing change vs prior run.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from datetime import datetime, timezone

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

# CSV file for each mode (relative to Profile/)
MODES = [
    ("legacy_300balls",     "legacy_bench_perf_log.csv",       "Legacy 300b",      300,  0),
    ("solver_300balls",     "solver_balls_bench_perf_log.csv", "Solver 300b",       300,  0),
    ("solver_150b_150box",  "solver_bench_perf_log.csv",       "Solver 150b+150box",150, 150),
    ("solver_300boxes",     "solver_boxes_bench_perf_log.csv", "Solver 300box",       0, 300),
]

GREEN  = "\033[32m"
RED    = "\033[31m"
YELLOW = "\033[33m"
BLUE   = "\033[34m"
BOLD   = "\033[1m"
RESET  = "\033[0m"


def parse_physics_column(csv_path):
    """Return pass-2 Frame/Physics values (ms).  Falls back to pass-1 if no pass-2."""
    header = []
    physics_col = -1
    pass1, pass2 = [], []
    with open(csv_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("# MEM"):
                continue
            if line.startswith("pass,frame,"):
                cols = line.split(",")
                header = cols[2:]
                for i, name in enumerate(header):
                    if name == "Frame/Physics":
                        physics_col = i
                        break
                continue
            if line.startswith("#"):
                continue
            cols = line.split(",")
            if len(cols) < 3 or physics_col < 0:
                continue
            pass_num = int(cols[0])
            val = float(cols[2 + physics_col]) if (2 + physics_col) < len(cols) else 0.0
            (pass2 if pass_num >= 2 else pass1).append(val)
    return pass2 if pass2 else pass1


def pct(vals, p):
    if not vals:
        return 0.0
    s = sorted(vals)
    k = (len(s) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (k - lo) * (s[hi] - s[lo])


def stats(vals):
    """Return dict with avg/p50/p95/p99 (ms) and frame count."""
    if not vals:
        return {"avg": 0.0, "p50": 0.0, "p95": 0.0, "p99": 0.0, "frames": 0}
    return {
        "avg":    round(sum(vals) / len(vals), 4),
        "p50":    round(pct(vals, 50), 4),
        "p95":    round(pct(vals, 95), 4),
        "p99":    round(pct(vals, 99), 4),
        "frames": len(vals),
    }


def color_delta(pct_val):
    sign = "+" if pct_val > 0 else ""
    s = f"{sign}{pct_val:.1f}%"
    if abs(pct_val) < 5.0:
        return f"{BLUE}{s}{RESET}"
    return f"{GREEN}{s}{RESET}" if pct_val < 0 else (f"{YELLOW}{s}{RESET}" if pct_val < 20 else f"{RED}{s}{RESET}")


def delta_str(prev, cur):
    if prev is None or prev == 0:
        return f"{BLUE}  n/a{RESET}"
    return color_delta((cur - prev) / prev * 100.0)


def get_commit():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True,
                              cwd=str(REPO_ROOT)).stdout.strip()
    except Exception:
        return "unknown"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir",   default="",  help="Archive directory; write physics_bench.json here")
    ap.add_argument("--previous",  default="",  help="Path to previous physics_bench.json for delta comparison")
    args = ap.parse_args()

    profile_dir = REPO_ROOT / "Profile"
    prev_data = {}
    if args.previous and Path(args.previous).exists():
        with open(args.previous) as f:
            prev_data = json.load(f).get("modes", {})

    # Collect results for each mode
    results = {}
    missing = []
    for key, csv_file, label, balls, boxes in MODES:
        csv_path = profile_dir / csv_file
        if not csv_path.exists():
            missing.append(csv_file)
            continue
        vals = parse_physics_column(csv_path)
        s = stats(vals)
        s["label"]  = label
        s["balls"]  = balls
        s["boxes"]  = boxes
        s["csv"]    = csv_file
        results[key] = s

    if missing:
        print(f"{RED}MISSING CSV files:{RESET}")
        for f in missing:
            print(f"  Profile/{f}")
        print("\nRun the bench suite first:")
        print("  .\\Profile\\SKULLBONEZ_CORE.exe --vsync off --suite SkullbonezData/scenes/physics_bench.suite")
        sys.exit(1)

    has_prev = bool(prev_data)
    sep = "-" * (74 + (12 if has_prev else 0))

    print()
    print(f"{BOLD}  Physics Benchmark — Legacy vs Impulse Solver{RESET}")
    print(f"  All modes: 300 objects, vsync off | Frame/Physics CPU time (ms) | pass-2 steady state")
    print(sep)

    hdr = f"  {'Mode':<22}  {'avg':>8}  {'p50':>8}  {'p95':>8}  {'p99':>8}  {'frames':>7}"
    if has_prev:
        hdr += f"  {'Δavg':>9}  {'Δp50':>9}"
    print(hdr)
    print(sep)

    for key, csv_file, label, balls, boxes in MODES:
        if key not in results:
            print(f"  {label:<22}  {'(no data)':>8}")
            continue
        r = results[key]
        row = f"  {label:<22}  {r['avg']:>8.4f}  {r['p50']:>8.4f}  {r['p95']:>8.4f}  {r['p99']:>8.4f}  {r['frames']:>7d}"
        if has_prev and key in prev_data:
            p = prev_data[key]
            row += f"  {delta_str(p.get('avg'), r['avg']):>9}  {delta_str(p.get('p50'), r['p50']):>9}"
        elif has_prev:
            row += f"  {'(new)':>9}  {'(new)':>9}"
        print(row)

    print(sep)
    print()

    # Write JSON artifact if out-dir specified
    if args.out_dir:
        out_path = Path(args.out_dir) / "physics_bench.json"
        artifact = {
            "commit":    get_commit(),
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "modes":     {k: {sk: sv for sk, sv in v.items() if sk != "label"}
                          for k, v in results.items()},
        }
        with open(out_path, "w") as f:
            json.dump(artifact, f, indent=2)
        print(f"  JSON written → {out_path}")
        print()


if __name__ == "__main__":
    main()
