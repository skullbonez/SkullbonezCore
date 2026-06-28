"""
File: tools/check_perf_budgets.py
Purpose:
  Enforces absolute millisecond budgets for perf artifacts produced by validate_perf.

Mental model:
  Baseline comparisons catch relative drift only when a matching baseline and
  machine are available. This checker is the hard stop for critical frame
  markers: if physics or render work grows into whole-frame territory, the
  perf gate fails even when the relative comparison is skipped.

Glossary:
  Perf artifact: JSON output from analyze_perf.py with per-marker timing stats.
  Marker budget: Absolute time ceiling for one profiler marker statistic.
  Critical marker: Frame path whose regression can make the game visibly hitch.

Invariants:
  - Budgets are in milliseconds and are intentionally independent of machine
    baseline names.
  - Missing critical markers fail closed so new profiling names cannot bypass
    the gate silently.

Related:
  - tools/validate_perf.bat
  - Agentic/Skills/skore-render-test/analyze_perf.py
  - TestOutput/baselines/*_perf.json
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Budget:
    marker: str
    stat: str
    limit_ms: float
    reason: str


BUDGETS_BY_RENDERER: dict[str, tuple[Budget, ...]] = {
    "dx12": (
        Budget("Frame", "avg", 2.0, "The DX12 perf scene should stay inside the old tiny-frame envelope on average."),
        Budget("Frame", "p99", 4.0, "Whole-frame p99 hitches need explicit perf review."),
        Budget("Frame/Physics", "avg", 0.75, "DX12 perf scene physics should stay comfortably sub-ms on average."),
        Budget("Frame/Physics", "p99", 2.0, "DX12 perf scene physics spikes must stay below frame-visible cost."),
        Budget("Frame/Physics", "max", 2.0, "A physics update must stay inside the tiny-frame envelope."),
        Budget("Frame/Physics/Step", "avg", 0.75, "One fixed physics step should stay comfortably sub-ms on average."),
        Budget("Frame/Physics/Step", "p99", 2.0, "One fixed physics step must not consume a 60 Hz frame slice."),
        Budget("Frame/Physics/Step", "max", 2.0, "A single fixed physics step must never become frame-scale work."),
        Budget("Frame/Render", "avg", 1.0, "The core DX12 render path should stay near the old tiny-frame envelope."),
        Budget("Frame/Render", "p99", 2.0, "Render spikes above two milliseconds need explicit review."),
    ),
    "physics_bench": (
        Budget("Frame", "avg", 1.0, "Physics bench should remain a tiny-frame benchmark."),
        Budget("Frame", "p99", 2.0, "Physics bench frame p99 should stay below visible hitch territory."),
        Budget("Frame/Physics", "avg", 0.5, "Physics bench is the focused solver guardrail."),
        Budget("Frame/Physics", "p99", 1.0, "Physics bench p99 should stay below one millisecond."),
        Budget("Frame/Physics", "max", 2.0, "Physics bench should not produce multi-ms solver stalls."),
        Budget("Frame/Physics/Step", "avg", 0.5, "A benchmarked fixed step should stay sub-ms on average."),
        Budget("Frame/Physics/Step", "p99", 1.0, "A benchmarked fixed step should stay below one millisecond at p99."),
        Budget("Frame/Physics/Step", "max", 2.0, "A benchmarked fixed step should never reach a frame-scale stall."),
        Budget("Frame/Render", "avg", 1.0, "Physics bench render overhead should remain incidental."),
        Budget("Frame/Render", "p99", 2.0, "Physics bench render spikes above two milliseconds need review."),
    ),
}


def check_budgets(artifact_path: Path) -> int:
    if not artifact_path.exists():
        print(f"ERROR: perf artifact not found: {artifact_path}")
        return 1

    artifact = json.loads(artifact_path.read_text(encoding="utf-8"))
    renderer = str(artifact.get("renderer", "")).lower()
    budgets = BUDGETS_BY_RENDERER.get(renderer)
    if not budgets:
        print(f"ERROR: no absolute perf budgets configured for renderer '{renderer}'.")
        return 1

    markers = artifact.get("markers", {})
    failures: list[str] = []
    for budget in budgets:
        marker_stats = markers.get(budget.marker)
        if marker_stats is None:
            failures.append(f"{budget.marker}.{budget.stat}: missing critical marker")
            continue
        value = marker_stats.get(budget.stat)
        if value is None:
            failures.append(f"{budget.marker}.{budget.stat}: missing statistic")
            continue
        if float(value) > budget.limit_ms:
            failures.append(
                f"{budget.marker}.{budget.stat}: {float(value):.4f} ms > "
                f"{budget.limit_ms:.4f} ms ({budget.reason})"
            )

    label = renderer.upper()
    if failures:
        print(f"** PERF BUDGET FAILURE - {len(failures)} failure(s) [{label}] **")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print(f"PASS: absolute perf budgets [{label}]")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Check absolute SkullbonezCore perf budgets.")
    parser.add_argument("--artifact", required=True, type=Path, help="Path to a {renderer}_perf.json artifact")
    args = parser.parse_args()
    return check_budgets(args.artifact)


if __name__ == "__main__":
    sys.exit(main())
