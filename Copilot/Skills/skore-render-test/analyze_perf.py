"""
Perf test analysis script for SkullbonezCore.
Reads the perf_log.csv written by the engine, computes statistics,
and writes a JSON artifact to perf_history/{commit_hash}.json.
Optionally compares against the previous artifact.
"""
import csv
import json
import os
import subprocess
import sys
from pathlib import Path

CSV_PATH   = r"Y:\SkullbonezCore\Debug\perf_log.csv"
HIST_DIR   = Path(r"Y:\SkullbonezCore\Copilot\Skills\skore-render-test\perf_history")


def percentile(sorted_vals, p):
    """Return the p-th percentile from a sorted list (0-100 scale)."""
    if not sorted_vals:
        return 0.0
    k = (len(sorted_vals) - 1) * (p / 100.0)
    f = int(k)
    c = f + 1 if f + 1 < len(sorted_vals) else f
    d = k - f
    return sorted_vals[f] + d * (sorted_vals[c] - sorted_vals[f])


def parse_csv(path):
    """Parse perf_log.csv into frame rows and memory checkpoints."""
    frames = []
    mem = []
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if line.startswith("# MEM"):
                parts = line.split()
                checkpoint = parts[2]
                pass_num = int(parts[3].split("=")[1])
                mb = float(parts[4].split("=")[1])
                mem.append({"checkpoint": checkpoint, "pass": pass_num, "working_set_mb": mb})
            elif line and not line.startswith("pass,") and not line.startswith("#"):
                cols = line.split(",")
                frames.append({
                    "pass":       int(cols[0]),
                    "frame":      int(cols[1]),
                    "physics_ms": float(cols[2]),
                    "render_ms":  float(cols[3]),
                })
    return frames, mem


def compute_stats(values):
    """Compute avg, p50, p99, p99.9 for a list of floats."""
    if not values:
        return {"min": 0, "max": 0, "avg": 0, "p50": 0, "p99": 0, "p99_9": 0}
    s = sorted(values)
    return {
        "min":   round(s[0], 4),
        "max":   round(s[-1], 4),
        "avg":   round(sum(s) / len(s), 4),
        "p50":   round(percentile(s, 50), 4),
        "p99":   round(percentile(s, 99), 4),
        "p99_9": round(percentile(s, 99.9), 4),
    }


def get_commit_hash():
    """Get current git commit hash."""
    result = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"],
        capture_output=True, text=True, cwd=r"Y:\SkullbonezCore"
    )
    return result.stdout.strip()


def find_previous_artifact(hist_dir, current_hash):
    """Find the most recent artifact that isn't the current commit."""
    artifacts = sorted(hist_dir.glob("*.json"), key=lambda p: p.stat().st_mtime)
    for a in reversed(artifacts):
        if a.stem != current_hash:
            return a
    return None


def compare_stats(current, previous):
    """Compare current vs previous, apply thresholds, return pass/fail."""
    prev = json.loads(previous.read_text())
    print(f"\n--- Comparison vs {prev['commit']} ---")

    # Thresholds: avg/p50 >10% regression, p99/p99.9 >20%, memory >5 MB
    TIMING_THRESHOLDS = {"min": 10, "max": 20, "avg": 10, "p50": 10, "p99": 20, "p99_9": 20}
    MEM_THRESHOLD_MB = 5.0

    failures = []

    for metric in ["physics_ms", "render_ms"]:
        print(f"\n  {metric}:")
        for stat in ["min", "max", "avg", "p50", "p99", "p99_9"]:
            cur_val  = current[metric][stat]
            prev_val = prev[metric][stat]
            threshold = TIMING_THRESHOLDS[stat]
            if prev_val > 0:
                delta_pct = ((cur_val - prev_val) / prev_val) * 100
                direction = "SLOWER" if delta_pct > 0 else "FASTER"
                flag = " ** REGRESSION **" if delta_pct > threshold else ""
                print(f"    {stat:6s}: {prev_val:.4f} -> {cur_val:.4f}  ({delta_pct:+.1f}% {direction}){flag}")
                if delta_pct > threshold:
                    failures.append(f"{metric}.{stat}: +{delta_pct:.1f}% (threshold: {threshold}%)")
            else:
                print(f"    {stat:6s}: {prev_val:.4f} -> {cur_val:.4f}")

    print(f"\n  Memory (MB):")
    for key in ["mem_start_mb", "mem_restart_mb", "mem_end_mb"]:
        cur_val  = current[key]
        prev_val = prev[key]
        delta = cur_val - prev_val
        flag = " ** REGRESSION **" if delta > MEM_THRESHOLD_MB else ""
        print(f"    {key:18s}: {prev_val:.2f} -> {cur_val:.2f}  ({delta:+.2f} MB){flag}")
        if delta > MEM_THRESHOLD_MB:
            failures.append(f"{key}: +{delta:.2f} MB (threshold: {MEM_THRESHOLD_MB} MB)")

    return failures


