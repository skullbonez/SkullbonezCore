#
# File: tools/check_physics_query_regression.py
# Purpose:
#   Documents and runs the check_physics_query_regression.py developer/validation helper script.
#
# Mental model:
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
#   it for decisions.
#
# Related:
#   - AGENTS.md
#   - Agentic/Reference/comment-style-guide.md
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


def canonical_json(packet):
    return json.dumps(packet, indent=2, sort_keys=True) + "\n"


def compare_or_update(current_text, update):
    if update or not BASELINE.exists():
        BASELINE.parent.mkdir(parents=True, exist_ok=True)
        BASELINE.write_text(current_text, encoding="utf-8")
        action = "UPDATED" if update else "CREATED"
        print(f"  {action}: {BASELINE.relative_to(REPO)}")
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
    parser.add_argument("--update", action="store_true", help="Update the committed baseline.")
    args = parser.parse_args()

    try:
        print("  Generating SkullScope trace from physics_bench_varied.scene.json...")
        generate_trace(TRACE, [])
        print("  Running SkullScope query packet...")
        current_text = canonical_json(run_queries())
        return compare_or_update(current_text, args.update)
    except Exception as exc:
        print(f"  FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
