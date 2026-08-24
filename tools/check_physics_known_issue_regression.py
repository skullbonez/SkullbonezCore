#
# File: tools/check_physics_known_issue_regression.py
# Purpose:
#   Compares compact signatures for known-risk physics scenes.
#
# Summary:
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
#   - A Physics-plan update requires an exact candidate hash and retained-runtime
#     transition manifest before this tool writes the committed oracle.
#
# Related:
#   - tools/validate_physics.bat
#   - tools/check_physics_regression.py
#
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import sys

from check_physics_baseline_guard import sha256_bytes, validate_physics_plan_transition


REPO = Path(os.environ.get("SKORE_REPO", Path(__file__).resolve().parents[1])).resolve()
BASELINE = REPO / "TestOutput" / "baselines" / "physics_known_issue_signatures.json"
EXE = REPO / "Debug" / "SKULLBONEZ_CORE.exe"

KNOWN_ISSUES = [
    {
        "name": "seeded_solver_distribution_watch",
        "risk": "Seeded mixed solver distribution drifts after the varied scene becomes the primary full CSV baseline.",
        "scene": "SkullbonezData/scenes/physics_regression_solver.scene.json",
        "artifact": "Debug/physics_regression_solver.csv",
    },
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


def compare_or_update(current_text, candidate_sha256, artifact_manifest):
    if candidate_sha256 is not None:
        if not BASELINE.exists():
            raise RuntimeError("automated override requires a tracked predecessor baseline")
        current_bytes = current_text.encode("utf-8")
        current_digest = sha256_bytes(current_bytes)
        if candidate_sha256.lower() != current_digest:
            raise RuntimeError(
                f"candidate SHA-256 does not match generated known-issue baseline: "
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

    if not BASELINE.exists():
        print(f"  FAIL: missing committed baseline {BASELINE.relative_to(REPO)}")
        print("        Restore the tracked baseline before running validation.")
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
        return compare_or_update(
            canonical_json(build_packet()),
            args.automated_override_sha256,
            args.artifact_manifest,
        )
    except Exception as exc:
        print(f"  FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