def main():
    if not os.path.exists(CSV_PATH):
        print(f"ERROR: {CSV_PATH} not found")
        sys.exit(1)

    frames, mem = parse_csv(CSV_PATH)
    commit = get_commit_hash()

    print(f"Commit: {commit}")
    print(f"Total frames: {len(frames)}")
    print(f"Memory checkpoints: {len(mem)}")

    # Compute stats across all frames (both passes)
    physics_vals = [f["physics_ms"] for f in frames]
    render_vals  = [f["render_ms"]  for f in frames]

    physics_stats = compute_stats(physics_vals)
    render_stats  = compute_stats(render_vals)

    # Extract memory checkpoints
    mem_start   = next((m["working_set_mb"] for m in mem if m["checkpoint"] == "start" and m["pass"] == 1), 0)
    mem_restart = next((m["working_set_mb"] for m in mem if m["checkpoint"] == "start" and m["pass"] == 2), 0)
    mem_end     = next((m["working_set_mb"] for m in mem if m["checkpoint"] == "end"   and m["pass"] == 2), 0)

    result = {
        "commit":         commit,
        "total_frames":   len(frames),
        "physics_ms":     physics_stats,
        "render_ms":      render_stats,
        "mem_start_mb":   round(mem_start, 2),
        "mem_restart_mb": round(mem_restart, 2),
        "mem_end_mb":     round(mem_end, 2),
    }

    # Print summary
    print(f"\nPhysics (ms): min={physics_stats['min']:.4f}  max={physics_stats['max']:.4f}  "
          f"avg={physics_stats['avg']:.4f}  p50={physics_stats['p50']:.4f}  "
          f"p99={physics_stats['p99']:.4f}  p99.9={physics_stats['p99_9']:.4f}")
    print(f"Render  (ms): min={render_stats['min']:.4f}  max={render_stats['max']:.4f}  "
          f"avg={render_stats['avg']:.4f}  p50={render_stats['p50']:.4f}  "
          f"p99={render_stats['p99']:.4f}  p99.9={render_stats['p99_9']:.4f}")
    print(f"Memory  (MB): start={mem_start:.2f}  restart={mem_restart:.2f}  end={mem_end:.2f}")

    # Write artifact
    HIST_DIR.mkdir(parents=True, exist_ok=True)
    artifact_path = HIST_DIR / f"{commit}.json"
    artifact_path.write_text(json.dumps(result, indent=2))
    print(f"\nArtifact written: {artifact_path}")

    # Compare against previous
    prev = find_previous_artifact(HIST_DIR, commit)
    if prev:
        failures = compare_stats(result, prev)
        if failures:
            print(f"\n** PERF TEST FAILED — {len(failures)} regression(s) **")
            for f in failures:
                print(f"  - {f}")
            return 1
        else:
            print("\nPERF TEST PASSED — no regressions")
    else:
        print("\nNo previous artifact to compare against. PASS (baseline).")

    return 0


if __name__ == "__main__":
    sys.exit(main())
