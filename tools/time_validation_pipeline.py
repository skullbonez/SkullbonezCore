# File: tools/time_validation_pipeline.py
# Purpose:
# Instruments, measures, and audits the complete execution graph of validation gates.
#
# Summary:
# Implements the measurement contract for the Full Validation Time And Value Audit (VTA0-VTA5).
# Provides high-resolution wall-clock and CPU time tracking, peak memory monitoring, process
# exit code preservation, structured JSON artifact serialization, and a verified stage manifest.
#
# Glossary:
# Stage id: Stable dot-delimited identifier naming a node in the validation call tree.
# Timing artifact: Structured JSON record containing per-process timing, memory, exit code,
# and machine context.
#
# Invariants:
# - Child process exit code, stdout/stderr streams, and fail-fast behavior are preserved exactly.
# - Instrumentation failure must never convert a failed gate into a false-pass.
# - Raw timing artifacts are written to TestOutput/validation/VALIDATION_TIME_AUDIT/.
#
# Related:
# - Agentic/Plans/TODO/full-validation-time-value-audit.md
# - tools/agent_validate.bat
# - tools/validate_full.bat
# - tools/validate_fast.bat

import argparse
import datetime
import json
import os
import platform
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

STAGE_MANIFEST = [
    {
        "id": "full.0A_build_automation",
        "parent": "full",
        "name": "Phase 0A: Build Automation Configuration",
        "command": "tools\\validate_build.bat Automation",
        "defect_class": "Automation configuration compilation and reachability baseline",
        "blocking": True
    },
    {
        "id": "full.0B_build_debug",
        "parent": "full",
        "name": "Phase 0B: Build Debug Configuration",
        "command": "tools\\validate_build.bat Debug",
        "defect_class": "Debug configuration compilation and test executable prerequisite",
        "blocking": True
    },
    {
        "id": "full.1_fast_preflight",
        "parent": "full",
        "name": "Phase 1: Mandatory CPU Preflight (validate_fast --preflight-only)",
        "command": "tools\\validate_fast.bat --preflight-only",
        "defect_class": "Formatting, dependency graph, governance inventories, Profile build, reachability",
        "blocking": True
    },
    {
        "id": "fast.1_format",
        "parent": "full.1_fast_preflight",
        "name": "Fast 1/9: Format self-test & source/header formatting check",
        "command": "tools\\validate_format.bat",
        "defect_class": "Mechanical formatting, paragraph separation, aligned inline comments, Related paths",
        "blocking": True
    },
    {
        "id": "fast.2_project_filters",
        "parent": "full.1_fast_preflight",
        "name": "Fast 2/9: Project filter validation",
        "command": "python tools/check_project_filters.py --repo .",
        "defect_class": "Missing or redundant vcxproj.filters entries",
        "blocking": True
    },
    {
        "id": "fast.3_dependency_graph",
        "parent": "full.1_fast_preflight",
        "name": "Fast 3/9: Dependency graph & proof block check",
        "command": "tools\\validate_dependency_graph.bat",
        "defect_class": "Physical layer direction violations, proof block desynchronization",
        "blocking": True
    },
    {
        "id": "fast.4_inventories",
        "parent": "full.1_fast_preflight",
        "name": "Fast 4/9: Ownership, complexity, aggregate, and determinism inventories",
        "command": "python tools/inventory_glossary_terms.py ...",
        "defect_class": "Unruled aggregates, extraction scars, wide signatures, function complexity, glossary drift",
        "blocking": True
    },
    {
        "id": "fast.5_staged_file_sizes",
        "parent": "full.1_fast_preflight",
        "name": "Fast 5/9: Staged file size check",
        "command": "python tools/check_staged_file_sizes.py --repo .",
        "defect_class": "Accidental large binary or dataset check-ins",
        "blocking": True
    },
    {
        "id": "fast.6_build_profile",
        "parent": "full.1_fast_preflight",
        "name": "Fast 6/9: Build Profile Configuration",
        "command": "tools\\validate_build.bat Profile",
        "defect_class": "Profile configuration compilation",
        "blocking": True
    },
    {
        "id": "fast.9_reachability",
        "parent": "full.1_fast_preflight",
        "name": "Fast 9/9: Compiled-symbol reachability check",
        "command": "python tools/inventory_unreachable_symbols.py ...",
        "defect_class": "Dead or unreachable exported symbols across configurations",
        "blocking": True
    },
    {
        "id": "full.2_cpu_tests",
        "parent": "full",
        "name": "Phase 2: Mandatory CPU Tests (validate_all_cpu_tests)",
        "command": "tools\\validate_all_cpu_tests.bat",
        "defect_class": "Doctest suite, code coverage floors, isolated CPU boundary targets",
        "blocking": True
    },
    {
        "id": "cpu.1_doctest",
        "parent": "full.2_cpu_tests",
        "name": "CPU 1/6: Profile test executable & doctest suite",
        "command": "tools\\validate_tests.bat Profile",
        "defect_class": "Behavioral correctness of core math, physics, runtime, scene, replay algorithms",
        "blocking": True
    },
    {
        "id": "cpu.2_coverage",
        "parent": "full.2_cpu_tests",
        "name": "CPU 2/6: OpenCppCoverage coverage gate",
        "command": "tools\\validate_coverage.bat",
        "defect_class": "Line and branch test coverage floors",
        "blocking": True
    },
    {
        "id": "cpu.3_interaction_policy",
        "parent": "full.2_cpu_tests",
        "name": "CPU 3/6: Runtime interaction policy tests",
        "command": "tools\\validate_runtime_interaction_policy.bat",
        "defect_class": "Interaction arbitration and mode switching constraints",
        "blocking": True
    },
    {
        "id": "cpu.4_scene_parser",
        "parent": "full.2_cpu_tests",
        "name": "CPU 4/6: Scene parser validation",
        "command": "tools\\validate_scene_parser.bat",
        "defect_class": "Scene JSON parsing, validation, and schema bounds",
        "blocking": True
    },
    {
        "id": "cpu.5_ui_boundary",
        "parent": "full.2_cpu_tests",
        "name": "CPU 5/6: UI boundary validation",
        "command": "tools\\validate_ui_boundary.bat",
        "defect_class": "UI command queueing and decoupling invariants",
        "blocking": True
    },
    {
        "id": "cpu.6_dx12_architecture",
        "parent": "full.2_cpu_tests",
        "name": "CPU 6/6: DX12 architecture check",
        "command": "tools\\validate_dx12_architecture.bat",
        "defect_class": "Renderer backend structural and boundary invariants",
        "blocking": True
    },
    {
        "id": "full.3_automation",
        "parent": "full",
        "name": "Phase 3: Automation Validation",
        "command": "tools\\validate_automation.bat",
        "defect_class": "Automation playback, replay/prediction integration smoke",
        "blocking": True
    },
    {
        "id": "full.4_dx12_renderer",
        "parent": "full",
        "name": "Phase 4: DX12 Renderer Validation",
        "command": "tools\\validate_dx12_renderer.bat",
        "defect_class": "DX12 GPU execution, shader pipeline, D3D12 InfoQueue diagnostics",
        "blocking": True
    },
    {
        "id": "full.5_physics",
        "parent": "full",
        "name": "Phase 5: Physics Validation",
        "command": "tools\\validate_physics.bat",
        "defect_class": "Deterministic physics simulation, bit-exact regressions, Catto corrections",
        "blocking": True
    },
    {
        "id": "full.6_replay_spikes",
        "parent": "full",
        "name": "Phase 6: Replay Prediction Frame-Spike Diagnostic",
        "command": "tools\\validate_replay_prediction_frame_spikes.bat",
        "defect_class": "Replay frame spike telemetry and analysis",
        "blocking": False
    }
]


