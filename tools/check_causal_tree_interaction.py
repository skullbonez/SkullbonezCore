"""
File: tools/check_causal_tree_interaction.py
Purpose:
  Runs and verifies the causal-window hierarchy and repeated-click UI probe.

Summary:
  The checker drives the normal pointer route over a retained solver frame,
  then requires both a deep recorded contact hierarchy and the final physical
  retarget to remain active.

Invariants:
  - Pointer clicks are injected through Input; only the initial setup row uses
    the semantic automation seam.
  - Recorded hierarchy evidence must extend beyond body/manifold/solver depth.
  - A successful final click keeps replay paused on the selected causal row.

Related:
  - SkullbonezData/interaction/causal_tree_retarget_visual_qa.json
  - SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp
"""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import Any


def validate_report(report: dict[str, Any]) -> None:
    if not report.get("ok", False):
        raise ValueError(f"interaction failed: {report.get('failure', 'unknown failure')}")

    final = report.get("finalState", {})

    if int(final.get("replayCauseTreeRowCount", 0)) < 6:
        raise ValueError("recorded cause tree did not retain counterpart-body rows")

    if int(final.get("replayCauseTreeMaximumDepth", 0)) < 4:
        raise ValueError("recorded cause tree did not extend beyond one contact-detail level")

    if int(final.get("replayCauseTreeSelectedRow", -1)) != 2:
        raise ValueError("final physical row click did not retarget the cause tree")

    if int(final.get("replayCauseInspectionSelectedRow", -1)) != 2:
        raise ValueError("cause inspection did not retain the final physical row selection")

    if not final.get("replayHistoricalSamplePaused", False):
        raise ValueError("causal retarget unexpectedly returned replay to live advance")

    physical_clicks = [action for action in report.get("actions", []) if action.get("type") == "clickPoint"]

    if len(physical_clicks) != 2 or not all(action.get("consumed", False) for action in physical_clicks):
        raise ValueError("expected two injected physical cause-row clicks")


def self_test() -> None:
    validate_report(
        {
            "ok": True,
            "actions": [
                {"type": "clickPoint", "consumed": True},
                {"type": "clickPoint", "consumed": True},
            ],
            "finalState": {
                "replayCauseTreeRowCount": 11,
                "replayCauseTreeMaximumDepth": 6,
                "replayCauseTreeSelectedRow": 2,
                "replayCauseInspectionSelectedRow": 2,
                "replayHistoricalSamplePaused": True,
            },
        }
    )


def run_probe(repo: Path, executable: Path, timeout_seconds: float) -> Path:
    output = repo / "TestOutput" / "validation" / "causal_tree_interaction"
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / "interaction.json"
    command = [
        str(executable),
        "--renderer",
        "dx12",
        "--vsync",
        "off",
        "--shadows",
        "off",
        "--cinematic",
        "off",
        "--hide-top-text",
        "--automation-hidden-window",
        "--scene",
        "SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json",
        "--interaction-script",
        "SkullbonezData/interaction/causal_tree_retarget_visual_qa.json",
        "--interaction-report",
        str(report_path),
        "--frames",
        "1250",
        "--replay",
        "on",
        "--replay-seconds",
        "2",
        "--fixed-step",
    ]
    result = subprocess.run(
        command,
        cwd=repo,
        capture_output=True,
        text=True,
        timeout=timeout_seconds,
        check=False,
    )

    if result.returncode != 0:
        raise RuntimeError(
            f"causal interaction exited with code {result.returncode}:\n{result.stdout}\n{result.stderr}"
        )

    report = json.loads(report_path.read_text(encoding="utf-8"))
    validate_report(report)
    return report_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify causal hierarchy and repeated physical row selection.")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        print("PASS: causal tree interaction checker self-test")
        return 0

    repo = args.repo.resolve()
    executable = args.executable or repo / "Automation" / "SKULLBONEZ_CORE.exe"
    report_path = run_probe(repo, executable.resolve(), args.timeout_seconds)
    print(f"PASS: causal tree accepts repeated row clicks and reaches recorded depth >= 4 ({report_path})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
