"""
File: Agentic/Skills/skore-render-test/perf_compare.py
Purpose:
  Compares current and baseline render-performance JSON artifacts.

Summary:
  The comparison prints one bounded row per current marker, applies a
  duration-sensitive CPU regression threshold, reports GPU timers without
  gating them, and rejects memory growth above the fixed artifact threshold.
  Different renderers fail; different machines produce a warning-only result.

Glossary:
  Ramped threshold: Percentage allowance `max(10, 10/sqrt(baseline_ms))` that
    gives sub-millisecond markers additional scheduling-jitter headroom.
  Measurement floor: Baseline below 0.05 ms, where percentage gating is skipped.

Invariants:
  - Only matching renderer labels are comparable.
  - GPU timer and PipelineSync rows remain visible but never gate commits.
  - Output size is bounded by current marker count and detected failures.

Related:
  - Agentic/Skills/skore-render-test/analyze_perf.py
  - Agentic/Skills/skore-render-test/skill.md
"""
import argparse
import json
import math
import sys
from pathlib import Path


def ramp_threshold(baseline_ms: float) -> float:
    """Dynamic regression % threshold scaled to baseline duration.

    Converges to 10% above ~1 ms; gives more headroom for sub-ms markers
    where OS scheduling jitter dominates over real performance differences.
    """
    return max(10.0, 10.0 / math.sqrt(max(baseline_ms, 1e-9)))


def percentile(s, p):
    if not s: return 0.0
    k = (len(s) - 1) * (p / 100.0)
    f = int(k); c = min(f + 1, len(s) - 1)
    return s[f] + (k - f) * (s[c] - s[f])


def color_cell(cur_val, bas_val):
    if bas_val == 0:
        return "N/A      "
    thresh = ramp_threshold(bas_val)
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
    pad = " " * max(0, 9 - (3 + len(s)))
    return f"{dot} {s}{pad}"


H  = "\u2500"; V  = "\u2502"
TL = "\u250c"; TM = "\u252c"; TR = "\u2510"
ML = "\u251c"; MM = "\u253c"; MR = "\u2524"
BL = "\u2514"; BM = "\u2534"; BR = "\u2518"

def hline(l, m, r, ws):
    return l + m.join(H * (w + 2) for w in ws) + r

def print_table(title, markers, cur_stats, bas_stats, prev_commit):
    mw = max((len(m) for m in markers), default=30)
    mw = max(mw, 30)
    ws = [mw, 7, 7, 9, 9]

    print(f"\n  {title} \u2014 vs {prev_commit}")
    print(f"  ramped threshold: max(10%, 10/\u221ams) per marker\n")
    print("  " + hline(TL, TM, TR, ws))

    def vrow(cells):
        parts = []
        for i, (cell, w) in enumerate(zip(cells, ws)):
            if i < 3:
                parts.append(f" {cell:<{w}} ")
            else:
                parts.append(f" {cell} ")
        return "  " + V + V.join(parts) + V

    headers = ["Marker", "bas avg", "cur avg", "\u0394avg", "\u0394p50"]
    print(vrow(headers))
    print("  " + hline(ML, MM, MR, ws))

    for i, m in enumerate(markers):
        c = cur_stats[m]
        b = bas_stats.get(m)
        if b:
            d = [
                color_cell(c["avg"], b["avg"]),
                color_cell(c["p50"], b["p50"]),
            ]
        else:
            d = ["(new)    "] * 2
        print(vrow([m, f"{b['avg']:.4f}" if b else "  ---  ", f"{c['avg']:.4f}", *d]))
        if i < len(markers) - 1:
            print("  " + hline(ML, MM, MR, ws))

    print("  " + hline(BL, BM, BR, ws))


def mem_cell(cur_mb, bas_mb):
    d = cur_mb - bas_mb
    sign = "+" if d >= 0 else ""
    s = f"{sign}{d:.2f}"
    dot = "\U0001f534" if d > 5.0 else ("\U0001f7e2" if d < -5.0 else "\U0001f535")
    return f"{dot} {s}"

def print_memory(cur_json, bas_json):
    ms  = cur_json["mem_start_mb"];   mr  = cur_json["mem_restart_mb"]; me  = cur_json["mem_end_mb"]
    bms = bas_json["mem_start_mb"];   bmr = bas_json["mem_restart_mb"]; bme = bas_json["mem_end_mb"]
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