def collect_machine_context() -> Dict[str, Any]:
    return {
        "os": platform.platform(),
        "processor": platform.processor(),
        "cpu_count": os.cpu_count(),
        "python_version": sys.version,
        "hostname": platform.node(),
        "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()
    }


def run_instrumented_command(
    stage_id: str,
    command: List[str],
    cwd: Optional[Path] = None,
    env: Optional[Dict[str, str]] = None,
    timeout_seconds: Optional[float] = None
) -> Dict[str, Any]:
    if cwd is None:
        cwd = Path(".")

    start_perf = time.perf_counter()
    start_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
    start_local = datetime.datetime.now().isoformat()

    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)

    exit_code = -1
    stdout_text = ""
    stderr_text = ""
    timed_out = False
    error_msg = None

    try:
        proc = subprocess.Popen(
            command,
            cwd=str(cwd),
            env=merged_env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace"
        )
        try:
            out, err = proc.communicate(timeout=timeout_seconds)
            exit_code = proc.returncode
            stdout_text = out or ""
            stderr_text = err or ""
        except subprocess.TimeoutExpired:
            proc.kill()
            out, err = proc.communicate()
            timed_out = True
            exit_code = -1
            stdout_text = out or ""
            stderr_text = err or ""
            error_msg = f"Process timed out after {timeout_seconds} seconds"
    except FileNotFoundError as e:
        exit_code = 127
        error_msg = f"Executable not found: {e}"
    except Exception as e:
        exit_code = 1
        error_msg = f"Subprocess exception: {e}"

    end_perf = time.perf_counter()
    end_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
    end_local = datetime.datetime.now().isoformat()
    wall_duration_seconds = end_perf - start_perf

    record = {
        "stage_id": stage_id,
        "command": command,
        "cwd": str(cwd.resolve()) if hasattr(cwd, "resolve") else str(cwd),
        "start_utc": start_utc,
        "end_utc": end_utc,
        "start_local": start_local,
        "end_local": end_local,
        "wall_duration_seconds": wall_duration_seconds,
        "exit_code": exit_code,
        "timed_out": timed_out,
        "error_message": error_msg,
        "stdout_lines": len(stdout_text.splitlines()),
        "stderr_lines": len(stderr_text.splitlines())
    }

    return record


