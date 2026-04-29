"""
perf_compare.py — Compare perf_log.csv against a baseline perf artifact.

Usage:
    py Copilot/Skills/skore-render-test/perf_compare.py
    py Copilot/Skills/skore-render-test/perf_compare.py  <baseline.json>

Defaults:
    CSV      : Profile/perf_log.csv
    Baseline : TestOutput/baseline-001/perf.json

Delta = (current - baseline) / baseline * 100
  Negative = current is FASTER  → 🟢 green  (>5% improvement)
  Near zero = noise             → 🔵 blue   (<5% either way)
  Small positive = minor        → 🟡 yellow (5–threshold%)
  Large positive = regression   → 🔴 red    (>threshold%)

Thresholds: avg/p50 = 10%,  p99/p99.9 = 20%
"""
import json
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT   = SCRIPT_DIR.parent.parent.parent
CSV_PATH    = REPO_ROOT / "Profile" / "perf_log.csv"
DEFAULT_BASE = REPO_ROOT / "TestOutput" / "baseline-001" / "perf.json"


# ── stats ────────────────────────────────────────────────────────────────────

def percentile(s, p):
    if not s: return 0.0
    k = (len(s) - 1) * (p / 100.0)
    f = int(k); c = min(f + 1, len(s) - 1)
    return s[f] + (k - f) * (s[c] - s[f])

def compute_stats(vals):
    if not vals: return dict(avg=0, p50=0, p99=0, p99_9=0)
    s = sorted(vals)
    return dict(
        avg=round(sum(s) / len(s), 4),
        p50=round(percentile(s, 50), 4),
        p99=round(percentile(s, 99), 4),
        p99_9=round(percentile(s, 99.9), 4),
    )


# ── CSV parser ───────────────────────────────────────────────────────────────

def parse_csv(path):
    marker_names, frames, mem = [], [], []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("# MEM"):
                parts = line.split()
                cp = parts[2]; ps = int(parts[3].split("=")[1]); mb = float(parts[4].split("=")[1])
                mem.append((cp, ps, mb))
            elif line.startswith("pass,frame,"):
                marker_names = line.split(",")[2:]
            elif line and not line.startswith("#"):
                cols = line.split(",")
                if len(cols) < 2: continue
                row = {"pass": int(cols[0]), "frame": int(cols[1])}
                for i, n in enumerate(marker_names):
                    row[n] = float(cols[2 + i]) if (2 + i) < len(cols) else 0.0
                frames.append(row)
    return marker_names, frames, mem


# ── color cell ───────────────────────────────────────────────────────────────
# delta = (cur - bas) / bas * 100

def color_cell(cur_val, bas_val, thresh):
    if bas_val == 0:
        return "N/A      "
    p = (cur_val - bas_val) / bas_val * 100
    sign = "+" if p >= 0 else ""
    s = f"{sign}{p:.1f}%"
    if abs(p) < 5:
        dot = "\U0001f535"   # 🔵
    elif p < 0:
        dot = "\U0001f7e2"   # 🟢
    elif p <= thresh:
        dot = "\U0001f7e1"   # 🟡
    else:
        dot = "\U0001f534"   # 🔴
    # visible width = 2 (emoji) + 1 (space) + len(s); pad to visible 9
    pad = " " * max(0, 9 - (3 + len(s)))
    return f"{dot} {s}{pad}"


# ── box table ────────────────────────────────────────────────────────────────

H  = "\u2500"; V  = "\u2502"
TL = "\u250c"; TM = "\u252c"; TR = "\u2510"
ML = "\u251c"; MM = "\u253c"; MR = "\u2524"
BL = "\u2514"; BM = "\u2534"; BR = "\u2518"

def hline(l, m, r, ws):
    return l + m.join(H * (w + 2) for w in ws) + r