def check_regressions(cur_stats, bas_stats, cur_json, bas_json):
    failures = []
    for marker, cm in cur_stats.items():
        # GPU timer query averages are inherently noisy (driver scheduling,
        # thermal transitions) — display them but don't gate commits.
        if marker.endswith("_gpu"):
            continue
        # PipelineSync measures glFinish/Flush wait time — it is entirely
        # driven by GPU load and scheduling jitter, not CPU logic.
        if "PipelineSync" in marker:
            continue
        pm = bas_stats.get(marker)
        if not pm:
            continue
        for stat in ["avg", "p50"]:
            pv, cv = pm[stat], cm[stat]
            # Skip sub-0.05ms markers — percentage comparison is meaningless
            # at the measurement floor (scheduling jitter dominates).
            if pv < 0.05:
                continue
            if pv > 0:
                threshold = ramp_threshold(pv)
                pct = (cv - pv) / pv * 100
                if pct > threshold:
                    failures.append(f"{marker}.{stat}: +{pct:.1f}% (threshold: {threshold:.0f}%)")

    for key, label, thresh in [
        ("mem_start_mb", "mem_start", 5.0),
        ("mem_restart_mb", "mem_restart", 5.0),
        ("mem_end_mb", "mem_end", 5.0),
    ]:
        delta = cur_json[key] - bas_json[key]
        if delta > thresh:
            failures.append(f"{label}: +{delta:.2f} MB (threshold: {thresh} MB)")

    return failures


def main():
    parser = argparse.ArgumentParser(description="Compare two SkullbonezCore perf JSON artifacts.")
    parser.add_argument("--current",  required=True, type=Path, help="Current {renderer}_perf.json")
    parser.add_argument("--previous", required=True, type=Path, help="Previous {renderer}_perf.json to compare against")
    args = parser.parse_args()

    if not args.current.exists():
        print(f"ERROR: Current artifact not found: {args.current}"); return 1
    if not args.previous.exists():
        print(f"ERROR: Previous artifact not found: {args.previous}"); return 1

    cur_json = json.loads(args.current.read_text())
    bas_json = json.loads(args.previous.read_text())

    cur_renderer = cur_json.get("renderer", "unknown")
    bas_renderer = bas_json.get("renderer", "unknown")

    if cur_renderer != bas_renderer:
        print(f"ERROR: Renderer mismatch — current={cur_renderer}, previous={bas_renderer}")
        return 1

    # Hazard: timing deltas across CPUs are not comparable. Preserve a clean
    # exit so shared artifact inspection remains possible, but skip the gate.
    cur_cpu = cur_json.get("machine", {}).get("cpu", "")
    bas_cpu = bas_json.get("machine", {}).get("cpu", "")
    if bas_cpu and cur_cpu != bas_cpu:
        print(f"\n  WARNING: Machine mismatch — perf comparison is not valid across machines.")
        print(f"    Previous : {bas_cpu}")
        print(f"    Current  : {cur_cpu}")
        print(f"  Skipping regression check.")
        return 0

    renderer_label = cur_renderer.upper()
    prev_commit    = bas_json.get("commit", args.previous.stem)
    cur_commit     = cur_json.get("commit", args.current.stem)

    print(f"\n  {renderer_label} Perf: {cur_commit} vs {prev_commit}")
    print(f"  \U0001f535 = noise (<5%)  \U0001f7e2 = improvement  \U0001f7e1 = minor regression  \U0001f534 = regression")
    print(f"  Current: {cur_json['total_frames']} frames   Previous: {bas_json['total_frames']} frames")

    cur_stats = cur_json.get("markers", {})
    bas_stats = bas_json.get("markers", {})

    cpu_markers = [m for m in cur_stats if not m.endswith("_gpu")]
    gpu_markers = [m for m in cur_stats if m.endswith("_gpu")]

    print_table(f"CPU Timing (ms) [{renderer_label}]", cpu_markers, cur_stats, bas_stats, prev_commit)
    if gpu_markers:
        print_table(f"GPU Timing (ms) [{renderer_label}]", gpu_markers, cur_stats, bas_stats, prev_commit)
    print_memory(cur_json, bas_json)
    print()

    failures = check_regressions(cur_stats, bas_stats, cur_json, bas_json)
    if failures:
        print(f"** PERF REGRESSION — {len(failures)} failure(s) [{renderer_label}] **")
        for f in failures:
            print(f"  - {f}")
        return 1

    print(f"PASS: No regressions [{renderer_label}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