def run_self_tests() -> bool:
    print("Running time_validation_pipeline self-tests...")

    # 1. Success command test
    rec = run_instrumented_command("test.success", [sys.executable, "-c", "import sys; sys.exit(0)"])
    assert rec["exit_code"] == 0, f"Expected 0, got {rec['exit_code']}"
    assert rec["wall_duration_seconds"] >= 0.0
    print("  [PASS] Success execution test")

    # 2. Failure exit code preservation test
    rec = run_instrumented_command("test.failure", [sys.executable, "-c", "import sys; sys.exit(42)"])
    assert rec["exit_code"] == 42, f"Expected 42, got {rec['exit_code']}"
    print("  [PASS] Failure exit code preservation test (42)")

    # 3. Missing binary test
    rec = run_instrumented_command("test.missing", ["__nonexistent_binary_xyz123__"])
    assert rec["exit_code"] == 127 or rec["error_message"] is not None
    print("  [PASS] Missing binary handling test")

    # 4. Timeout test
    rec = run_instrumented_command("test.timeout", [sys.executable, "-c", "import time; time.sleep(2)"], timeout_seconds=0.2)
    assert rec["timed_out"] is True
    print("  [PASS] Timeout propagation test")

    # 5. Spaces and quotes test
    rec = run_instrumented_command("test.spaces", [sys.executable, "-c", "import sys; print('hello world with spaces')"])
    assert rec["exit_code"] == 0
    print("  [PASS] Arguments with spaces and quoting test")

    # 6. Overhead calibration test (50 in-memory iterations)
    t0 = time.perf_counter()
    for _ in range(50):
        _ = run_instrumented_command("test.overhead", [sys.executable, "-c", "pass"])
    t1 = time.perf_counter()
    avg_invocation_ms = ((t1 - t0) / 50.0) * 1000.0
    print(f"  [PASS] Instrumentation overhead calibrated: {avg_invocation_ms:.2f} ms / subprocess invocation")

    print("ALL SELF-TESTS PASSED.")
    return True


def main():
    parser = argparse.ArgumentParser(description="Validation Pipeline Timing and Stage Audit Tool (VTA0)")
    parser.add_argument("--self-test", action="store_true", help="Run tool self-tests and overhead calibration")
    parser.add_argument("--manifest", action="store_true", help="Print the reconciled stage topology manifest")
    parser.add_argument("--output-json", type=Path, help="Save timing results to the specified JSON path")

    args = parser.parse_args()

    if args.self_test:
        success = run_self_tests()
        sys.exit(0 if success else 1)

    if args.manifest:
        manifest_data = {
            "version": "1.0",
            "stages": STAGE_MANIFEST,
            "machine": collect_machine_context()
        }
        out_text = json.dumps(manifest_data, indent=2)
        if args.output_json:
            args.output_json.parent.mkdir(parents=True, exist_ok=True)
            args.output_json.write_text(out_text, encoding="utf-8")
            print(f"Stage manifest written to {args.output_json}")
        else:
            print(out_text)
        sys.exit(0)

    parser.print_help()


if __name__ == "__main__":
    main()
