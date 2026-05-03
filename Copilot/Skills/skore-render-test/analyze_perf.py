"""
Perf test analysis script for SkullbonezCore.

Usage:
    py analyze_perf.py --renderer <gl|dx11|dx12> --csv <path> --out-dir <dir>

Reads a perf_log.csv, computes statistics for every profiler marker column,
and writes {renderer}_perf.json into the specified output directory.

Paths are derived from this script's location — no hardcoded drive letters.
  analyze_perf.py  lives in  Copilot/Skills/skore-render-test/
  three levels up  gives the repo root.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent          # …/Copilot/Skills/skore-render-test
REPO_ROOT  = SCRIPT_DIR.parent.parent.parent          # repo root


def _wmic(resource, field):
    """Query a single WMIC field value; return empty string on failure."""
    try:
        r = subprocess.run(
            ["wmic", resource, "get", field, "/value"],
            capture_output=True, text=True
        )
        for line in r.stdout.splitlines():
            if line.startswith(field + "="):
                return line.split("=", 1)[1].strip()
    except Exception:
        pass
    return ""


def get_machine_info():
    """Collect CPU and RAM info (no hostname for privacy)."""
    import platform

    cpu   = _wmic("cpu", "Name") or platform.processor() or "unknown"
    cores = _wmic("cpu", "NumberOfLogicalProcessors") or str(os.cpu_count() or 0)
    ram_bytes = _wmic("computersystem", "TotalPhysicalMemory")
    ram_gb = round(int(ram_bytes) / (1024 ** 3), 1) if ram_bytes.isdigit() else 0.0

    return {
        "cpu":    cpu,
        "cores":  int(cores) if cores.isdigit() else 0,
        "ram_gb": ram_gb,
        "os":     platform.version(),
    }


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
    """Parse perf_log.csv into frame rows and memory checkpoints.

    Returns (marker_names, frames, mem) where marker_names is a list of
    column names (excluding pass/frame), frames is a list of dicts, and
    mem is a list of memory checkpoint dicts.
    """
    marker_names = []
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
            elif line.startswith("pass,frame,"):
                # Dynamic header — discover marker columns
                cols = line.split(",")
                marker_names = cols[2:]  # everything after pass, frame
            elif line and not line.startswith("#"):
                cols = line.split(",")
                if len(cols) < 2:
                    continue
                row = {"pass": int(cols[0]), "frame": int(cols[1])}
                for i, name in enumerate(marker_names):
                    row[name] = float(cols[2 + i]) if (2 + i) < len(cols) else 0.0
                frames.append(row)
    return marker_names, frames, mem


def compute_stats(values):
    """Compute min/max/avg/p50/p99/p99.9 for a list of floats."""
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
        capture_output=True, text=True, cwd=str(REPO_ROOT)
    )
    return result.stdout.strip()


NOISE_TOLERANCE = 5.0  # ±5% is within noise — shown in blue


def _color_pct(pct, threshold):
    """Return ANSI-colored percentage string.
    Blue   = within ±5% noise tolerance
    Green  = clearly faster (beyond tolerance)
    Yellow = small regression (tolerance < pct <= threshold)
    Red    = regression exceeds threshold
    """
    GREEN  = "\033[32m"
    YELLOW = "\033[33m"
    RED    = "\033[31m"
    BLUE   = "\033[34m"
    RESET  = "\033[0m"
    sign = "+" if pct > 0 else ""
    s = f"{sign}{pct:.1f}%"
    if abs(pct) <= NOISE_TOLERANCE:
        return f"{BLUE}{s}{RESET}"
    elif pct < 0:
        return f"{GREEN}{s}{RESET}"
    elif pct <= threshold:
        return f"{YELLOW}{s}{RESET}"
    else:
        return f"{RED}{s}{RESET}"


def _color_mem(delta_mb, threshold=5.0):
    GREEN = "\033[32m"
    RED   = "\033[31m"
    RESET = "\033[0m"
    sign = "+" if delta_mb > 0 else ""
    s = f"{sign}{delta_mb:.2f} MB"
    return f"{RED}{s}{RESET}" if delta_mb > threshold else f"{GREEN}{s}{RESET}"


def main():
    parser = argparse.ArgumentParser(description="Analyze SkullbonezCore perf CSV and write JSON artifact.")
    parser.add_argument("--renderer", required=True, choices=["gl", "dx11", "dx12"],
                        help="Renderer that produced this CSV (gl or dx11)")
    parser.add_argument("--csv", required=True, type=Path,
                        help="Path to perf_log.csv")
    parser.add_argument("--out-dir", required=True, type=Path,
                        help="Directory to write {renderer}_perf.json into")
    args = parser.parse_args()

    csv_path = args.csv
    if not csv_path.exists():
        print(f"ERROR: CSV not found: {csv_path}")
        sys.exit(1)

    marker_names, frames, mem = parse_csv(str(csv_path))
    if not frames:
        print(f"ERROR: No frame rows found in {csv_path}")
        sys.exit(1)

    commit  = get_commit_hash()
    machine = get_machine_info()

    print(f"Commit   : {commit}")
    print(f"Renderer : {args.renderer.upper()}")
    print(f"Machine  : {machine['cpu']}  {machine['cores']} cores  {machine['ram_gb']} GB RAM")
    print(f"Frames   : {len(frames)}  |  Markers: {len(marker_names)}")

    # Compute stats for every marker column
    marker_stats = {}
    for name in marker_names:
        vals = [f[name] for f in frames if name in f]
        marker_stats[name] = compute_stats(vals)

    # Print summary for top-level markers
    for name in marker_names:
        if "/" not in name:
            s = marker_stats[name]
            print(f"{name:20s}: avg={s['avg']:.4f}  p50={s['p50']:.4f}  p99={s['p99']:.4f}  p99.9={s['p99_9']:.4f}")

    # Extract memory checkpoints
    mem_start   = next((m["working_set_mb"] for m in mem if m["checkpoint"] == "start"   and m["pass"] == 1), 0)
    mem_restart = next((m["working_set_mb"] for m in mem if m["checkpoint"] == "start"   and m["pass"] == 2), 0)
    mem_end     = next((m["working_set_mb"] for m in mem if m["checkpoint"] == "end"     and m["pass"] == 2), 0)
    print(f"Memory   (MB): start={mem_start:.2f}  restart={mem_restart:.2f}  end={mem_end:.2f}")

    result = {
        "schema_version": 2,
        "commit":         commit,
        "renderer":       args.renderer,
        "machine":        machine,
        "total_frames":   len(frames),
        "markers":        marker_stats,
        "mem_start_mb":   round(mem_start, 2),
        "mem_restart_mb": round(mem_restart, 2),
        "mem_end_mb":     round(mem_end, 2),
    }

    # Write artifact
    args.out_dir.mkdir(parents=True, exist_ok=True)
    artifact_path = args.out_dir / f"{args.renderer}_perf.json"
    artifact_path.write_text(json.dumps(result, indent=2))
    print(f"\nArtifact written: {artifact_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
