#
# File: tools/check_physics_query_regression.py
# Purpose:
#   Documents and runs the check_physics_query_regression.py developer/validation helper script.
#
# Summary:
#   Tools are command-line guardrails around builds, validation, screenshots,
#   diagnostics, and artifact handling. They make the safe path repeatable and
#   keep output bounded for humans and agents.
#
# Glossary:
#   SkullScope: Queryable physics diagnostics workflow backed by bounded trace
#   output and local queries.
#   JSON (JavaScript Object Notation): Structured text format used by
#   diagnostics, baselines, and tool reports.
#   Validation gate: Repository script that proves a class of changes before
#   commit or PR.
#
# Invariants:
#   - Tool output should be bounded and readable because agents and humans use
#     it for decisions.
#   - The default solver packet remains byte-exact; convergence diagnostics are
#     exercised separately through their explicit opt-in flag.
#   - A Physics-plan update requires an exact candidate hash and retained-runtime
#     transition manifest before this tool writes the committed oracle.
#
# Related:
#   - AGENTS.md
#
#
"""
Regression check for SkullScope queryable physics diagnostics.

Generates a deterministic physics diagnostics trace from physics_bench_varied.scene.json,
runs a fixed set of representative queries through tools/physics_query.py, removes
machine-local cache paths from the results, and compares the compact JSON packet
against a committed baseline.
"""

import argparse
import difflib
import json
import os
from pathlib import Path
import subprocess
import sys

from check_physics_baseline_guard import sha256_bytes, validate_physics_plan_transition


REPO = Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1])).resolve()
BASELINE = REPO / "TestOutput" / "baselines" / "physics_query_varied.json"
TRACE = REPO / "Debug" / "physics_query_varied.physicsdiag.ndjson"
SCENE_ARG = "SkullbonezData/scenes/physics_bench_varied.scene.json"
EXE = REPO / "Debug" / "SKULLBONEZ_CORE.exe"
QUERY_TOOL = REPO / "tools" / "physics_query.py"

DROP_KEYS = {
    "cache",
    "otherCache",
    "questionsFile",
    "batCommands",
    "batFollowups",
}

QUERIES = [
    ("summary", ["summary", "--limit", "8"]),
    ("events", ["events", "--limit", "20"]),
    ("frame_600", ["frame", "600", "--limit", "8"]),
    ("body_roll_a", ["body", "roll_a", "--frames", "0:1200", "--limit", "12"]),
    ("energy", ["energy", "--frames", "0:1200", "--limit", "12"]),
    ("events_penetration", ["events", "--type", "penetration_sustained,penetration_growing", "--limit", "20"]),
    ("contacts_penetration", ["contacts", "--top", "penetration", "--limit", "12"]),
    ("island_1_final", ["island", "1", "--frame", "1199", "--limit", "12"]),
    ("stacks", ["stacks", "--frames", "0:1200", "--limit", "12"]),
    ("rolling", ["rolling", "--frames", "0:1200", "--limit", "12"]),
    ("broadphase", ["broadphase", "--frames", "0:1200", "--limit", "12"]),
    ("solver", ["solver", "--frames", "0:1200", "--limit", "12"]),
    ("pipeline", ["pipeline", "--frames", "0:1200", "--limit", "12"]),
    ("question_penetration_spikes", ["questions", "penetration_spikes"]),
    ("question_stack_health", ["questions", "stack_health"]),
    ("compare_self", ["compare", str(TRACE), "--limit", "8"]),
]

def remove_if_exists(path):
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def normalize(value):
    if isinstance(value, dict):
        return {key: normalize(item) for key, item in value.items() if key not in DROP_KEYS}
    if isinstance(value, list):
        return [normalize(item) for item in value]
    if isinstance(value, str):
        return normalize_string(value)
    return value


def normalize_string(value):
    candidate = value.replace("\\", "/")
    repo_prefix = str(REPO).replace("\\", "/")
    if candidate.lower().startswith(repo_prefix.lower()):
        return candidate[len(repo_prefix):].lstrip("/")

    scene_marker = "/SkullbonezData/"
    marker_index = candidate.lower().find(scene_marker.lower())
    if marker_index >= 0 and ":" in candidate[:marker_index]:
        return candidate[marker_index + 1:]

    return value