def print_table(title, markers, cur_stats, bas_stats, bas_commit):
    # measure marker col width
    mw = max((len(m) for m in markers), default=30)
    mw = max(mw, 30)
    ws = [mw, 7, 7, 9, 9, 9, 9]

    print(f"\n  {title} \u2014 vs baseline-001 {bas_commit}")
    print(f"  avg/p50 threshold 10% \u00b7 p99/p99.9 threshold 20%\n")
    print("  " + hline(TL, TM, TR, ws))

    def vrow(cells):
        parts = []
        for i, (cell, w) in enumerate(zip(cells, ws)):
            if i < 3:
                parts.append(f" {cell:<{w}} ")
            else:
                # delta cell: fixed 9 visible chars already padded in color_cell
                parts.append(f" {cell} ")
        return "  " + V + V.join(parts) + V

    headers = ["Marker", "bas avg", "cur avg", "\u0394avg", "\u0394p50", "\u0394p99", "\u0394p99.9"]
    print(vrow(headers))
    print("  " + hline(ML, MM, MR, ws))

    for i, m in enumerate(markers):
        c = cur_stats[m]
        b = bas_stats.get(m)
        if b:
            d = [
                color_cell(c["avg"],   b["avg"],   10),
                color_cell(c["p50"],   b["p50"],   10),
                color_cell(c["p99"],   b["p99"],   20),
                color_cell(c["p99_9"], b["p99_9"], 20),
            ]
        else:
            d = ["(new)    "] * 4
        print(vrow([m, f"{b['avg']:.4f}" if b else "  ---  ", f"{c['avg']:.4f}", *d]))
        if i < len(markers) - 1:
            print("  " + hline(ML, MM, MR, ws))

    print("  " + hline(BL, BM, BR, ws))


# ── memory table ─────────────────────────────────────────────────────────────

def mem_cell(cur_mb, bas_mb):
    d = cur_mb - bas_mb
    sign = "+" if d >= 0 else ""
    s = f"{sign}{d:.2f}"
    dot = "\U0001f534" if d > 5.0 else ("\U0001f7e2" if d < -5.0 else "\U0001f535")
    return f"{dot} {s}"

def print_memory(cur, bas_json):
    ms, mr, me = cur
    bms, bmr, bme = bas_json["mem_start_mb"], bas_json["mem_restart_mb"], bas_json["mem_end_mb"]
    ws = [10, 8, 8, 8]
    print(f"\n  Memory (MB)\n")
    print("  " + hline(TL, TM, TR, ws))
    def vrow(cells): return "  " + V + V.join(f" {c:<{w}} " for c, w in zip(cells, ws)) + V
    print(vrow(["", "Start", "Restart", "End"]))
    print("  " + hline(ML, MM, MR, ws))
    print(vrow(["Baseline", f"{bms:.2f}", f"{bmr:.2f}", f"{bme:.2f}"]))
    print("  " + hline(ML, MM, MR, ws))
    print(vrow(["Current",  f"{ms:.2f}", f"{mr:.2f}", f"{me:.2f}"]))
    print("  " + hline(ML, MM, MR, ws))
    print(vrow(["Delta", mem_cell(ms, bms), mem_cell(mr, bmr), mem_cell(me, bme)]))
    print("  " + hline(BL, BM, BR, ws))


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    base_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_BASE

    if not CSV_PATH.exists():
        print(f"ERROR: CSV not found: {CSV_PATH}"); return 1
    if not base_path.exists():
        print(f"ERROR: Baseline not found: {base_path}"); return 1

    marker_names, frames, mem_data = parse_csv(CSV_PATH)
    bas_json = json.loads(base_path.read_text())
    bas_commit = bas_json.get("commit", base_path.stem)

    cur_stats = {n: compute_stats([r[n] for r in frames if n in r]) for n in marker_names}
    bas_stats  = bas_json.get("markers", {})

    cpu_markers = [m for m in marker_names if not m.endswith("_gpu")]
    gpu_markers = [m for m in marker_names if m.endswith("_gpu")]

    print(f"\n  \U0001f535 = noise (<5%)  \U0001f7e2 = improvement  \U0001f7e1 = minor regression  \U0001f534 = regression")
    print(f"  CSV: {CSV_PATH.name} ({len(frames)} frames)   Baseline: {base_path.name} / {bas_commit} ({bas_json.get('total_frames','?')} frames)")

    print_table("CPU Timing (ms)", cpu_markers, cur_stats, bas_stats, bas_commit)
    print_table("GPU Timing (ms)", gpu_markers, cur_stats, bas_stats, bas_commit)

    cur_mem = (
        next((mb for cp, ps, mb in mem_data if cp == "start" and ps == 1), 0),
        next((mb for cp, ps, mb in mem_data if cp == "start" and ps == 2), 0),
        next((mb for cp, ps, mb in mem_data if cp == "end"   and ps == 2), 0),
    )
    print_memory(cur_mem, bas_json)
    print()
    return 0

if __name__ == "__main__":
    sys.exit(main())
