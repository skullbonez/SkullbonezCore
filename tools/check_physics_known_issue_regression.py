#
# File: tools/check_physics_known_issue_regression.py
# Purpose:
#   Compares compact signatures for known-risk physics scenes.
#
# Mental model:
#   Tools are command-line guardrails around builds, validation, screenshots,
#   diagnostics, and artifact handling. Known-risk physics scenes can be too
#   large for committed CSV baselines, so this checker records exact file
#   signatures while leaving the raw artifacts in Debug.
#
# Glossary:
#   SHA-256: Content hash used to detect any byte-level change in an artifact.
#   Validation gate: Repository script that proves a class of changes before
#   commit or PR.
#
# Invariants:
#   - Known-risk scene behavior changes must be explicit baseline updates.
#
# Related:
#   - tools/validate_physics.bat
#   - tools/check_physics_regression.py
#   - Agentic/Reference/comment-style-guide.md
#
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import sys


REPO = Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1])).resolve()
BASELINE = REPO / "TestOutput" / "baselines" / "physics_known_issue_signatures.json"

KNOWN_ISSUES = [
    {
        "name": "stacking_stability_watch",
        "risk": "Stacking drift or topple in stacking.scene.json.",
        "scene": "SkullbonezData/scenes/stacking.scene.json",
        "artifact": "Debug/physics_known_stacking.csv",
    },
    {
        "name": "at_rest_settling_watch",
        "risk": "Mixed ball/box resting jitter or interpenetration in at_rest.scene.json.",
        "scene": "SkullbonezData/scenes/at_rest.scene.json",
        "artifact": "Debug/physics_known_at_rest.csv",
    },
    {
        "name": "terrain_contact_watch",
        "risk": "Terrain contact micro-bounce or support classification drift.",
        "scene": "SkullbonezData/scenes/terrain_contact_probe_debug.scene.json",
        "artifact": "Debug/physics_known_terrain_contact.csv",
    },
]


def signature_for(artifact):
    path = REPO / artifact
    if not path.exists():
        raise FileNotFoundError(f"{artifact} was not produced")

    data = path.read_bytes()
    return {
        "artifact": artifact,
        "bytes": len(data),
        "lines": data.count(b"\n"),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def build_packet():
    tests = []
    for item in KNOWN_ISSUES:
        entry = {
            "name": item["name"],
            "risk": item["risk"],
            "scene": item["scene"],
        }
        entry.update(signature_for(item["artifact"]))
        tests.append(entry)

    return {
        "name": "Known physics issue regression signatures",
        "format": "sha256 over Debug physics CSV artifacts",
        "tests": tests,
    }


def canonical_json(packet):
    return json.dumps(packet, indent=2, sort_keys=True) + "\n"


def compare_or_update(current_text, update):
    if update:
        BASELINE.parent.mkdir(parents=True, exist_ok=True)
        BASELINE.write_text(current_text, encoding="utf-8")
        print(f"  UPDATED: {BASELINE.relative_to(REPO)}")
        return 0

    if not BASELINE.exists():
        print(f"  FAIL: missing committed baseline {BASELINE.relative_to(REPO)}")
        print("        Run this checker with --update only when intentionally refreshing the known-issue signatures.")
        return 1

    expected_text = canonical_json(json.loads(BASELINE.read_text(encoding="utf-8")))
    if expected_text == current_text:
        print(f"  PASS: {BASELINE.name} exact signature match")
        return 0

    print(f"  FAIL: {BASELINE.name} differs from current known-issue signatures")
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
    parser = argparse.ArgumentParser(description="Check known physics issue CSV signatures against baseline.")
    parser.add_argument("--update", action="store_true", help="Update the committed signature baseline.")
    args = parser.parse_args()

    try:
        return compare_or_update(canonical_json(build_packet()), args.update)
    except Exception as exc:
        print(f"  FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
