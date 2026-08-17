"""
File: tools/measure_causal_inspection_perf.py
Purpose:
  Measures the causal solver-inspection path on the dense 212-body wall scene.

Summary:
  Prepares a perf-enabled scene copy, runs the real Automation executable with
  gameplay allocation enforcement, verifies exact-frame detail and wheel input,
  and records named CPU costs beside the existing cause-overlay cost. The
  report is measurement evidence, not a machine-specific timing ratchet.

Glossary:
  Causal phase: One top-level inspection operation whose profiler marker may
    contain narrower lookup, layout, or presentation markers.
  Registered replay growth: RuntimeReserveAllocator growth admitted by the
    existing replay policy; it is reported separately from the fixed causal
    inspection owner.

Invariants:
  - The source scene and committed performance baselines are read-only.
  - Dynamic profiler headers govern only subsequent CSV rows.
  - Every required causal marker must execute at least once.
  - The causal owner reports fixed resident storage and has no reserve-growth
    registration of its own.
  - Timing values are evidence only; this tool imposes no regression budget.

Related:
  - SkullbonezData/interaction/causal_inspection_perf.json
  - SkullbonezSource/Runtime/Planning/ReplayCauseInspection.cpp
  - tools/validate_perf.bat
"""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
from pathlib import Path
from typing import Any

from analyze_replay_prediction_spikes import parse_perf_csv, prepare_scene


CAUSAL_MARKERS = (
    "Frame/Replay/CauseInspection/Selection",
    "Frame/Replay/CauseInspection/SolverDetailLookup",
    "Frame/Replay/CauseInspection/ManifoldPresentation",
    "Frame/Replay/CauseInspection/PanelLayout",
    "Frame/Replay/CauseInspection/PanelInput",
    "Frame/Replay/CauseInspection/PanelRender",
)
EXISTING_OVERLAY_MARKER = "Frame/Replay/CauseTree/Overlay"
RESERVE_GROWTH_PATTERN = re.compile(r"\[runtime-reserve\] growth owner=([^\s]+)")


def percentile(values: list[float], quantile: float) -> float:
    if not values:
        return 0.0

    ordered = sorted(values)
    index = max(0, math.ceil(len(ordered) * quantile) - 1)
    return ordered[index]


def marker_summary(rows: list[dict[str, Any]], marker: str) -> dict[str, float | int]:
    values = [row["timings"].get(marker, 0.0) for row in rows]
    observed = [value for value in values if value > 0.0]
    return {
        "observations": len(observed),
        "meanMs": sum(observed) / len(observed) if observed else 0.0,
        "p95Ms": percentile(observed, 0.95),
        "maxMs": max(observed, default=0.0),
    }


def registered_growth_owners(log_text: str) -> dict[str, int]:
    owners: dict[str, int] = {}

    for match in RESERVE_GROWTH_PATTERN.finditer(log_text):
        owner = match.group(1)
        owners[owner] = owners.get(owner, 0) + 1

    return owners


def validate_evidence(
    rows: list[dict[str, Any]], interaction: dict[str, Any], log_text: str
) -> dict[str, Any]:
    if not interaction.get("ok", False):
        raise ValueError(f"causal interaction failed: {interaction.get('failure', 'unknown failure')}")

    final = interaction.get("finalState", {})

    if not final.get("replayCauseInspectionDetailVisible", False):
        raise ValueError("causal solver detail never became visible")

    if final.get("replayCauseInspectionContactRowCount", 0) < 1:
        raise ValueError("causal solver detail did not retain a contact row")

    if final.get("replayCauseInspectionPipelineRecordCount", 0) < 1:
        raise ValueError("causal solver detail did not retain pipeline records")

    if final.get("replayCauseInspectionTargetFrame") != final.get("replayCauseInspectionPresentedFrame"):
        raise ValueError("causal inspection did not finish on its exact target frame")

    scroll_actions = [action for action in interaction.get("actions", []) if action.get("type") == "scrollPoint"]

    if not scroll_actions or not all(action.get("consumed", False) for action in scroll_actions):
        raise ValueError("causal panel wheel input was not injected successfully")

    first_visible_row = int(final.get("replayCauseInspectionFirstVisibleRow", 0))

    summaries = {marker: marker_summary(rows, marker) for marker in CAUSAL_MARKERS}
    missing = [marker for marker, summary in summaries.items() if summary["observations"] == 0]

    if missing:
        raise ValueError("required causal profiler markers were not observed: " + ", ".join(missing))

    overlay = marker_summary(rows, EXISTING_OVERLAY_MARKER)

    if overlay["observations"] == 0:
        raise ValueError("existing cause-overlay marker was not observed")

    panel_render = summaries["Frame/Replay/CauseInspection/PanelRender"]

    if panel_render["maxMs"] > overlay["maxMs"]:
        raise ValueError("nested causal panel render exceeded its enclosing cause-overlay sample")

    if "[allocation-guard] PASS:" not in log_text:
        raise ValueError("gameplay allocation guard did not publish a clean PASS marker")

    if "[allocation-guard] FAIL:" in log_text:
        raise ValueError("gameplay allocation guard reported a failure")

    growth_owners = registered_growth_owners(log_text)
    causal_growth = {name: count for name, count in growth_owners.items() if "cause" in name.lower()}

    if causal_growth:
        raise ValueError(f"causal inspection unexpectedly used registered reserve growth: {causal_growth}")

    fixed_bytes = int(final.get("replayCauseInspectionFixedStorageBytes", 0))

    if fixed_bytes <= 0:
        raise ValueError("causal inspection fixed-storage evidence is missing")

    total_bodies = max(
        (int(row["timings"].get("Counter/Physics/TotalBodies", 0.0)) for row in rows),
        default=0,
    )
    top_level_markers = (
        "Frame/Replay/CauseInspection/Selection",
        "Frame/Replay/CauseInspection/PanelInput",
        "Frame/Replay/CauseInspection/PanelRender",
    )
    max_top_level_phase = max(summaries[name]["maxMs"] for name in top_level_markers)

    return {
        "schemaVersion": 1,
        "scene": {
            "name": "prediction_ragdoll_wall_200",
            "maximumPhysicsBodies": total_bodies,
            "representativeDenseScene": total_bodies >= 200,
        },
        "inspection": {
            "selectedRow": final.get("replayCauseInspectionSelectedRow"),
            "targetFrame": final.get("replayCauseInspectionTargetFrame"),
            "presentedFrame": final.get("replayCauseInspectionPresentedFrame"),
            "contactRows": final.get("replayCauseInspectionContactRowCount"),
            "pipelineRecords": final.get("replayCauseInspectionPipelineRecordCount"),
            "visibleViewportRows": 4,
            "scrollInputExercised": True,
            "firstVisibleRowAfterWheel": first_visible_row,
        },
        "costMs": {
            "markers": summaries,
            "existingCauseOverlay": overlay,
            "maximumTopLevelCausalPhase": max_top_level_phase,
            "panelRenderShareOfEnclosingOverlayMax": (
                panel_render["maxMs"] / overlay["maxMs"] if overlay["maxMs"] > 0.0 else 0.0
            ),
            "budgetDisposition": "measurement-only; existing overlay budgets remain authoritative",
        },
        "allocation": {
            "gameplayGuardPassed": True,
            "causalReserveGrowthEvents": 0,
            "fixedInspectionStorageBytes": fixed_bytes,
            "registeredReplayGrowthOwners": growth_owners,
        },
    }


