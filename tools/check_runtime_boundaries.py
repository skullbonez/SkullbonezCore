#!/usr/bin/env python3
#
# File: tools/check_runtime_boundaries.py
# Purpose:
#   Check that Run.h stays a runtime composition root instead of regrowing
#   extracted subsystem ownership.
#
# Mental model:
#   Runtime decomposition is easy to regress by adding one convenient field or
#   helper back to Run. This check is intentionally small: it watches the
#   boundaries named by Agentic/Plans/runtime-run-decomposition-plan.md.
#
# Invariants:
#   - Run.h may own subsystem objects, but not their extracted transient state.
#   - Subsystems may borrow explicit service/context structs, but not store Run.
#
# Related:
#   - Agentic/Plans/runtime-run-decomposition-plan.md
#   - tools/validate_fast.bat
#
"""Validate runtime Run-boundary guardrails."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


RUN_HEADER = Path("SkullbonezSource/Runtime/Run.h")
RUNTIME_ROOT = Path("SkullbonezSource/Runtime")
FIELD_TAIL_PATTERN = r"(?=[^;{}]*\bm_[A-Za-z_]\w*)[^;{}]*;"
RUN_NAME_PATTERN = r"(?:(?:[A-Za-z_]\w*::)*Run)\b"
RUN_CV_PATTERN = rf"(?:const\s+{RUN_NAME_PATTERN}|{RUN_NAME_PATTERN}\s+const|{RUN_NAME_PATTERN})"

RUN_HEADER_RULES: tuple[tuple[str, str, str], ...] = (
    (
        "render pass class definitions must stay out of Run.h",
        r"\b(?:class|struct)\s+(?:FullscreenQuadPass|SkyPass|SceneTargetPass|ShadowPass|ReflectionPass|ObjectPass|"
        r"TerrainPass|WaterPass|TornadoVisualPass|DebugOverlayPass|VolumetricPass|TonemapPass|UiTextPass)\b",
        "Put render pass types in Runtime/Render/RuntimeRenderPasses.h.",
    ),
    (
        "replay recorder fields must stay out of Run.h",
        r"\b(?:ReplayRecorder|ReplaySolverRecorder|ReplayEventRecorder|ReplayBranchInfo)\b" + FIELD_TAIL_PATTERN,
        "ReplayRuntime owns recorder and branch state.",
    ),
    (
        "tool transient fields must stay out of Run.h",
        r"\b(?:RunRayCastTestState|LauncherLaser|RunMousePickupState|RunEditorPlacementState|RunEditorTracer)\b"
        + FIELD_TAIL_PATTERN,
        "RuntimeTools owns launcher, manipulator, and editor transient state.",
    ),
    (
        "scene object population helpers must stay out of Run.h",
        r"\b(?:SetUpGameModels|SetUpSolverObjects|SetUpCamerasFromScene|SetUpGameModelsFromScene|"
        r"SetUpRequiredContactsFromScene|SetUpRequiredBroadphaseXCellsFromScene)\s*\(",
        "Call SceneGeneratedSetup or SceneAuthoredSetup through explicit context builders.",
    ),
)

RUN_STORAGE_RULE = (
    "subsystems must not store Run by pointer or reference",
    rf"(?:\b{RUN_CV_PATTERN}\s*(?:[&*]\s*(?:const\s*)?)?|"
    rf"\bstd::(?:reference_wrapper|unique_ptr|shared_ptr|weak_ptr|optional)<\s*{RUN_CV_PATTERN}\s*>\s*)"
    + FIELD_TAIL_PATTERN,
    "Use EngineContext, RuntimeRenderHost, or a subsystem-specific service struct instead.",
)


@dataclass(frozen=True)
class BoundaryError:
    path: Path
    line: int
    message: str
    detail: str


def repo_relative(repo: Path, path: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo.resolve())).replace("/", "\\")
    except ValueError:
        return str(path)


def strip_cpp_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda match: "\n" * match.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//.*", "", text)


def line_for_offset(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def check_text_rules(path: Path, text: str, rules: tuple[tuple[str, str, str], ...]) -> list[BoundaryError]:
    stripped = strip_cpp_comments(text)
    errors: list[BoundaryError] = []
    for message, pattern, detail in rules:
        for match in re.finditer(pattern, stripped):
            errors.append(BoundaryError(path, line_for_offset(stripped, match.start()), message, detail))
    return errors


def check_run_storage(repo: Path) -> list[BoundaryError]:
    message, pattern, detail = RUN_STORAGE_RULE
    errors: list[BoundaryError] = []
    for path in sorted((repo / RUNTIME_ROOT).rglob("*.h")):
        if path.name == "Run.h":
            continue
        text = path.read_text(encoding="utf-8")
        stripped = strip_cpp_comments(text)
        for match in re.finditer(pattern, stripped):
            errors.append(BoundaryError(path, line_for_offset(stripped, match.start()), message, detail))
    return errors


def validate_runtime_boundaries(repo: Path) -> list[BoundaryError]:
    run_header = repo / RUN_HEADER
    errors = check_text_rules(run_header, run_header.read_text(encoding="utf-8"), RUN_HEADER_RULES)
    errors.extend(check_run_storage(repo))
    return errors


def write_summary(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--max-errors", type=int, default=80)
    args = parser.parse_args()

    repo = args.repo.resolve()
    summary_path = args.json_out or repo / "TestOutput" / "validation" / "runtime_boundaries" / "summary.json"
    errors = validate_runtime_boundaries(repo)
    summary = {
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "status": "pass" if not errors else "fail",
        "errorCount": len(errors),
        "errors": [
            {
                "path": repo_relative(repo, error.path),
                "line": error.line,
                "message": error.message,
                "detail": error.detail,
            }
            for error in errors
        ],
    }
    write_summary(summary_path, summary)

    for error in errors[: args.max_errors]:
        print(
            f"ERROR: {repo_relative(repo, error.path)}:{error.line}: "
            f"{error.message} {error.detail}"
        )
    if len(errors) > args.max_errors:
        print(f"ERROR: suppressed {len(errors) - args.max_errors} additional runtime boundary issue(s).")

    print(f"Runtime boundary summary: {repo_relative(repo, summary_path)} ({len(errors)} errors)")
    if errors:
        print("FAIL: Runtime boundary validation failed.")
        return 1

    print("PASS: Runtime boundary validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