def run_checked(args, cwd):
    result = subprocess.run(
        args,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        raise RuntimeError(f"command failed with exit code {result.returncode}: {' '.join(map(str, args))}")
    return result.stdout


def generate_trace(trace, extra_args):
    if not EXE.exists():
        raise RuntimeError(f"Debug executable not found: {EXE}")
    remove_if_exists(trace)
    remove_if_exists(trace.with_suffix(".sqlite"))
    remove_if_exists(trace.with_suffix(".sqlite.lock"))
    run_checked(
        [
            str(EXE),
            "--renderer",
            "dx12",
            "--vsync",
            "off",
            "--shadows",
            "off",
            "--scene",
            SCENE_ARG,
            *extra_args,
            "--physics-diag",
            str(trace),
        ],
        REPO,
    )
    if not trace.exists():
        raise RuntimeError(f"diagnostic trace was not produced: {trace}")


def run_query_set(trace, queries):
    outputs = {}
    for name, query_args in queries:
        stdout = run_checked([sys.executable, str(QUERY_TOOL), str(trace)] + query_args, REPO)
        outputs[name] = normalize(json.loads(stdout))
    return outputs


def run_queries():
    packet = {
        "name": "SkullScope physics query regression",
        "scene": "SkullbonezData/scenes/physics_bench_varied.scene.json",
        "traceKind": "physicsdiag.ndjson",
        "queries": run_query_set(TRACE, QUERIES),
    }
    return packet


def verify_convergence_projection():
    payload = run_query_set(
        TRACE,
        [("solver_convergence", ["solver", "--frames", "0:1200", "--limit", "12", "--include-convergence"])],
    )["solver_convergence"]
    stats = payload.get("convergenceStats")
    worst = payload.get("convergenceWorst")
    if not isinstance(stats, dict) or int(stats.get("sample_count") or 0) <= 0:
        raise RuntimeError("opt-in solver convergence statistics are missing or empty")
    if not isinstance(worst, list) or not worst:
        raise RuntimeError("opt-in solver convergence sample is missing or empty")
    print("  PASS: opt-in solver convergence projection is populated")


def canonical_json(packet):
    return json.dumps(packet, indent=2, sort_keys=True) + "\n"


def compare_or_update(current_text, candidate_sha256, artifact_manifest):
    if candidate_sha256 is not None:
        if not BASELINE.exists():
            raise RuntimeError("automated override requires a tracked predecessor baseline")
        current_bytes = current_text.encode("utf-8")
        current_digest = sha256_bytes(current_bytes)
        if candidate_sha256.lower() != current_digest:
            raise RuntimeError(
                f"candidate SHA-256 does not match generated SkullScope baseline: "
                f"expected={current_digest} supplied={candidate_sha256.lower()}"
            )
        previous_digest = sha256_bytes(BASELINE.read_bytes())
        validate_physics_plan_transition(
            REPO,
            artifact_manifest,
            BASELINE.relative_to(REPO).as_posix(),
            previous_digest,
            current_digest,
            EXE,
            "Debug|x64",
        )
        BASELINE.parent.mkdir(parents=True, exist_ok=True)
        temporary = BASELINE.with_suffix(BASELINE.suffix + ".tmp")
        temporary.write_bytes(current_bytes)
        os.replace(temporary, BASELINE)
        print(f"  AUTOMATED OVERRIDE: {BASELINE.relative_to(REPO)} ({current_digest})")
        return 0

    expected_text = canonical_json(normalize(json.loads(BASELINE.read_text(encoding="utf-8"))))
    if expected_text == current_text:
        print(f"  PASS: {BASELINE.name} exact match")
        return 0

    print(f"  FAIL: {BASELINE.name} differs from current SkullScope query output")
    diff = difflib.unified_diff(
        expected_text.splitlines(),
        current_text.splitlines(),
        fromfile="baseline",
        tofile="current",
        lineterm="",
    )
    for index, line in enumerate(diff):
        if index >= 120:
            print("  ... diff truncated after 120 lines")
            break
        print(line)
    return 1


def main():
    parser = argparse.ArgumentParser(description="Check SkullScope physics query output against baseline.")
    parser.add_argument(
        "--automated-override-sha256",
        help="exact candidate SHA-256 for an archived Physics-plan baseline transition",
    )
    parser.add_argument("--artifact-manifest", type=Path)
    parser.add_argument("--update", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.update:
        parser.error(
            "--update now requires the archived automated lane; use "
            "--automated-override-sha256 and --artifact-manifest"
        )
    if (args.automated_override_sha256 is None) != (args.artifact_manifest is None):
        parser.error("--automated-override-sha256 and --artifact-manifest are required together")

    try:
        print("  Generating SkullScope trace from physics_bench_varied.scene.json...")
        generate_trace(TRACE, [])
        print("  Checking opt-in solver convergence projection...")
        verify_convergence_projection()
        print("  Running SkullScope query packet...")
        current_text = canonical_json(run_queries())
        return compare_or_update(
            current_text, args.automated_override_sha256, args.artifact_manifest
        )
    except Exception as exc:
        print(f"  FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