def run_measurement(repo: Path, output: Path, timeout_seconds: float) -> dict[str, Any]:
    executable = repo / "Automation" / "SKULLBONEZ_CORE.exe"
    source_scene = repo / "SkullbonezData" / "scenes" / "prediction_ragdoll_wall_200.scene.json"
    script = repo / "SkullbonezData" / "interaction" / "causal_inspection_perf.json"
    scene = output / "causal_inspection_perf.scene.json"
    perf_csv = output / "perf.csv"
    interaction_report = output / "interaction.json"
    run_log = output / "run.log"
    report_path = output / "report.json"

    if not executable.is_file():
        raise FileNotFoundError(f"Automation executable is missing: {executable}")

    output.mkdir(parents=True, exist_ok=True)

    for artifact in (perf_csv, interaction_report, run_log, report_path):
        artifact.unlink(missing_ok=True)

    perf_path = perf_csv.relative_to(repo).as_posix()
    prepare_scene(source_scene, scene, perf_path)
    command = [
        str(executable),
        "--renderer", "dx12",
        "--vsync", "off",
        "--shadows", "off",
        "--cinematic", "off",
        "--hide-top-text",
        "--automation-hidden-window",
        "--allocation-guard", "gameplay",
        "--scene", str(scene),
        "--interaction-script", str(script),
        "--interaction-report", str(interaction_report),
        "--frames", "1150",
        "--replay", "on",
        "--replay-seconds", "2",
        "--fixed-step",
    ]

    try:
        with run_log.open("w", encoding="utf-8") as log:
            result = subprocess.run(
                command,
                cwd=repo,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=timeout_seconds,
                check=False,
            )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f"causal inspection measurement exceeded {timeout_seconds:g} seconds") from exc

    if result.returncode != 0:
        raise RuntimeError(f"causal inspection workload exited with code {result.returncode}; see {run_log}")

    interaction = json.loads(interaction_report.read_text(encoding="utf-8"))
    report = validate_evidence(parse_perf_csv(perf_csv), interaction, run_log.read_text(encoding="utf-8"))
    report["artifacts"] = {
        "scene": str(scene),
        "perfCsv": str(perf_csv),
        "interactionReport": str(interaction_report),
        "runLog": str(run_log),
    }
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def self_test() -> None:
    values = [4.0, 1.0, 3.0, 2.0]
    assert percentile(values, 0.95) == 4.0
    assert registered_growth_owners("[runtime-reserve] growth owner=replay_test target=x\n") == {
        "replay_test": 1
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Measure dense-scene causal inspection cost and allocation posture.")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("TestOutput/validation/causal_inspection_perf"),
    )
    parser.add_argument("--timeout-seconds", type=float, default=90.0)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        print("PASS: causal inspection perf analyzer self-test")
        return 0

    repo = args.repo.resolve()
    output = args.output if args.output.is_absolute() else repo / args.output
    report = run_measurement(repo, output, args.timeout_seconds)
    cost = report["costMs"]
    allocation = report["allocation"]
    print(
        "PASS: causal inspection dense-scene cost "
        f"max_phase={cost['maximumTopLevelCausalPhase']:.4f}ms "
        f"panel/overlay={cost['panelRenderShareOfEnclosingOverlayMax']:.3f} "
        f"fixed_storage={allocation['fixedInspectionStorageBytes']}B "
        "steady_allocations=0"
    )
    print(f"Evidence: {output / 'report.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
